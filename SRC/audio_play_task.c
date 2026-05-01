#include "audio_shared.h"

#include <stdio.h>

static HAL_StatusTypeDef AudioPlay_StartBlockFromTask(audio_context_t *ctx, uint8_t block_index)
{
    HAL_StatusTypeDef status;

    taskENTER_CRITICAL();
    if ((ctx->dma_idle == 0U) ||
        (ctx->pcm_state[block_index] != AUDIO_PCM_READY) ||
        (ctx->pcm_sample_count[block_index] == 0U))
    {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }

    ctx->pcm_state[block_index] = AUDIO_PCM_PLAYING;
    ctx->dma_active_block = block_index;
    ctx->dma_idle = 0U;
    taskEXIT_CRITICAL();

    status = HAL_I2S_Transmit_DMA(ctx->i2s,
                                  (uint16_t *)AudioApp_GetPcmBlock(block_index),
                                  ctx->pcm_sample_count[block_index]);
    if (status != HAL_OK)
    {
        taskENTER_CRITICAL();
        ctx->pcm_state[block_index] = AUDIO_PCM_READY;
        ctx->dma_idle = 1U;
        taskEXIT_CRITICAL();
    }

    return status;
}

static HAL_StatusTypeDef AudioPlay_StartBlockFromIsr(audio_context_t *ctx, uint8_t block_index)
{
    HAL_StatusTypeDef status;

    if ((ctx->pcm_state[block_index] != AUDIO_PCM_READY) ||
        (ctx->pcm_sample_count[block_index] == 0U))
    {
        return HAL_BUSY;
    }

    ctx->pcm_state[block_index] = AUDIO_PCM_PLAYING;
    ctx->dma_active_block = block_index;
    ctx->dma_idle = 0U;

    status = HAL_I2S_Transmit_DMA(ctx->i2s,
                                  (uint16_t *)AudioApp_GetPcmBlock(block_index),
                                  ctx->pcm_sample_count[block_index]);
    if (status != HAL_OK)
    {
        ctx->pcm_state[block_index] = AUDIO_PCM_READY;
        ctx->dma_idle = 1U;
    }

    return status;
}

static void AudioPlay_TryStart(audio_context_t *ctx)
{
    uint8_t ready_count;
    int next_block;

    if ((ctx->state == AUDIO_STATE_ERROR) || (ctx->state == AUDIO_STATE_PAUSE))
    {
        return;
    }

    if ((ctx->file_open == 0U) || (ctx->dma_idle == 0U))
    {
        return;
    }

    ready_count = AudioApp_PcmReadyCount(ctx);
    if (ready_count == 0U)
    {
        return;
    }

    if ((ctx->decode_done == 0U) && (ready_count < AUDIO_PLAY_PREFILL_BLOCKS))
    {
        return;
    }

    next_block = AudioApp_FindNextReadyBlock(ctx, ctx->dma_active_block);
    if (next_block < 0)
    {
        return;
    }

    if (AudioPlay_StartBlockFromTask(ctx, (uint8_t)next_block) == HAL_OK)
    {
        ctx->state = AUDIO_STATE_PLAY;
        if ((ctx->decode_done == 0U) && (AudioApp_PcmReadyCount(ctx) <= 1U))
        {
            ctx->low_buffer_count++;
        }
    }
    else
    {
        ctx->state = AUDIO_STATE_ERROR;
    }
}

static void AudioPlay_HandlePauseToggle(audio_context_t *ctx)
{
    if (ctx->toggle_pause_request == 0U)
    {
        return;
    }

    ctx->toggle_pause_request = 0U;

    if (ctx->state == AUDIO_STATE_PAUSE)
    {
        HAL_I2S_DMAResume(ctx->i2s);
        ctx->state = AUDIO_STATE_PLAY;
        return;
    }

    if ((ctx->state == AUDIO_STATE_PLAY) && (ctx->dma_idle == 0U))
    {
        HAL_I2S_DMAPause(ctx->i2s);
        ctx->state = AUDIO_STATE_PAUSE;
    }
}

static void AudioPlay_Report(audio_context_t *ctx)
{
    static TickType_t s_last_report_tick = 0U;
    static uint32_t s_last_dma_total = 0U;
    static uint32_t s_last_underrun_total = 0U;
    TickType_t now_tick;
    UBaseType_t sd_stack_free;
    UBaseType_t dec_stack_free;
    UBaseType_t play_stack_free;
    UBaseType_t ui_stack_free;
    uint32_t dma_delta;
    uint32_t underrun_delta;
    uint8_t ready_count;
    uint8_t busy_count;
    const char *smooth_text;

    now_tick = xTaskGetTickCount();
    if ((now_tick - s_last_report_tick) < pdMS_TO_TICKS(1000))
    {
        return;
    }
    s_last_report_tick = now_tick;

    sd_stack_free = (ctx->sd_task_handle != NULL) ?
                    uxTaskGetStackHighWaterMark(ctx->sd_task_handle) : 0U;
    dec_stack_free = (ctx->decode_task_handle != NULL) ?
                     uxTaskGetStackHighWaterMark(ctx->decode_task_handle) : 0U;
    play_stack_free = uxTaskGetStackHighWaterMark(NULL);
    ui_stack_free = (ctx->lvgl_task_handle != NULL) ?
                    uxTaskGetStackHighWaterMark(ctx->lvgl_task_handle) : 0U;

    dma_delta = ctx->dma_complete_count - s_last_dma_total;
    underrun_delta = ctx->underrun_count - s_last_underrun_total;
    ready_count = AudioApp_PcmReadyCount(ctx);
    busy_count = AudioApp_PcmBusyCount(ctx);

    s_last_dma_total = ctx->dma_complete_count;
    s_last_underrun_total = ctx->underrun_count;

    smooth_text = (underrun_delta == 0U) ? "OK" : "XRUN";

    printf("[PLAY] state=%s smooth=%s file=%s busy=%u/%u ready=%u dma/s=%lu xr_total=%lu "
           "stream=%luB sdrefill=%lu frames=%lu stack(sd/dec/play/ui)=%lu/%lu/%lu/%lu\r\n",
           AudioApp_StateString(ctx->state),
           smooth_text,
           ctx->file_path,
           (unsigned int)busy_count,
           (unsigned int)AUDIO_PCM_BLOCK_COUNT,
           (unsigned int)ready_count,
           (unsigned long)dma_delta,
           (unsigned long)ctx->underrun_count,
           (unsigned long)AudioApp_StreamGetLevel(ctx),
           (unsigned long)ctx->sd_refill_count,
           (unsigned long)ctx->decoded_frame_count,
           (unsigned long)sd_stack_free,
           (unsigned long)dec_stack_free,
           (unsigned long)play_stack_free,
           (unsigned long)ui_stack_free);
}

void AudioPlayTask(void *argument)
{
    audio_context_t *ctx;
    uint32_t notify_value;

    (void)argument;
    ctx = AudioApp_GetContext();

    for (;;)
    {
        notify_value = 0U;
        (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_value, portMAX_DELAY);

        if ((notify_value & AUDIO_PLAY_NOTIFY_ERROR) != 0U)
        {
            ctx->state = AUDIO_STATE_ERROR;
            AudioApp_StopDma(ctx);
            AudioPlay_Report(ctx);
            continue;
        }

        if ((notify_value & AUDIO_PLAY_NOTIFY_STOP) != 0U)
        {
            AudioApp_StopDma(ctx);
            if (ctx->state != AUDIO_STATE_ERROR)
            {
                ctx->state = AUDIO_STATE_IDLE;
            }
        }

        if ((notify_value & AUDIO_PLAY_NOTIFY_PAUSE_TOGGLE) != 0U)
        {
            AudioPlay_HandlePauseToggle(ctx);
        }

        if ((ctx->state != AUDIO_STATE_ERROR) &&
            ((notify_value & AUDIO_PLAY_NOTIFY_PCM_READY) != 0U ||
             (notify_value & AUDIO_PLAY_NOTIFY_DMA_DONE) != 0U ||
             (notify_value & AUDIO_PLAY_NOTIFY_DECODE_DONE) != 0U))
        {
            AudioPlay_TryStart(ctx);

            if ((ctx->decode_done != 0U) &&
                (ctx->dma_idle != 0U) &&
                (AudioApp_PcmReadyCount(ctx) == 0U))
            {
                ctx->state = AUDIO_STATE_IDLE;
            }
        }

        AudioPlay_Report(ctx);
    }
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    audio_context_t *ctx;
    BaseType_t high_task_woken;
    uint8_t done_index;
    int next_block;

    ctx = AudioApp_GetContext();
    high_task_woken = pdFALSE;

    if ((ctx == NULL) || (ctx->i2s != hi2s))
    {
        return;
    }

    done_index = ctx->dma_active_block;
    ctx->dma_complete_count++;
    ctx->pcm_sample_count[done_index] = 0U;
    ctx->pcm_state[done_index] = AUDIO_PCM_EMPTY;

    if (ctx->decode_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(ctx->decode_task_handle, &high_task_woken);
    }

    next_block = AudioApp_FindNextReadyBlock(ctx, done_index);
    if ((ctx->state == AUDIO_STATE_PLAY) && (next_block >= 0))
    {
        if (AudioPlay_StartBlockFromIsr(ctx, (uint8_t)next_block) == HAL_OK)
        {
            if ((ctx->decode_done == 0U) && (AudioApp_PcmReadyCount(ctx) <= 1U))
            {
                ctx->low_buffer_count++;
            }
        }
        else
        {
            ctx->dma_idle = 1U;
            ctx->state = AUDIO_STATE_ERROR;
        }
    }
    else
    {
        ctx->dma_idle = 1U;
        if ((ctx->state == AUDIO_STATE_PLAY) && (ctx->decode_done == 0U))
        {
            ctx->underrun_count++;
            ctx->state = AUDIO_STATE_IDLE;
            if (ctx->sd_task_handle != NULL)
            {
                (void)xTaskNotifyFromISR(ctx->sd_task_handle,
                                         AUDIO_SD_NOTIFY_REFILL,
                                         eSetBits,
                                         &high_task_woken);
            }
        }
        else if (ctx->state != AUDIO_STATE_ERROR)
        {
            ctx->state = AUDIO_STATE_IDLE;
        }
    }

    if (ctx->play_task_handle != NULL)
    {
        (void)xTaskNotifyFromISR(ctx->play_task_handle,
                                 AUDIO_PLAY_NOTIFY_DMA_DONE,
                                 eSetBits,
                                 &high_task_woken);
    }

    portYIELD_FROM_ISR(high_task_woken);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    audio_context_t *ctx;
    BaseType_t high_task_woken;

    ctx = AudioApp_GetContext();
    high_task_woken = pdFALSE;

    if ((ctx == NULL) || (ctx->i2s != hi2s))
    {
        return;
    }

    ctx->state = AUDIO_STATE_ERROR;
    ctx->dma_idle = 1U;

    if (ctx->play_task_handle != NULL)
    {
        (void)xTaskNotifyFromISR(ctx->play_task_handle,
                                 AUDIO_PLAY_NOTIFY_ERROR,
                                 eSetBits,
                                 &high_task_woken);
    }

    if (ctx->decode_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(ctx->decode_task_handle, &high_task_woken);
    }

    portYIELD_FROM_ISR(high_task_woken);
}
