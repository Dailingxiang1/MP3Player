#include "audio_shared.h"

#include <stdio.h>

/**
 * @brief 尽量往 MP3 压缩流 ring buffer 里补更多数据。
 *
 * 设计重点：
 * 1. SD 任务是唯一调用 f_read() 的任务；
 * 2. 读卡前只短暂持锁，计算当前可写位置；
 * 3. 真正阻塞的 f_read() 不放在锁内，避免解码任务被白白卡住。
 */
static FRESULT AudioSd_RefillStream(audio_context_t *ctx)
{
    FRESULT fres;
    UINT bytes_read;
    uint32_t write_index;
    uint32_t write_count;

    if ((ctx->file_open == 0U) || (ctx->stream_eof != 0U))
    {
        return FR_OK;
    }

    if (xSemaphoreTake(ctx->stream_mutex, portMAX_DELAY) != pdTRUE)
    {
        return FR_INT_ERR;
    }

    write_index = ctx->stream_write_index;
    write_count = AudioApp_StreamGetContiguousWrite(ctx);

    /*
     * ????? FatFs + f_read() ? polling ???f_read() ????????
     * ??????? 512 ?????
     *
     * ?????????????????? < 512 ????? write_count ?? 0?
     * ?? SD ??????????? ring buffer ??????????
     * ???????stream=0B, state=BUFFER?
     */
    if (write_count > AUDIO_STREAM_READ_CHUNK)
    {
        write_count = AUDIO_STREAM_READ_CHUNK;
    }

    (void)xSemaphoreGive(ctx->stream_mutex);

    if (write_count == 0U)
    {
        return FR_OK;
    }

    bytes_read = 0U;

    /*
     * ????? polling ? SD ?????
     * HAL_SD_ReadBlocks() ???????? CPU ??????? SDIO FIFO?
     * ??????? HAL_SD_ERROR_RX_OVERRUN (0x20)?
     *
     * ???????????????? FreeRTOS ???? SD ????????
     * f_read() ????????/UI ??????????? FIFO ??????
     *
     * ???????????????????????????
     * ?????????? I2S DMA / SysTick ???????
     * ???????????????????
     */
    vTaskSuspendAll();
    fres = f_read(ctx->file,
                  &AudioApp_GetStreamBuffer()[write_index],
                  (UINT)write_count,
                  &bytes_read);
    (void)xTaskResumeAll();
    if (fres != FR_OK)
    {
        return fres;
    }

    if (xSemaphoreTake(ctx->stream_mutex, portMAX_DELAY) != pdTRUE)
    {
        return FR_INT_ERR;
    }

    AudioApp_StreamCommitWrite(ctx, (uint32_t)bytes_read);
    ctx->sd_read_bytes += (uint32_t)bytes_read;
    ctx->sd_refill_count++;
    if (bytes_read == 0U)
    {
        ctx->stream_eof = 1U;
    }

    (void)xSemaphoreGive(ctx->stream_mutex);
    return FR_OK;
}

/**
 * @brief 打开当前选中的歌曲，并把流缓冲预热到一个较高水位。
 */
static FRESULT AudioSd_OpenCurrentSong(audio_context_t *ctx)
{
    FRESULT fres;

    printf("[SD] open %s\r\n", ctx->file_path);

    fres = f_open(ctx->file, ctx->file_path, FA_READ);
    if (fres != FR_OK)
    {
        printf("[SD] f_open failed: %d\r\n", (int)fres);
        return fres;
    }

    ctx->file_open = 1U;
    ctx->song_generation++;
    ctx->state = AUDIO_STATE_IDLE;

    while ((AudioApp_StreamGetLevel(ctx) < AUDIO_STREAM_PREFILL_TARGET) && (ctx->stream_eof == 0U))
    {
        fres = AudioSd_RefillStream(ctx);
        if (fres != FR_OK)
        {
            printf("[SD] initial refill failed: %d\r\n", (int)fres);
            return fres;
        }
        if (ctx->stream_eof != 0U)
        {
            break;
        }
    }

    return FR_OK;
}

static FRESULT AudioSd_SwitchToNextSong(audio_context_t *ctx)
{
    FRESULT fres;
    int8_t requested_step;
    uint16_t previous_index;

    /* 切歌时先做一次“硬刷新”：
     * 1. 停 DMA
     * 2. 清 PCM block
     * 3. 清压缩流 ring buffer
     * 4. 清解码/播放任务残留通知
     *
     * 这样可以最大限度避免旧歌尾巴、旧通知、旧缓冲混入新歌。
     */
    requested_step = ctx->requested_track_step;
    previous_index = ctx->current_song_index;
    printf("[SD] sw1 flush start step=%d idx=%u/%u file=%s\r\n",
           (int)requested_step,
           (unsigned int)(previous_index + 1U),
           (unsigned int)ctx->playlist_count,
           ctx->file_path);
    AudioApp_FlushForSongSwitch(ctx);
    printf("[SD] sw2 flush done stream=%lu busy=%u ready=%u\r\n",
           (unsigned long)AudioApp_StreamGetLevel(ctx),
           (unsigned int)AudioApp_PcmBusyCount(ctx),
           (unsigned int)AudioApp_PcmReadyCount(ctx));

    fres = AudioApp_CloseFile(ctx);
    printf("[SD] sw3 close old result=%d\r\n", (int)fres);
    if (fres != FR_OK)
    {
        return fres;
    }

    if (requested_step < 0)
    {
        fres = AudioApp_FindPreviousMp3(ctx, AUDIO_SCAN_PATH);
    }
    else if (ctx->file_path[0] == '\0')
    {
        fres = AudioApp_FindFirstMp3(ctx, AUDIO_SCAN_PATH);
    }
    else
    {
        fres = AudioApp_FindNextMp3(ctx, AUDIO_SCAN_PATH);
    }

    ctx->requested_track_step = 0;
    printf("[SD] sw4 select result=%d new_idx=%u/%u path=%s\r\n",
           (int)fres,
           (unsigned int)(ctx->current_song_index + 1U),
           (unsigned int)ctx->playlist_count,
           ctx->file_path);

    if (fres != FR_OK)
    {
        return fres;
    }

    printf("[SD] sw5 open new start\r\n");
    fres = AudioSd_OpenCurrentSong(ctx);
    printf("[SD] sw6 open new result=%d stream=%lu gen=%lu\r\n",
           (int)fres,
           (unsigned long)AudioApp_StreamGetLevel(ctx),
           (unsigned long)ctx->song_generation);
    return fres;
}


void AudioSdTask(void *argument)
{
    audio_context_t *ctx;
    FRESULT fres;
    uint32_t notify_value;

    (void)argument;
    ctx = AudioApp_GetContext();

    printf("[SD] mount sd...\r\n");
    /*
     * 这里改成和之前“能工作的成熟播放器版本”一致的挂载方式：
     *     f_mount(&fs, "", 1);
     *
     * 虽然 SDPath/“0:” 理论上也能用，但为了减少和已验证版本之间的差异，
     * 当前 polling 路径统一使用空字符串作为逻辑盘根。
     */
    fres = f_mount(ctx->fs, "", 1);
    if (fres != FR_OK)
    {
        ctx->last_fres = fres;
        ctx->state = AUDIO_STATE_ERROR;
        printf("[SD] f_mount failed: %d\r\n", (int)fres);
        if (ctx->play_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
        }
        vTaskDelete(NULL);
    }

    ctx->fs_mounted = 1U;

    fres = AudioApp_FindFirstMp3(ctx, AUDIO_SCAN_PATH);
    if (fres != FR_OK)
    {
        ctx->last_fres = fres;
        ctx->state = AUDIO_STATE_ERROR;
        printf("[SD] no mp3 file found: %d\r\n", (int)fres);
        if (ctx->play_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
        }
        vTaskDelete(NULL);
    }

    (void)AudioApp_CloseFile(ctx);
    AudioApp_ResetSongState(ctx);
    fres = AudioSd_OpenCurrentSong(ctx);
    if (fres != FR_OK)
    {
        ctx->last_fres = fres;
        ctx->state = AUDIO_STATE_ERROR;
        printf("[SD] open song failed: %d\r\n", (int)fres);
        if (ctx->play_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
        }
        vTaskDelete(NULL);
    }

    if (ctx->decode_task_handle != NULL)
    {
        (void)xTaskNotify(ctx->decode_task_handle, 1UL, eSetBits);
        printf("[SD] init notify decode\r\n");
    }

    for (;;)
    {
        notify_value = 0U;
        (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_value, portMAX_DELAY);

        if (((notify_value & AUDIO_SD_NOTIFY_NEXT_SONG) != 0U) &&
            (ctx->requested_track_step != 0))
        {
            printf("[SD] switch request step=%d index=%u/%u\r\n",
                   (int)ctx->requested_track_step,
                   (unsigned int)(ctx->current_song_index + 1U),
                   (unsigned int)ctx->playlist_count);
            fres = AudioSd_SwitchToNextSong(ctx);
            if (fres != FR_OK)
            {
                ctx->last_fres = fres;
                ctx->state = AUDIO_STATE_ERROR;
                printf("[SD] switch song failed: %d\r\n", (int)fres);
                if (ctx->play_task_handle != NULL)
                {
                    (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
                }
                vTaskDelete(NULL);
            }

            if (ctx->decode_task_handle != NULL)
            {
                (void)xTaskNotify(ctx->decode_task_handle, 1UL, eSetBits);
                printf("[SD] sw7 notify decode\r\n");
            }
            continue;
        }

        if (((notify_value & AUDIO_SD_NOTIFY_REFILL) != 0U) &&
            (ctx->file_open != 0U) &&
            (ctx->stream_eof == 0U))
        {
            while ((ctx->requested_track_step == 0) &&
                   (ctx->file_open != 0U) &&
                   (ctx->stream_eof == 0U) &&
                   (AudioApp_StreamGetLevel(ctx) < AUDIO_STREAM_PREFILL_TARGET))
            {
                fres = AudioSd_RefillStream(ctx);
                if (fres != FR_OK)
                {
                    ctx->last_fres = fres;
                    ctx->state = AUDIO_STATE_ERROR;
                    printf("[SD] refill failed: %d\r\n", (int)fres);
                    if (ctx->play_task_handle != NULL)
                    {
                        (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_ERROR, eSetBits);
                    }
                    vTaskDelete(NULL);
                }

                if (ctx->decode_task_handle != NULL)
                {
                    (void)xTaskNotify(ctx->decode_task_handle, 1UL, eSetBits);
                }
            }
        }
    }
}
