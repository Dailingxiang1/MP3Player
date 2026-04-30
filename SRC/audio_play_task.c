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
    ctx->active_index = block_index;
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
    ctx->active_index = block_index;
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

    if (ctx->run_state == AUDIO_TASK_STATE_ERROR)
    {
        return;
    }

    if (ctx->dma_idle == 0U)
    {
        return;
    }

    ready_count = AudioApp_PcmReadyCount(ctx);
    if (ready_count == 0U)
    {
        return;
    }

    if ((ctx->decode_done == 0U) && (ready_count < AUDIO_PREFILL_BLOCKS))
    {
        return;
    }

    next_block = AudioApp_FindNextReadyBlock(ctx, ctx->active_index);
    if (next_block < 0)
    {
        return;
    }

    if (AudioPlay_StartBlockFromTask(ctx, (uint8_t)next_block) == HAL_OK)
    {
        ctx->run_state = AUDIO_TASK_STATE_PLAYING;
        if ((ctx->decode_done == 0U) && (AudioApp_PcmReadyCount(ctx) <= 1U))
        {
            ctx->low_buffer_count++;
        }
    }
    else
    {
        ctx->run_state = AUDIO_TASK_STATE_ERROR;
    }
}

static void AudioPlay_CheckFinish(audio_context_t *ctx)
{
    if ((ctx->decode_done != 0U) &&
        (ctx->dma_idle != 0U) &&
        (AudioApp_PcmReadyCount(ctx) == 0U) &&
        (ctx->run_state != AUDIO_TASK_STATE_FINISHED))
    {
        ctx->run_state = AUDIO_TASK_STATE_FINISHED;
        printf("[PLAY] playback finished\r\n");
    }
}

static void AudioPlay_Report(audio_context_t *ctx)
{
    static TickType_t s_last_report_tick = 0U;
    static uint32_t s_last_dma_total = 0U;
    static uint32_t s_last_underrun_total = 0U;
    TickType_t now_tick;
    UBaseType_t dec_stack_free;
    UBaseType_t play_stack_free;
    uint32_t dma_delta;
    uint32_t underrun_delta;
    const char *smooth_text;

    now_tick = xTaskGetTickCount();
    if ((now_tick - s_last_report_tick) < pdMS_TO_TICKS(1000))
    {
        return;
    }
    s_last_report_tick = now_tick;

    dec_stack_free = (ctx->decode_task_handle != NULL) ?
                     uxTaskGetStackHighWaterMark(ctx->decode_task_handle) : 0U;
    play_stack_free = uxTaskGetStackHighWaterMark(NULL);

    dma_delta = ctx->dma_complete_count - s_last_dma_total;
    underrun_delta = ctx->underrun_count - s_last_underrun_total;

    s_last_dma_total = ctx->dma_complete_count;
    s_last_underrun_total = ctx->underrun_count;

    smooth_text = (underrun_delta == 0U) ? "OK" : "XRUN";

    printf("[PLAY] state=%s smooth=%s file=%s hz=%lu ch=%u br=%uk ready=%u/%u "
           "dma/s=%lu xr_total=%lu low=%lu frames=%lu sd=%luB stack(dec/play)=%lu/%lu\r\n",
           AudioApp_StateString(ctx->run_state),
           smooth_text,
           ctx->file_path,
           (unsigned long)ctx->current_sample_rate,
           (unsigned int)ctx->current_channels,
           (unsigned int)ctx->current_bitrate_kbps,
           (unsigned int)AudioApp_PcmReadyCount(ctx),
           (unsigned int)AUDIO_PCM_BLOCK_COUNT,
           (unsigned long)dma_delta,
           (unsigned long)ctx->underrun_count,
           (unsigned long)ctx->low_buffer_count,
           (unsigned long)ctx->decoded_frame_count,
           (unsigned long)ctx->sd_read_bytes,
           (unsigned long)dec_stack_free,
           (unsigned long)play_stack_free);
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
        (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_value, pdMS_TO_TICKS(50));

        if ((notify_value & AUDIO_PLAY_NOTIFY_ERROR) != 0U)
        {
            ctx->run_state = AUDIO_TASK_STATE_ERROR;
        }

        if (ctx->run_state == AUDIO_TASK_STATE_ERROR)
        {
            AudioApp_StopDma(ctx);
        }
        else
        {
            AudioPlay_TryStart(ctx);
            AudioPlay_CheckFinish(ctx);
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

    done_index = ctx->active_index;
    ctx->dma_complete_count++;
    ctx->pcm_sample_count[done_index] = 0U;
    ctx->pcm_state[done_index] = AUDIO_PCM_EMPTY;

    if (ctx->decode_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(ctx->decode_task_handle, &high_task_woken);
    }

    next_block = AudioApp_FindNextReadyBlock(ctx, done_index);
    if (((ctx->run_state == AUDIO_TASK_STATE_PLAYING) ||
         (ctx->run_state == AUDIO_TASK_STATE_BUFFERING)) &&
        (next_block >= 0))
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
            ctx->run_state = AUDIO_TASK_STATE_ERROR;
        }
    }
    else
    {
        ctx->dma_idle = 1U;
        if (((ctx->run_state == AUDIO_TASK_STATE_PLAYING) ||
             (ctx->run_state == AUDIO_TASK_STATE_BUFFERING)) &&
            (ctx->decode_done == 0U))
        {
            ctx->underrun_count++;
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

    ctx->run_state = AUDIO_TASK_STATE_ERROR;
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
