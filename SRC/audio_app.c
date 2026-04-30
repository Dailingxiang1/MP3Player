#include "audio_app.h"
#include "audio_mem.h"
#include "audio_shared.h"

#include <stdio.h>
#include <string.h>

static AUDIO_CCMRAM audio_context_t s_audio_ctx;
static AUDIO_SRAM2 uint8_t s_audio_stream_buffer[AUDIO_MP3_STREAM_BUFFER_SIZE];
static AUDIO_SRAM1 int16_t s_audio_pcm_blocks[AUDIO_PCM_BLOCK_COUNT][AUDIO_PCM_BLOCK_SAMPLES];

static int AudioApp_ToLower(int ch)
{
    if ((ch >= 'A') && (ch <= 'Z'))
    {
        ch += ('a' - 'A');
    }
    return ch;
}

static int AudioApp_HasMp3Ext(const char *name)
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
        if (AudioApp_ToLower((unsigned char)*ext) != AudioApp_ToLower((unsigned char)*ref))
        {
            return 0;
        }
        ext++;
        ref++;
    }

    return ((*ext == '\0') && (*ref == '\0')) ? 1 : 0;
}

static int AudioApp_BuildPath(char *dst, uint32_t dst_size, const char *path, const char *name)
{
    int written;

    if ((strcmp(path, "/") == 0) || (strcmp(path, "") == 0))
    {
        written = snprintf(dst, dst_size, "/%s", name);
    }
    else
    {
        written = snprintf(dst, dst_size, "%s/%s", path, name);
    }

    return ((written > 0) && ((uint32_t)written < dst_size)) ? 1 : 0;
}

audio_context_t *AudioApp_GetContext(void)
{
    return &s_audio_ctx;
}

uint8_t *AudioApp_GetStreamBuffer(void)
{
    return s_audio_stream_buffer;
}

int16_t *AudioApp_GetPcmBlock(uint8_t index)
{
    return s_audio_pcm_blocks[index];
}

void AudioApp_ResetPipeline(audio_context_t *ctx)
{
    uint8_t i;

    ctx->stream_offset = 0;
    ctx->stream_bytes = 0;
    ctx->eof = 0U;
    ctx->decode_done = 0U;
    ctx->next_song_request = 0U;
    ctx->run_state = AUDIO_TASK_STATE_IDLE;

    ctx->current_sample_rate = 0U;
    ctx->current_channels = 0U;
    ctx->current_bitrate_kbps = 0U;
    ctx->sample_rate_configured = 0U;

    ctx->decoded_frame_count = 0U;
    ctx->sd_read_bytes = 0U;
    ctx->dma_complete_count = 0U;
    ctx->underrun_count = 0U;
    ctx->low_buffer_count = 0U;
    ctx->decode_skip_count = 0U;

    ctx->last_fres = FR_OK;
    ctx->dma_idle = 1U;
    ctx->active_index = 0U;

    memset(&ctx->frame_info, 0, sizeof(ctx->frame_info));
    memset(&ctx->decoder, 0, sizeof(ctx->decoder));

    for (i = 0U; i < AUDIO_PCM_BLOCK_COUNT; i++)
    {
        ctx->pcm_state[i] = AUDIO_PCM_EMPTY;
        ctx->pcm_sample_count[i] = 0U;
    }
}

void AudioApp_CloseFile(audio_context_t *ctx)
{
    if (ctx->file_open != 0U)
    {
        (void)f_close(ctx->file);
        ctx->file_open = 0U;
    }
}

void AudioApp_StopDma(audio_context_t *ctx)
{
    uint8_t i;

    if (ctx->i2s != NULL)
    {
        (void)HAL_I2S_DMAStop(ctx->i2s);
    }

    ctx->dma_idle = 1U;
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
    DIR dir;
    FILINFO fno;
    FRESULT fres;
    int written;

    memset(ctx->file_path, 0, sizeof(ctx->file_path));

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
            fres = FR_NO_FILE;
            break;
        }

        if ((fno.fattrib & AM_DIR) != 0U)
        {
            continue;
        }

        if (!AudioApp_HasMp3Ext(fno.fname))
        {
            continue;
        }

        written = AudioApp_BuildPath(ctx->file_path, sizeof(ctx->file_path), path, fno.fname);

        if (written != 0)
        {
            fres = FR_OK;
            break;
        }

        fres = FR_INVALID_NAME;
        break;
    }

    (void)f_closedir(&dir);
    return fres;
}

FRESULT AudioApp_FindNextMp3(audio_context_t *ctx, const char *path)
{
    DIR dir;
    FILINFO fno;
    FRESULT fres;
    char first_path[AUDIO_FILE_PATH_MAX_LEN];
    char candidate[AUDIO_FILE_PATH_MAX_LEN];
    uint8_t seen_current;

    memset(first_path, 0, sizeof(first_path));
    memset(candidate, 0, sizeof(candidate));
    seen_current = (ctx->file_path[0] == '\0') ? 1U : 0U;

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
            fres = FR_NO_FILE;
            break;
        }

        if (((fno.fattrib & AM_DIR) != 0U) || !AudioApp_HasMp3Ext(fno.fname))
        {
            continue;
        }

        if (AudioApp_BuildPath(candidate, sizeof(candidate), path, fno.fname) == 0)
        {
            continue;
        }

        if (first_path[0] == '\0')
        {
            (void)strncpy(first_path, candidate, sizeof(first_path) - 1U);
        }

        if ((seen_current == 0U) && (strcmp(candidate, ctx->file_path) == 0))
        {
            seen_current = 1U;
            continue;
        }

        if (seen_current != 0U)
        {
            (void)strncpy(ctx->file_path, candidate, sizeof(ctx->file_path) - 1U);
            fres = FR_OK;
            break;
        }
    }

    (void)f_closedir(&dir);

    if ((fres == FR_NO_FILE) && (first_path[0] != '\0'))
    {
        (void)strncpy(ctx->file_path, first_path, sizeof(ctx->file_path) - 1U);
        fres = FR_OK;
    }

    return fres;
}

HAL_StatusTypeDef AudioApp_ReconfigI2S(audio_context_t *ctx, uint32_t sample_rate)
{
    if ((ctx->i2s == NULL) || (sample_rate == 0U))
    {
        return HAL_ERROR;
    }

    if (ctx->i2s->Init.AudioFreq == sample_rate)
    {
        return HAL_OK;
    }

    (void)HAL_I2S_DMAStop(ctx->i2s);

    if (HAL_I2S_DeInit(ctx->i2s) != HAL_OK)
    {
        return HAL_ERROR;
    }

    ctx->i2s->Init.AudioFreq = sample_rate;
    return HAL_I2S_Init(ctx->i2s);
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
                  (uint8_t)((ctx->active_index + 1U) % AUDIO_PCM_BLOCK_COUNT);

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

const char *AudioApp_StateString(audio_task_state_t state)
{
    switch (state)
    {
    case AUDIO_TASK_STATE_BUFFERING:
        return "BUFFER";
    case AUDIO_TASK_STATE_PLAYING:
        return "PLAY";
    case AUDIO_TASK_STATE_FINISHED:
        return "DONE";
    case AUDIO_TASK_STATE_ERROR:
        return "ERROR";
    case AUDIO_TASK_STATE_IDLE:
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
    AudioApp_ResetPipeline(ctx);

    status = xTaskCreate(AudioDecodeTask,
                         "AudioDec",
                         AUDIO_DECODE_TASK_STACK_WORDS,
                         NULL,
                         AUDIO_DECODE_TASK_PRIORITY,
                         &ctx->decode_task_handle);
    if (status != pdPASS)
    {
        printf("Audio decode task create failed\r\n");
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
        printf("Audio play task create failed\r\n");
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
    ctx->next_song_request = 1U;
    HAL_GPIO_TogglePin(led_out_GPIO_Port, led_out_Pin);

    if (ctx->decode_task_handle != NULL)
    {
        (void)xTaskNotifyFromISR(ctx->decode_task_handle, 1UL, eSetBits, &high_task_woken);
    }

    portYIELD_FROM_ISR(high_task_woken);
}
