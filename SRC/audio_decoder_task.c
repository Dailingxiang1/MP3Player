#include "audio_shared.h"

#include <stdio.h>
#include <string.h>

static void AudioDecoder_MonoToStereo(int16_t *pcm, int samples_per_channel)
{
    int i;

    for (i = samples_per_channel - 1; i >= 0; i--)
    {
        int16_t sample = pcm[i];
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }
}

static FRESULT AudioDecoder_Refill(audio_context_t *ctx)
{
    uint8_t *stream;
    UINT bytes_read;
    FRESULT fres;
    int tail_free;
    int to_read;

    if ((ctx->eof != 0U) || (ctx->stream_bytes >= (int)AUDIO_MP3_REFILL_THRESHOLD))
    {
        return FR_OK;
    }

    stream = AudioApp_GetStreamBuffer();

    if (ctx->stream_offset > (int)(AUDIO_MP3_STREAM_BUFFER_SIZE / 2U))
    {
        if (ctx->stream_bytes > 0)
        {
            memmove(stream, &stream[ctx->stream_offset], (size_t)ctx->stream_bytes);
        }
        ctx->stream_offset = 0;
    }

    tail_free = (int)AUDIO_MP3_STREAM_BUFFER_SIZE - (ctx->stream_offset + ctx->stream_bytes);
    if (tail_free < (int)AUDIO_MP3_READ_CHUNK)
    {
        if (ctx->stream_bytes > 0)
        {
            memmove(stream, &stream[ctx->stream_offset], (size_t)ctx->stream_bytes);
        }
        ctx->stream_offset = 0;
        tail_free = (int)AUDIO_MP3_STREAM_BUFFER_SIZE - ctx->stream_bytes;
    }

    to_read = tail_free;
    if (to_read > (int)AUDIO_MP3_READ_CHUNK)
    {
        to_read = (int)AUDIO_MP3_READ_CHUNK;
    }

    to_read &= ~511;
    if (to_read <= 0)
    {
        return FR_OK;
    }

    bytes_read = 0U;
    fres = f_read(ctx->file,
                  &stream[ctx->stream_offset + ctx->stream_bytes],
                  (UINT)to_read,
                  &bytes_read);
    if (fres != FR_OK)
    {
        return fres;
    }

    ctx->sd_read_bytes += (uint32_t)bytes_read;
    if (bytes_read == 0U)
    {
        ctx->eof = 1U;
    }
    else
    {
        ctx->stream_bytes += (int)bytes_read;
    }

    return FR_OK;
}

static int AudioDecoder_DecodeOneFrame(audio_context_t *ctx,
                                       int16_t *pcm,
                                       uint16_t *out_samples)
{
    uint8_t *stream;
    int guard;

    stream = AudioApp_GetStreamBuffer();
    *out_samples = 0U;

    for (guard = 0; guard < AUDIO_MP3_SEARCH_GUARD; guard++)
    {
        int samples_per_channel;
        FRESULT fres;

        fres = AudioDecoder_Refill(ctx);
        if (fres != FR_OK)
        {
            ctx->last_fres = fres;
            ctx->run_state = AUDIO_TASK_STATE_ERROR;
            return -1;
        }

        if (ctx->stream_bytes <= 0)
        {
            return 0;
        }

        samples_per_channel = mp3dec_decode_frame(&ctx->decoder,
                                                  &stream[ctx->stream_offset],
                                                  ctx->stream_bytes,
                                                  pcm,
                                                  &ctx->frame_info);

        if (ctx->frame_info.frame_bytes > 0)
        {
            ctx->stream_offset += ctx->frame_info.frame_bytes;
            ctx->stream_bytes -= ctx->frame_info.frame_bytes;
        }

        if ((samples_per_channel > 0) && (ctx->frame_info.frame_bytes > 0))
        {
            ctx->current_sample_rate = (uint32_t)ctx->frame_info.hz;
            ctx->current_bitrate_kbps = (uint16_t)ctx->frame_info.bitrate_kbps;

            if (ctx->frame_info.channels == 1)
            {
                AudioDecoder_MonoToStereo(pcm, samples_per_channel);
                ctx->current_channels = 2U;
                *out_samples = (uint16_t)(samples_per_channel * 2);
            }
            else if (ctx->frame_info.channels == 2)
            {
                ctx->current_channels = (uint16_t)ctx->frame_info.channels;
                *out_samples = (uint16_t)(samples_per_channel * ctx->frame_info.channels);
            }
            else
            {
                ctx->decode_skip_count++;
                continue;
            }

            ctx->decoded_frame_count++;
            return 1;
        }

        if (ctx->frame_info.frame_bytes <= 0)
        {
            ctx->stream_offset++;
            ctx->stream_bytes--;
            ctx->decode_skip_count++;
        }
    }

    return 0;
}

static int AudioDecoder_DecodeBlock(audio_context_t *ctx, uint8_t block_index)
{
    int16_t *pcm;
    uint16_t total_samples;
    uint16_t frame_samples;

    pcm = AudioApp_GetPcmBlock(block_index);
    total_samples = 0U;

    while ((total_samples + MINIMP3_MAX_SAMPLES_PER_FRAME) <= AUDIO_PCM_BLOCK_SAMPLES)
    {
        int result = AudioDecoder_DecodeOneFrame(ctx, &pcm[total_samples], &frame_samples);

        if (result < 0)
        {
            return -1;
        }

        if (result == 0)
        {
            break;
        }

        total_samples = (uint16_t)(total_samples + frame_samples);

        if (ctx->decoded_frame_count == 1U)
        {
            printf("[DEC] first frame: hz=%lu ch=%u br=%uk\r\n",
                   (unsigned long)ctx->current_sample_rate,
                   (unsigned int)ctx->current_channels,
                   (unsigned int)ctx->current_bitrate_kbps);
        }
    }

    if (total_samples != 0U)
    {
        AudioApp_PublishBlock(ctx, block_index, total_samples);
        return 1;
    }

    return 0;
}

static FRESULT AudioDecoder_OpenCurrentFile(audio_context_t *ctx)
{
    FRESULT fres;

    fres = f_open(ctx->file, ctx->file_path, FA_READ);
    if (fres != FR_OK)
    {
        return fres;
    }

    ctx->file_open = 1U;
    ctx->run_state = AUDIO_TASK_STATE_BUFFERING;
    mp3dec_init(&ctx->decoder);
    return FR_OK;
}

void AudioDecodeTask(void *argument)
{
    audio_context_t *ctx;
    FRESULT fres;

    (void)argument;
    ctx = AudioApp_GetContext();

    AudioApp_ResetPipeline(ctx);

    printf("[DEC] mp3 stream profile=%s buf=%lu read=%lu refill=%lu\r\n",
           AUDIO_MP3_STREAM_PROFILE_TEXT,
           (unsigned long)AUDIO_MP3_STREAM_BUFFER_SIZE,
           (unsigned long)AUDIO_MP3_READ_CHUNK,
           (unsigned long)AUDIO_MP3_REFILL_THRESHOLD);
    printf("[DEC] mount sd...\r\n");
    fres = f_mount(ctx->fs, SDPath, 1);
    if (fres != FR_OK)
    {
        ctx->last_fres = fres;
        ctx->run_state = AUDIO_TASK_STATE_ERROR;
        printf("[DEC] f_mount failed: %d\r\n", (int)fres);
        if (ctx->play_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
        }
        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ctx->fs_mounted = 1U;

    for (;;)
    {
        if (ctx->file_path[0] == '\0')
        {
            fres = AudioApp_FindFirstMp3(ctx, AUDIO_FILE_SCAN_PATH);
            if (fres != FR_OK)
            {
                ctx->last_fres = fres;
                ctx->run_state = AUDIO_TASK_STATE_ERROR;
                printf("[DEC] no mp3 file found, fres=%d\r\n", (int)fres);
            }
        }

        if (ctx->run_state == AUDIO_TASK_STATE_ERROR)
        {
            if (ctx->play_task_handle != NULL)
            {
                (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
            }
            for (;;)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        printf("[DEC] open %s\r\n", ctx->file_path);
        fres = AudioDecoder_OpenCurrentFile(ctx);
        if (fres != FR_OK)
        {
            ctx->last_fres = fres;
            ctx->run_state = AUDIO_TASK_STATE_ERROR;
            printf("[DEC] f_open failed: %d\r\n", (int)fres);
            if (ctx->play_task_handle != NULL)
            {
                (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
            }
            for (;;)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        for (;;)
        {
            int block_index;
            int decode_result;

            if (ctx->next_song_request != 0U)
            {
                ctx->next_song_request = 0U;
                break;
            }

            if (ctx->run_state == AUDIO_TASK_STATE_ERROR)
            {
                break;
            }

            block_index = AudioApp_ReserveEmptyBlock(ctx);
            if (block_index < 0)
            {
                (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
                continue;
            }

            decode_result = AudioDecoder_DecodeBlock(ctx, (uint8_t)block_index);
            if (decode_result > 0)
            {
                if (ctx->play_task_handle != NULL)
                {
                    (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_DATA_READY, eSetBits);
                }
                continue;
            }

            AudioApp_ReleaseBlock(ctx, (uint8_t)block_index);

            if (decode_result < 0)
            {
                break;
            }

            if ((ctx->eof != 0U) && (ctx->stream_bytes <= 0))
            {
                ctx->decode_done = 1U;
                break;
            }
        }

        AudioApp_CloseFile(ctx);

        if (ctx->run_state == AUDIO_TASK_STATE_ERROR)
        {
            printf("[DEC] decode error, fres=%d\r\n", (int)ctx->last_fres);
            if (ctx->play_task_handle != NULL)
            {
                (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
            }
            for (;;)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        if (ctx->next_song_request != 0U)
        {
            ctx->next_song_request = 0U;
            AudioApp_StopDma(ctx);
            AudioApp_ResetPipeline(ctx);
            fres = AudioApp_FindNextMp3(ctx, AUDIO_FILE_SCAN_PATH);
            if (fres != FR_OK)
            {
                ctx->last_fres = fres;
                ctx->run_state = AUDIO_TASK_STATE_ERROR;
            }
            continue;
        }

        ctx->decode_done = 1U;
        printf("[DEC] done, frames=%lu, bytes=%lu, skip=%lu\r\n",
               (unsigned long)ctx->decoded_frame_count,
               (unsigned long)ctx->sd_read_bytes,
               (unsigned long)ctx->decode_skip_count);
        if (ctx->play_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_DECODE_DONE, eSetBits);
        }

        for (;;)
        {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            if (ctx->next_song_request != 0U)
            {
                ctx->next_song_request = 0U;
                AudioApp_StopDma(ctx);
                AudioApp_ResetPipeline(ctx);
                fres = AudioApp_FindNextMp3(ctx, AUDIO_FILE_SCAN_PATH);
                if (fres != FR_OK)
                {
                    ctx->last_fres = fres;
                    ctx->run_state = AUDIO_TASK_STATE_ERROR;
                }
                break;
            }
        }
    }
}
