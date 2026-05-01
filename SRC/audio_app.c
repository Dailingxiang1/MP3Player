#include "audio_app.h"
#include "audio_mem.h"
#include "audio_shared.h"

#include <stdio.h>
#include <string.h>

/*
 * 内存布局说明：
 * 1. 共享控制结构放 CCMRAM，节省普通 SRAM；
 * 2. MP3 压缩流 ring buffer 放 SRAM2，便于和你原本的规划保持一致；
 * 3. 线性化视图缓冲放 CCMRAM，因为它只被 CPU 读取，不给 DMA 访问；
 * 4. PCM block 放 SRAM1，因为 I2S DMA 会直接从这里取数。
 */
static AUDIO_CCMRAM audio_context_t s_audio_ctx;
static AUDIO_CCMRAM char s_audio_playlist[AUDIO_PLAYLIST_MAX_SONGS][AUDIO_FILE_PATH_MAX_LEN];
static AUDIO_SRAM2 uint8_t s_audio_stream_ring[AUDIO_STREAM_BUFFER_SIZE];
static AUDIO_CCMRAM uint8_t s_audio_stream_linear_view[AUDIO_STREAM_LINEAR_VIEW_SIZE];
static AUDIO_SRAM1 int16_t s_audio_pcm_blocks[AUDIO_PCM_BLOCK_COUNT][AUDIO_PCM_BLOCK_SAMPLES];

static int audio_ascii_tolower(int ch)
{
    if ((ch >= 'A') && (ch <= 'Z'))
    {
        ch += ('a' - 'A');
    }
    return ch;
}

static int audio_has_mp3_ext(const char *name)
{
    const char *ext;
    const char *ref;

    ext = strrchr(name, '.');
    if (ext == NULL)
    {
        return 0;
    }

    ref = ".mp3";
    while ((*ext != '\0') && (*ref != '\0'))
    {
        if (audio_ascii_tolower((unsigned char)*ext) != audio_ascii_tolower((unsigned char)*ref))
        {
            return 0;
        }
        ext++;
        ref++;
    }

    return ((*ext == '\0') && (*ref == '\0')) ? 1 : 0;
}

static int audio_build_path(char *dst, uint32_t dst_size, const char *path, const char *name)
{
    int written;

    /*
     * ??????????????????
     *   snprintf("%s/%s", path, name)
     *
     * ? path = "/" ???????? "//file.mp3"?
     * FatFs ??????????????????????????
     * ???????????????????
     */
    written = snprintf(dst, dst_size, "%s/%s", path, name);

    return ((written > 0) && ((uint32_t)written < dst_size)) ? 1 : 0;
}

audio_context_t *AudioApp_GetContext(void)
{
    return &s_audio_ctx;
}

uint8_t *AudioApp_GetStreamBuffer(void)
{
    return s_audio_stream_ring;
}

uint8_t *AudioApp_GetLinearViewBuffer(void)
{
    return s_audio_stream_linear_view;
}

int16_t *AudioApp_GetPcmBlock(uint8_t index)
{
    return s_audio_pcm_blocks[index];
}

void AudioApp_ResetSongState(audio_context_t *ctx)
{
    uint8_t i;

    ctx->current_sample_rate = 0U;
    ctx->current_channels = 0U;
    ctx->current_bitrate_kbps = 0U;

    ctx->stream_read_index = 0U;
    ctx->stream_write_index = 0U;
    ctx->stream_level_bytes = 0U;
    ctx->stream_eof = 0U;

    ctx->decode_done = 0U;
    ctx->state = AUDIO_STATE_IDLE;
    ctx->toggle_pause_request = 0U;

    ctx->decoded_frame_count = 0U;
    ctx->sd_read_bytes = 0U;
    ctx->sd_refill_count = 0U;
    ctx->dma_complete_count = 0U;
    ctx->underrun_count = 0U;
    ctx->low_buffer_count = 0U;
    ctx->decode_skip_count = 0U;

    ctx->last_fres = FR_OK;
    ctx->dma_idle = 1U;
    ctx->dma_active_block = 0U;

    memset(&ctx->frame_info, 0, sizeof(ctx->frame_info));
    memset(&ctx->decoder, 0, sizeof(ctx->decoder));
    memset(s_audio_stream_ring, 0, sizeof(s_audio_stream_ring));
    memset(s_audio_stream_linear_view, 0, sizeof(s_audio_stream_linear_view));

    for (i = 0U; i < AUDIO_PCM_BLOCK_COUNT; i++)
    {
        ctx->pcm_state[i] = AUDIO_PCM_EMPTY;
        ctx->pcm_sample_count[i] = 0U;
    }
}

void AudioApp_ResetPipeline(audio_context_t *ctx)
{
    ctx->fs_mounted = 0U;
    ctx->file_open = 0U;
    ctx->requested_track_step = 0;
    ctx->song_generation = 0U;
    ctx->playlist_count = 0U;
    ctx->current_song_index = 0U;
    memset(ctx->file_path, 0, sizeof(ctx->file_path));
    memset(s_audio_playlist, 0, sizeof(s_audio_playlist));
    AudioApp_ResetSongState(ctx);
}

void AudioApp_FlushForSongSwitch(audio_context_t *ctx)
{
    uint8_t i;

    /**
     * 先停 DMA，防止旧歌的最后一个 PCM block 继续往外送。
     */
    printf("[SD] sw1.1 stop dma start\r\n");
    AudioApp_StopDma(ctx);
    printf("[SD] sw1.2 stop dma done\r\n");

    /**
     * 清掉播放/解码任务上残留的通知状态，
     * 避免切歌后旧通知把新歌流程“误推进”。
     */
    printf("[SD] sw1.3 clear task notify start\r\n");
	
	taskENTER_CRITICAL();
    if (ctx->decode_task_handle != NULL)
    {
        //(void)xTaskAbortDelay(ctx->decode_task_handle);
        (void)xTaskNotifyStateClear(ctx->decode_task_handle);
        (void)ulTaskNotifyValueClear(ctx->decode_task_handle, 0xFFFFFFFFUL);
    }
    if (ctx->play_task_handle != NULL)
    {
        //(void)xTaskAbortDelay(ctx->play_task_handle);
        (void)xTaskNotifyStateClear(ctx->play_task_handle);
        (void)ulTaskNotifyValueClear(ctx->play_task_handle, 0xFFFFFFFFUL);
    }
    if (ctx->sd_task_handle != NULL)
    {
        //(void)xTaskAbortDelay(ctx->sd_task_handle);
        (void)xTaskNotifyStateClear(ctx->sd_task_handle);
        (void)ulTaskNotifyValueClear(ctx->sd_task_handle, 0xFFFFFFFFUL);
    }
	taskEXIT_CRITICAL();
    printf("[SD] sw1.4 clear task notify done\r\n");

    /**
     * generation 自增后，解码任务即使手里还有旧歌的一帧，
     * 也会因为 generation 不一致而主动丢弃。
     */
    ctx->song_generation++;
    ctx->decode_done = 0U;
    ctx->toggle_pause_request = 0U;
    ctx->requested_track_step = 0;
    ctx->state = AUDIO_STATE_IDLE;

    printf("[SD] sw1.5 take stream mutex wait\r\n");
    if (xSemaphoreTake(ctx->stream_mutex, portMAX_DELAY) == pdTRUE)
    {
        printf("[SD] sw1.6 take stream mutex done\r\n");
        ctx->stream_read_index = 0U;
        ctx->stream_write_index = 0U;
        ctx->stream_level_bytes = 0U;
        ctx->stream_eof = 0U;
        memset(s_audio_stream_ring, 0, sizeof(s_audio_stream_ring));
        memset(s_audio_stream_linear_view, 0, sizeof(s_audio_stream_linear_view));
        (void)xSemaphoreGive(ctx->stream_mutex);
        printf("[SD] sw1.7 release stream mutex done\r\n");
    }

    printf("[SD] sw1.8 clear pcm state start\r\n");
    taskENTER_CRITICAL();
	
    ctx->dma_idle = 1U;
    ctx->dma_active_block = 0U;
    for (i = 0U; i < AUDIO_PCM_BLOCK_COUNT; i++)
    {
        ctx->pcm_state[i] = AUDIO_PCM_EMPTY;
        ctx->pcm_sample_count[i] = 0U;
    }
    taskEXIT_CRITICAL();
    printf("[SD] sw1.9 clear pcm state done\r\n");

    if (ctx->play_task_handle != NULL)
    {
        (void)xTaskNotify(ctx->play_task_handle, AUDIO_PLAY_NOTIFY_STOP, eSetBits);
    }
    printf("[SD] sw1.10 notify play stop done\r\n");
}

static FRESULT AudioApp_BuildPlaylist(audio_context_t *ctx, const char *path)
{
    DIR dir;
    FILINFO fno;
    FRESULT fres;
    uint16_t count;

    memset(s_audio_playlist, 0, sizeof(s_audio_playlist));
    memset(ctx->file_path, 0, sizeof(ctx->file_path));
    ctx->playlist_count = 0U;
    ctx->current_song_index = 0U;
    count = 0U;

    fres = f_opendir(&dir, path);
    if (fres != FR_OK)
    {
        return fres;
    }

    for (;;)
    {
        fres = f_readdir(&dir, &fno);
        if (fres != FR_OK)
        {
            break;
        }
        if (fno.fname[0] == '\0')
        {
            break;
        }
        if (((fno.fattrib & AM_DIR) != 0U) || !audio_has_mp3_ext(fno.fname))
        {
            continue;
        }
        if (count < AUDIO_PLAYLIST_MAX_SONGS)
        {
            if (audio_build_path(s_audio_playlist[count],
                                 sizeof(s_audio_playlist[count]),
                                 path,
                                 fno.fname) != 0)
            {
                count++;
            }
        }
    }

    (void)f_closedir(&dir);

    ctx->playlist_count = count;
    if (count == 0U)
    {
        return FR_NO_FILE;
    }

    (void)strncpy(ctx->file_path, s_audio_playlist[0], sizeof(ctx->file_path) - 1U);
    return FR_OK;
}

FRESULT AudioApp_CloseFile(audio_context_t *ctx)
{
    FRESULT fres;

    fres = FR_OK;
    if (ctx->file_open != 0U)
    {
        fres = f_close(ctx->file);
        if (fres == FR_OK)
        {
            ctx->file_open = 0U;
        }
    }

    return fres;
}

void AudioApp_StopDma(audio_context_t *ctx)
{
    uint8_t i;

    ctx->dma_idle = 1U;
    
    if (ctx->i2s != NULL)
    {
        (void)HAL_I2S_DMAStop(ctx->i2s);
    }

    for (i = 0U; i < AUDIO_PCM_BLOCK_COUNT; i++)
    {
        if (ctx->pcm_state[i] == AUDIO_PCM_PLAYING)
        {
            ctx->pcm_state[i] = AUDIO_PCM_EMPTY;
            ctx->pcm_sample_count[i] = 0U;
        }
    }
}

FRESULT AudioApp_FindFirstMp3(audio_context_t *ctx, const char *path)
{
    return AudioApp_BuildPlaylist(ctx, path);
}

FRESULT AudioApp_FindNextMp3(audio_context_t *ctx, const char *path)
{
    (void)path;

    if (ctx->playlist_count == 0U)
    {
        return AudioApp_BuildPlaylist(ctx, AUDIO_SCAN_PATH);
    }

    ctx->current_song_index = (uint16_t)((ctx->current_song_index + 1U) % ctx->playlist_count);
    (void)strncpy(ctx->file_path,
                  s_audio_playlist[ctx->current_song_index],
                  sizeof(ctx->file_path) - 1U);
    return FR_OK;
}

FRESULT AudioApp_FindPreviousMp3(audio_context_t *ctx, const char *path)
{
    (void)path;

    if (ctx->playlist_count == 0U)
    {
        return AudioApp_BuildPlaylist(ctx, AUDIO_SCAN_PATH);
    }

    if (ctx->current_song_index == 0U)
    {
        ctx->current_song_index = (uint16_t)(ctx->playlist_count - 1U);
    }
    else
    {
        ctx->current_song_index--;
    }

    (void)strncpy(ctx->file_path,
                  s_audio_playlist[ctx->current_song_index],
                  sizeof(ctx->file_path) - 1U);
    return FR_OK;
}

uint32_t AudioApp_StreamGetLevel(const audio_context_t *ctx)
{
    return ctx->stream_level_bytes;
}

uint32_t AudioApp_StreamGetFree(const audio_context_t *ctx)
{
    return AUDIO_STREAM_BUFFER_SIZE - ctx->stream_level_bytes;
}

uint32_t AudioApp_StreamGetContiguousRead(const audio_context_t *ctx)
{
    if (ctx->stream_level_bytes == 0U)
    {
        return 0U;
    }

    if (ctx->stream_read_index < ctx->stream_write_index)
    {
        return ctx->stream_write_index - ctx->stream_read_index;
    }

    return AUDIO_STREAM_BUFFER_SIZE - ctx->stream_read_index;
}

uint32_t AudioApp_StreamGetContiguousWrite(const audio_context_t *ctx)
{
    uint32_t free_bytes;

    free_bytes = AudioApp_StreamGetFree(ctx);
    if (free_bytes == 0U)
    {
        return 0U;
    }

    if (ctx->stream_write_index < ctx->stream_read_index)
    {
        return ctx->stream_read_index - ctx->stream_write_index;
    }

    return AUDIO_STREAM_BUFFER_SIZE - ctx->stream_write_index;
}

void AudioApp_StreamConsume(audio_context_t *ctx, uint32_t bytes)
{
    if (bytes > ctx->stream_level_bytes)
    {
        bytes = ctx->stream_level_bytes;
    }

    ctx->stream_read_index = (ctx->stream_read_index + bytes) & AUDIO_STREAM_BUFFER_MASK;
    ctx->stream_level_bytes -= bytes;
}

void AudioApp_StreamCommitWrite(audio_context_t *ctx, uint32_t bytes)
{
    uint32_t free_bytes;

    free_bytes = AudioApp_StreamGetFree(ctx);
    if (bytes > free_bytes)
    {
        bytes = free_bytes;
    }

    ctx->stream_write_index = (ctx->stream_write_index + bytes) & AUDIO_STREAM_BUFFER_MASK;
    ctx->stream_level_bytes += bytes;
}

uint32_t AudioApp_StreamLinearizeForDecode(audio_context_t *ctx)
{
    uint32_t level_bytes;
    uint32_t first_part;
    uint32_t second_part;
    uint32_t linear_bytes;

    level_bytes = AudioApp_StreamGetLevel(ctx);
    if (level_bytes == 0U)
    {
        return 0U;
    }

    first_part = AudioApp_StreamGetContiguousRead(ctx);
    linear_bytes = first_part;
    if (linear_bytes > AUDIO_STREAM_LINEAR_VIEW_SIZE)
    {
        linear_bytes = AUDIO_STREAM_LINEAR_VIEW_SIZE;
    }

    memcpy(s_audio_stream_linear_view,
           &s_audio_stream_ring[ctx->stream_read_index],
           linear_bytes);

    if ((first_part < level_bytes) && (linear_bytes < AUDIO_STREAM_LINEAR_VIEW_SIZE))
    {
        second_part = level_bytes - first_part;
        if (second_part > (AUDIO_STREAM_LINEAR_VIEW_SIZE - linear_bytes))
        {
            second_part = AUDIO_STREAM_LINEAR_VIEW_SIZE - linear_bytes;
        }

        memcpy(&s_audio_stream_linear_view[linear_bytes],
               &s_audio_stream_ring[0],
               second_part);
        linear_bytes += second_part;
    }

    return linear_bytes;
}

uint8_t AudioApp_PcmReadyCount(const audio_context_t *ctx)
{
    uint8_t i;
    uint8_t count;

    count = 0U;
    for (i = 0U; i < AUDIO_PCM_BLOCK_COUNT; i++)
    {
        if ((ctx->pcm_state[i] == AUDIO_PCM_READY) && (ctx->pcm_sample_count[i] != 0U))
        {
            count++;
        }
    }

    return count;
}

uint8_t AudioApp_PcmBusyCount(const audio_context_t *ctx)
{
    uint8_t i;
    uint8_t count;

    count = 0U;
    for (i = 0U; i < AUDIO_PCM_BLOCK_COUNT; i++)
    {
        if ((ctx->pcm_state[i] == AUDIO_PCM_FILLING) ||
            (ctx->pcm_state[i] == AUDIO_PCM_READY) ||
            (ctx->pcm_state[i] == AUDIO_PCM_PLAYING))
        {
            count++;
        }
    }

    return count;
}

int AudioApp_FindNextReadyBlock(const audio_context_t *ctx, uint8_t after_index)
{
    uint8_t n;

    for (n = 1U; n <= AUDIO_PCM_BLOCK_COUNT; n++)
    {
        uint8_t index = (uint8_t)((after_index + n) % AUDIO_PCM_BLOCK_COUNT);

        if ((ctx->pcm_state[index] == AUDIO_PCM_READY) &&
            (ctx->pcm_sample_count[index] != 0U))
        {
            return (int)index;
        }
    }

    return -1;
}

int AudioApp_ReserveEmptyBlock(audio_context_t *ctx)
{
    uint8_t n;
    uint8_t start_index;
    int result;

    result = -1;
    start_index = (ctx->dma_idle != 0U) ? 0U :
                  (uint8_t)((ctx->dma_active_block + 1U) % AUDIO_PCM_BLOCK_COUNT);

    taskENTER_CRITICAL();
    for (n = 0U; n < AUDIO_PCM_BLOCK_COUNT; n++)
    {
        uint8_t index = (uint8_t)((start_index + n) % AUDIO_PCM_BLOCK_COUNT);
        if (ctx->pcm_state[index] == AUDIO_PCM_EMPTY)
        {
            ctx->pcm_state[index] = AUDIO_PCM_FILLING;
            ctx->pcm_sample_count[index] = 0U;
            result = (int)index;
            break;
        }
    }
    taskEXIT_CRITICAL();

    return result;
}

void AudioApp_ReleaseBlock(audio_context_t *ctx, uint8_t index)
{
    taskENTER_CRITICAL();
    ctx->pcm_sample_count[index] = 0U;
    ctx->pcm_state[index] = AUDIO_PCM_EMPTY;
    taskEXIT_CRITICAL();
}

void AudioApp_PublishBlock(audio_context_t *ctx, uint8_t index, uint16_t sample_count)
{
    taskENTER_CRITICAL();
    ctx->pcm_sample_count[index] = sample_count;
    ctx->pcm_state[index] = (sample_count == 0U) ? AUDIO_PCM_EMPTY : AUDIO_PCM_READY;
    taskEXIT_CRITICAL();
}

const char *AudioApp_StateString(audio_state_t state)
{
    switch (state)
    {
    case AUDIO_STATE_PLAY:
        return "PLAY";
    case AUDIO_STATE_PAUSE:
        return "PAUSE";
    case AUDIO_STATE_ERROR:
        return "ERROR";
    case AUDIO_STATE_IDLE:
    default:
        return "IDLE";
    }
}

void AudioApp_CreateTasks(void)
{
    BaseType_t status;
    audio_context_t *ctx;

    ctx = AudioApp_GetContext();
    memset(ctx, 0, sizeof(*ctx));
    ctx->i2s = &hi2s2;
    ctx->fs = &SDFatFS;
    ctx->file = &SDFile;
    ctx->stream_mutex = xSemaphoreCreateMutex();
    if (ctx->stream_mutex == NULL)
    {
        printf("[APP] stream mutex create failed\r\n");
        Error_Handler();
    }

    AudioApp_ResetPipeline(ctx);

    status = xTaskCreate(AudioSdTask,
                         "AudioSD",
                         AUDIO_SD_TASK_STACK_WORDS,
                         NULL,
                         AUDIO_SD_TASK_PRIORITY,
                         &ctx->sd_task_handle);
    if (status != pdPASS)
    {
        printf("[APP] SD task create failed\r\n");
        Error_Handler();
    }

    status = xTaskCreate(AudioDecodeTask,
                         "AudioDec",
                         AUDIO_DECODE_TASK_STACK_WORDS,
                         NULL,
                         AUDIO_DECODE_TASK_PRIORITY,
                         &ctx->decode_task_handle);
    if (status != pdPASS)
    {
        printf("[APP] decode task create failed\r\n");
        Error_Handler();
    }

    status = xTaskCreate(AudioPlayTask,
                         "AudioPlay",
                         AUDIO_PLAY_TASK_STACK_WORDS,
                         NULL,
                         AUDIO_PLAY_TASK_PRIORITY,
                         &ctx->play_task_handle);
    if (status != pdPASS)
    {
        printf("[APP] play task create failed\r\n");
        Error_Handler();
    }

    status = xTaskCreate(AudioLvglTask,
                         "AudioUI",
                         AUDIO_LVGL_TASK_STACK_WORDS,
                         NULL,
                         AUDIO_LVGL_TASK_PRIORITY,
                         &ctx->lvgl_task_handle);
    if (status != pdPASS)
    {
        printf("[APP] LVGL task create failed\r\n");
        Error_Handler();
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t s_last_pa0_tick = 0U;
    audio_context_t *ctx;
    BaseType_t high_task_woken;
    uint32_t now_tick;

    if (GPIO_Pin != GPIO_PIN_0)
    {
        return;
    }

    now_tick = HAL_GetTick();
    if ((now_tick - s_last_pa0_tick) < 200U)
    {
        return;
    }
    s_last_pa0_tick = now_tick;

    ctx = AudioApp_GetContext();
    high_task_woken = pdFALSE;
    ctx->requested_track_step = 1;
    HAL_GPIO_TogglePin(led_out_GPIO_Port, led_out_Pin);

    if (ctx->sd_task_handle != NULL)
    {
        (void)xTaskNotifyFromISR(ctx->sd_task_handle,
                                 AUDIO_SD_NOTIFY_NEXT_SONG,
                                 eSetBits,
                                 &high_task_woken);
    }

    portYIELD_FROM_ISR(high_task_woken);
}
