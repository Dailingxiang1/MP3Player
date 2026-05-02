#include "audio_shared.h"

#include <stdio.h>
#include <string.h>

static void AudioDecode_MonoToStereo(int16_t *pcm, int samples_per_channel)
{
    int i;

    for (i = samples_per_channel - 1; i >= 0; i--)
    {
        int16_t sample = pcm[i];
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }
}

/**
 * @brief 从压缩流 ring buffer 中准备一段连续的线性视图给 minimp3。
 *
 * 说明：
 * - 如果读指针到写指针之间本来就是连续的，直接使用 ring buffer 尾部；
 * - 如果数据跨越缓冲区尾部，则拷贝到线性视图缓冲区；
 * - 这样能在“结构仍然简单”的前提下实现 ring buffer。
 */
static uint8_t *AudioDecode_PrepareLinearView(audio_context_t *ctx, uint32_t *linear_bytes)
{
    uint32_t contiguous_bytes;

    contiguous_bytes = AudioApp_StreamGetContiguousRead(ctx);
    *linear_bytes = AudioApp_StreamGetLevel(ctx);
    if (*linear_bytes == 0U)
    {
        return NULL;
    }

    if (contiguous_bytes == *linear_bytes)
    {
        return &AudioApp_GetStreamBuffer()[ctx->stream_read_index];
    }

    *linear_bytes = AudioApp_StreamLinearizeForDecode(ctx);
    return AudioApp_GetLinearViewBuffer();
}

static int AudioDecode_OneFrame(audio_context_t *ctx,
                                int16_t *pcm,
                                uint16_t *out_samples)
{
    int guard;

    *out_samples = 0U;

    for (guard = 0; guard < AUDIO_DECODE_SEARCH_GUARD; guard++)
    {
        int samples_per_channel;
        uint32_t linear_bytes;
        uint8_t *linear_view;

        linear_view = AudioDecode_PrepareLinearView(ctx, &linear_bytes);
        if ((linear_view == NULL) || (linear_bytes == 0U))
        {
            return 0;
        }

        samples_per_channel = mp3dec_decode_frame(&ctx->decoder,
                                                  linear_view,
                                                  (int)linear_bytes,
                                                  pcm,
                                                  &ctx->frame_info);

        if (ctx->frame_info.frame_bytes > 0)
        {
            AudioApp_StreamConsume(ctx, (uint32_t)ctx->frame_info.frame_bytes);
        }

        if ((samples_per_channel > 0) && (ctx->frame_info.frame_bytes > 0))
        {
            ctx->current_sample_rate = (uint32_t)ctx->frame_info.hz;
            ctx->current_bitrate_kbps = (uint16_t)ctx->frame_info.bitrate_kbps;

            if (ctx->frame_info.channels == 1)
            {
                AudioDecode_MonoToStereo(pcm, samples_per_channel);
                ctx->current_channels = 2U;
                *out_samples = (uint16_t)(samples_per_channel * 2);
            }
            else if (ctx->frame_info.channels == 2)
            {
                ctx->current_channels = 2U;
                *out_samples = (uint16_t)(samples_per_channel * 2);
            }
            else
            {
                ctx->decode_skip_count++;
                continue;
            }

            ctx->decoded_frame_count++;
            return 1;
        }

        if (AudioApp_StreamGetLevel(ctx) > 0U)
        {
            AudioApp_StreamConsume(ctx, 1U);
            ctx->decode_skip_count++;
        }
    }

    return 0;
}

void AudioDecodeTask(void *argument)
{
    audio_context_t *ctx;
    uint32_t notify_value;
    uint32_t local_song_generation;

    (void)argument;
    ctx = AudioApp_GetContext();
    local_song_generation = 0U;

    for (;;)
    {
        int block_index;
        int decode_result;
        uint16_t out_samples;

        if (ctx->file_open == 0U)
        {
            (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_value, pdMS_TO_TICKS(100));
            continue;
        }

        if (local_song_generation != ctx->song_generation)
        {
            local_song_generation = ctx->song_generation;
            mp3dec_init(&ctx->decoder);
            printf("[DEC] new song generation=%lu\r\n", (unsigned long)local_song_generation);
        }

        block_index = AudioApp_ReserveEmptyBlock(ctx);
        if (block_index < 0)
        {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
            continue;
        }

        if (xSemaphoreTake(ctx->stream_mutex, portMAX_DELAY) != pdTRUE)
        {
            AudioApp_ReleaseBlock(ctx, (uint8_t)block_index);
            continue;
        }

        decode_result = AudioDecode_OneFrame(ctx,
                                             AudioApp_GetPcmBlock((uint8_t)block_index),
                                             &out_samples);

        if (AudioApp_StreamGetLevel(ctx) < AUDIO_STREAM_REFILL_WATERMARK)
        {
            if (ctx->sd_task_handle != NULL)
            {
                (void)xTaskNotify(ctx->sd_task_handle, AUDIO_SD_NOTIFY_REFILL, eSetBits);
            }
        }

        (void)xSemaphoreGive(ctx->stream_mutex);

        /*
         * 如果在本次解码前后歌曲已经被切换，则这块 PCM 属于旧歌，
         * 直接丢弃，避免切歌瞬间把旧歌尾巴播出去。
         */
        if ((local_song_generation != ctx->song_generation) || (ctx->requested_track_step != 0))
        {
            AudioApp_ReleaseBlock(ctx, (uint8_t)block_index);
            continue;
        }

        if (decode_result > 0)
        {
            AudioApp_PublishBlock(ctx, (uint8_t)block_index, out_samples);

            /*
             * 切歌/首歌启动时，只有当 ready PCM 块数达到预充门限后，
             * 才允许播放任务真正起播。
             *
             * 这一步相当于给“切歌后的新歌”建立一个屏障：
             * - 先完成至少 2 块 PCM 的准备
             * - 再从 SWITCHING 切到 BUFFERING
             *
             * 这样可以避免“切歌时 PCM 一度为 0，然后再也起不来”的情况。
             */
            if (ctx->decoded_frame_count == 1U)
            {
                printf("[DEC] first frame: hz=%lu ch=%u br=%uk\r\n",
                       (unsigned long)ctx->current_sample_rate,
                       (unsigned int)ctx->current_channels,
                       (unsigned int)ctx->current_bitrate_kbps);
            }

            if (ctx->play_task_handle != NULL)
            {
                (void)xTaskNotify(ctx->play_task_handle,
                                  AUDIO_PLAY_NOTIFY_PCM_READY,
                                  eSetBits);
            }

            /*
             * 解码成功后只主动让出一个时间片，而不是人为把 ready block
             * 长期限制在 1~2 块。这样既能给 LVGL 任务运行机会，
             * 又能让 PCM 缓冲在空闲时自然填到 3/3，提高抗抖能力。
             */
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        AudioApp_ReleaseBlock(ctx, (uint8_t)block_index);

        if ((ctx->stream_eof != 0U) && (AudioApp_StreamGetLevel(ctx) == 0U))
        {
            if (ctx->decode_done == 0U)
            {
                ctx->decode_done = 1U;
                printf("[DEC] decode finished frames=%lu bytes=%lu refill=%lu skip=%lu\r\n",
                       (unsigned long)ctx->decoded_frame_count,
                       (unsigned long)ctx->sd_read_bytes,
                       (unsigned long)ctx->sd_refill_count,
                       (unsigned long)ctx->decode_skip_count);
                if (ctx->play_task_handle != NULL)
                {
                    (void)xTaskNotify(ctx->play_task_handle,
                                      AUDIO_PLAY_NOTIFY_DECODE_DONE,
                                      eSetBits);
                }
            }
            (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_value, pdMS_TO_TICKS(100));
        }
        else
        {
            (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_value, pdMS_TO_TICKS(10));
        }
    }
}
