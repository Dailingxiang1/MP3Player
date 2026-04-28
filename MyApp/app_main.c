#include "app_main.h"

#include "app_memory.h"
#include "audio_player.h"
#include "fatfs.h"
#include "i2s.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "player_ui.h"

#include <stdio.h>

#define APP_LVGL_PERIOD_MS        50U
#define APP_AUDIO_LOW_LVGL_SKIP   1U
#define APP_MONITOR_PERIOD_MS     2000U

static APP_CCMRAM FATFS s_fs;
static APP_CCMRAM audio_player_t s_player;
static APP_CCMRAM audio_player_workmem_t s_audio_workmem;
static APP_SRAM2 audio_player_dma_buffers_t s_audio_dma_buffers;
static volatile uint8_t s_next_song_flag;

volatile uint32_t g_app_phase = APP_PHASE_BOOT;
volatile uint32_t g_app_last_cmd;

static uint32_t s_last_monitor_tick;
static uint32_t s_last_heartbeat_tick;
static uint32_t s_last_lvgl_tick;
static uint32_t s_loop_count;
static uint32_t s_lvgl_skip_count;

static void App_HandleUiCommand(player_ui_cmd_t cmd)
{
    g_app_last_cmd = (uint32_t)cmd;

    switch (cmd) {
    case PLAYER_UI_CMD_PREVIOUS:
        AudioPlayer_Previous(&s_player);
        break;
    case PLAYER_UI_CMD_TOGGLE_PAUSE:
        AudioPlayer_TogglePause(&s_player);
        break;
    case PLAYER_UI_CMD_NEXT:
        AudioPlayer_Next(&s_player);
        break;
    case PLAYER_UI_CMD_NONE:
    default:
        break;
    }
}

static uint8_t App_AudioLow(void)
{
#if APP_AUDIO_LOW_LVGL_SKIP
    return AudioPlayer_IsBufferLow(&s_player);
#else
    return 0U;
#endif
}

static void App_RunLvglIfAllowed(void)
{
    uint32_t now_tick = HAL_GetTick();

    if ((now_tick - s_last_lvgl_tick) < APP_LVGL_PERIOD_MS) {
        return;
    }

    if (App_AudioLow()) {
        s_lvgl_skip_count++;
        return;
    }

    s_last_lvgl_tick = now_tick;
    g_app_phase = APP_PHASE_LV_TIMER;
    lv_timer_handler();
}

static void App_MonitorUpdate(void)
{
    uint32_t now_tick = HAL_GetTick();
    uint32_t elapsed_tick;
    uint32_t loop_per_s;
    uint32_t underflows;
    uint32_t dma_cplt;
    lv_port_touch_debug_t touch;
    lv_mem_monitor_t mem;

    s_loop_count++;

    if ((now_tick - s_last_heartbeat_tick) >= 500U) {
        s_last_heartbeat_tick = now_tick;
        HAL_GPIO_TogglePin(led_out_GPIO_Port, led_out_Pin);
    }

    if ((now_tick - s_last_monitor_tick) < APP_MONITOR_PERIOD_MS) {
        return;
    }

    if (App_AudioLow()) {
        return;
    }

    elapsed_tick = now_tick - s_last_monitor_tick;
    if (elapsed_tick == 0U) {
        elapsed_tick = 1U;
    }
    loop_per_s = (s_loop_count * 1000U) / elapsed_tick;
    s_last_monitor_tick = now_tick;
    underflows = AudioPlayer_TakeUnderflowCount(&s_player);
    dma_cplt = AudioPlayer_TakeDmaCpltCount(&s_player);
    lv_port_indev_get_touch_debug(&touch);
    lv_mem_monitor(&mem);

    printf("Alive t=%lu loop/s=%lu player=%s buf=%u/%u dma=%lu underflow=%lu "
           "lvskip=%lu touch=%s id=0x%02X st=%u p=%u raw=%d,%d xy=%d,%d ok=%lu fail=%lu "
           "lv_mem=%lu/%lu max=%lu%% frag=%u%%\r\n",
           now_tick,
           loop_per_s,
           AudioPlayer_StateName(s_player.state),
           (unsigned int)AudioPlayer_GetPcmBufferedCount(&s_player),
           (unsigned int)AUDIO_PLAYER_PCM_BUF_COUNT,
           dma_cplt,
           underflows,
           s_lvgl_skip_count,
           touch.touch_ready ? "OK" : "NO",
           touch.chip_id,
           touch.i2c_status,
           touch.pressed,
           (int)touch.raw_x,
           (int)touch.raw_y,
           (int)touch.x,
           (int)touch.y,
           touch.read_ok_count,
           touch.read_fail_count,
           mem.free_size,
           mem.total_size,
           (unsigned long)mem.used_pct,
           (unsigned int)mem.frag_pct);

    s_loop_count = 0;
    s_lvgl_skip_count = 0;
}

int app_main(void)
{
    FRESULT res;

    g_app_phase = APP_PHASE_BOOT;

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    AudioPlayer_Init(&s_player, &hi2s2, &s_audio_workmem, &s_audio_dma_buffers);
    PlayerUi_Init(&s_player);

    res = f_mount(&s_fs, "", 1);
    if (res != FR_OK) {
        printf("SD mount failed: %d\r\n", res);
        PlayerUi_SetStatus("SD mount failed");
    } else {
        res = AudioPlayer_BuildPlaylist(&s_player, "/");
        if (res != FR_OK) {
            printf("Build playlist failed: %d\r\n", res);
            PlayerUi_SetStatus("Playlist scan failed");
        } else if (s_player.playlist_len == 0U) {
            printf("No MP3/WAV files found\r\n");
            PlayerUi_SetStatus("No MP3/WAV files found");
        } else {
            printf("Found %u audio files\r\n", (unsigned int)s_player.playlist_len);
            PlayerUi_SetStatus("");
            AudioPlayer_Start(&s_player);
        }
    }

    while (1) {
        if (s_next_song_flag) {
            s_next_song_flag = 0;
            AudioPlayer_Next(&s_player);
        }

        g_app_phase = APP_PHASE_AUDIO_PROCESS;
        AudioPlayer_Process(&s_player);

        App_RunLvglIfAllowed();

        g_app_phase = APP_PHASE_UI_CMD;
        App_HandleUiCommand(PlayerUi_TakeCommand());

        g_app_phase = APP_PHASE_AUDIO_PROCESS;
        AudioPlayer_Process(&s_player);

        if (!App_AudioLow()) {
            g_app_phase = APP_PHASE_UI_UPDATE;
            PlayerUi_Update(&s_player);
        }

        g_app_phase = APP_PHASE_MONITOR;
        App_MonitorUpdate();

        g_app_phase = APP_PHASE_IDLE;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0 || GPIO_Pin == GPIO_PIN_1) {
        s_next_song_flag = 1;
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);
    }
}
