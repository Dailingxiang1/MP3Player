#include "app_main.h"

#include "app_memory.h"
#include "audio_player.h"
#include "fatfs.h"
#include "i2s.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "player_ui.h"
#include "cmsis_os.h"

#include <stdio.h>

/**
 * @brief LVGL 定时器处理周期。
 *
 * 这里并不是“系统 tick”，而是：
 * - 我们多长时间进入一次 lv_timer_handler()
 * - 过快会占用更多 CPU
 * - 过慢会让动画/触摸刷新不够丝滑
 *
 * 当前先保持原来验证过的 50ms 节奏，后续再微调。
 */
#define APP_LVGL_PERIOD_MS        50U

/**
 * @brief 当音频缓冲过低时，是否临时跳过 LVGL 刷新。
 *
 * 目的：
 * - 把 CPU 时间优先让给音频补流/播放
 * - 降低因为 UI 刷新导致的音频抖动风险
 */
#define APP_AUDIO_LOW_LVGL_SKIP   1U

/**
 * @brief 监控打印周期。
 *
 * 定期输出：
 * - 音频缓冲
 * - DMA 完成次数
 * - underrun 次数
 * - 触摸状态
 * - LVGL 内存占用
 */
#define APP_MONITOR_PERIOD_MS     2000U

/**
 * @brief AppMain 在 FreeRTOS 中的让步周期。
 *
 * 说明：
 * - 裸机版 app_main() 是 while(1) 轮询
 * - 放到 RTOS 任务里后，需要显式让出 CPU
 * - 否则这个任务会长期占满时间片，影响系统整体实时性
 */
#define APP_TASK_YIELD_MS         1U

/**
 * @brief FatFs 文件系统对象。
 *
 * 这里必须放在 DMA 可访问的 SRAM 中，不能放在 CCMRAM。
 * 原因：
 * - FatFs 对象内部包含 sector window 缓冲区；
 * - 当前工程的 SDIO 读卡路径使用 DMA RX；
 * - 若把 FATFS 放进 CCMRAM，DMA 无法直接访问，会导致
 *   读文件偶发错误、炸音、卡顿甚至挂载/解码异常。
 */
static APP_SRAM1 FATFS s_fs;

/**
 * @brief 播放器控制对象。
 *
 * 它主要保存状态量、句柄和统计信息，不作为 DMA 直接访问目标，
 * 因此放 CCMRAM 没问题，也能节省普通 SRAM。
 */
static APP_CCMRAM audio_player_t s_player;

/**
 * @brief MP3/WAV 流工作区。
 *
 * 这里包含：
 * - stream_buf：音频压缩数据流缓冲
 * - playlist：文件路径列表
 *
 * 由于 stream_buf 会被 f_read() / SDIO DMA 间接写入，
 * 所以必须放在 DMA 可访问的 SRAM 中。
 *
 * 当前先放到 SRAM2，方便和“音频流缓冲优先使用 SRAM2”的规划保持一致。
 */
static APP_SRAM2 audio_player_workmem_t s_audio_workmem;

/**
 * @brief PCM DMA 缓冲区。
 *
 * I2S DMA 直接从这里搬运采样数据到 MAX98357，因此放到 SRAM1 更合适：
 * - 访问稳定
 * - 空间相对充足
 * - 也符合你一开始的内存布局思路
 */
static APP_SRAM1 audio_player_dma_buffers_t s_audio_dma_buffers;
static volatile uint8_t s_next_song_flag;

volatile uint32_t g_app_phase = APP_PHASE_BOOT;
volatile uint32_t g_app_last_cmd;

static uint32_t s_last_monitor_tick;
static uint32_t s_last_heartbeat_tick;
static uint32_t s_last_lvgl_tick;
static uint32_t s_last_lvgl_tick_base;
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
    uint32_t tick_delta = now_tick - s_last_lvgl_tick_base;

    /**
     * LVGL v8 在 LV_TICK_CUSTOM=0 的配置下，
     * 需要用户显式调用 lv_tick_inc()。
     *
     * 旧裸机工程是在 SysTick 中每 1ms 调一次 lv_tick_inc(1)；
     * 现在我们把 app_main() 放到 FreeRTOS 任务里，不再沿用裸机式 SysTick 逻辑，
     * 所以这里按 HAL_GetTick() 的增量补给 LVGL 时间基准。
     */
    if (tick_delta != 0U) {
        lv_tick_inc(tick_delta);
        s_last_lvgl_tick_base = now_tick;
    }

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

    /**
     * 初始化 LVGL 时间基准。
     *
     * 后续 App_RunLvglIfAllowed() 会根据 HAL_GetTick() 的差值
     * 给 LVGL 补 tick。
     */
    s_last_lvgl_tick_base = HAL_GetTick();

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

        /**
         * 这里必须主动让出 CPU。
         *
         * 原因：
         * - 现在 app_main() 已经不是裸机主循环，而是一个 FreeRTOS 任务
         * - 音频 DMA 回调、文件系统、其它系统任务都需要运行机会
         * - 1ms 让步通常不会影响播放器响应，却能显著降低整机调度压力
         */
        osDelay(APP_TASK_YIELD_MS);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /**
     * 当前先明确保留 PA0 作为“下一首”按键。
     *
     * 之前历史代码里把 PA0 / PA1 都映射成下一首；
     * 这轮按你的要求，先只保证 PA0 行为明确可控。
     */
    if (GPIO_Pin == GPIO_PIN_0) {
        s_next_song_flag = 1;
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);
    }
}
