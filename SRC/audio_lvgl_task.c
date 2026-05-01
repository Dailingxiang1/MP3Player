#include "audio_shared.h"

#include "audio_player.h"
#include "player_ui.h"

#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define AUDIO_LVGL_HANDLER_PERIOD_MS   20U

static audio_player_t s_ui_player;

static audio_player_state_t AudioLvgl_MapState(const audio_context_t *ctx)
{
    if (ctx->state == AUDIO_STATE_ERROR)
    {
        return AUDIO_PLAYER_ERROR;
    }
    if (ctx->state == AUDIO_STATE_PAUSE)
    {
        return AUDIO_PLAYER_PAUSED;
    }
    if (ctx->state == AUDIO_STATE_PLAY)
    {
        return AUDIO_PLAYER_PLAYING;
    }
    return AUDIO_PLAYER_STOPPED;
}

static void AudioLvgl_UpdateUiSnapshot(const audio_context_t *ctx)
{
    memset(&s_ui_player, 0, sizeof(s_ui_player));
    s_ui_player.format = AUDIO_FORMAT_MP3;
    s_ui_player.state = AudioLvgl_MapState(ctx);
    s_ui_player.channels = ctx->current_channels;
    s_ui_player.sample_rate = ctx->current_sample_rate;
    s_ui_player.playlist_len = ctx->playlist_count;
    s_ui_player.current_index = ctx->current_song_index;
}

static void AudioLvgl_HandleUiCommand(audio_context_t *ctx)
{
    player_ui_cmd_t cmd;

    cmd = PlayerUi_TakeCommand();

    switch (cmd)
    {
    case PLAYER_UI_CMD_PREVIOUS:
        printf("[LVGL] cmd prev\r\n");
        ctx->requested_track_step = -1;
        if (ctx->sd_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->sd_task_handle,
                              AUDIO_SD_NOTIFY_NEXT_SONG,
                              eSetBits);
        }
        break;

    case PLAYER_UI_CMD_TOGGLE_PAUSE:
        printf("[LVGL] cmd pause\r\n");
        ctx->toggle_pause_request = 1U;
        if (ctx->play_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->play_task_handle,
                              AUDIO_PLAY_NOTIFY_PAUSE_TOGGLE,
                              eSetBits);
        }
        break;

    case PLAYER_UI_CMD_NEXT:
        printf("[LVGL] cmd next\r\n");
        ctx->requested_track_step = 1;
        if (ctx->sd_task_handle != NULL)
        {
            (void)xTaskNotify(ctx->sd_task_handle,
                              AUDIO_SD_NOTIFY_NEXT_SONG,
                              eSetBits);
        }
        break;

    case PLAYER_UI_CMD_NONE:
    default:
        break;
    }
}

void AudioLvglTask(void *argument)
{
    audio_context_t *ctx;
    uint32_t last_tick_ms;
    uint32_t now_tick_ms;
    uint32_t delta_ms;

    (void)argument;
    ctx = AudioApp_GetContext();

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    AudioLvgl_UpdateUiSnapshot(ctx);
    PlayerUi_Init(&s_ui_player);

    printf("[LVGL] original player ui task started\r\n");

    last_tick_ms = HAL_GetTick();

    for (;;)
    {
        now_tick_ms = HAL_GetTick();
        delta_ms = now_tick_ms - last_tick_ms;
        if (delta_ms != 0U)
        {
            lv_tick_inc(delta_ms);
            last_tick_ms = now_tick_ms;
        }

        AudioLvgl_UpdateUiSnapshot(ctx);
        PlayerUi_Update(&s_ui_player);
        AudioLvgl_HandleUiCommand(ctx);
        (void)lv_timer_handler();

        if (ctx->state == AUDIO_STATE_ERROR)
        {
            char text[64];
            snprintf(text, sizeof(text), "Audio error: %d", (int)ctx->last_fres);
            PlayerUi_SetStatus(text);
        }
        else
        {
            PlayerUi_SetStatus("");
        }

        vTaskDelay(pdMS_TO_TICKS(AUDIO_LVGL_HANDLER_PERIOD_MS));
    }
}
