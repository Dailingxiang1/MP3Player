# MP3Player-RTOS

STM32F407VETx based MP3 player project with FreeRTOS, LVGL, LCD/touch and local SD card playback.

This branch is used to continue the RTOS migration while keeping the previously verified `MyApp/audio_player.c` playback engine as the main audio path.

## Current branch goal

- Keep the mature `MyApp` audio engine as the primary playback pipeline
- Run the application main loop inside a FreeRTOS task
- Gradually restore the LVGL/LCD/touch UI that had been excluded from build
- Record RTOS migration pitfalls to avoid repeating the same issues

## Features

- MP3 and WAV playback through `MyApp/audio_player.*`
- LVGL based player UI (`MyApp/player_ui.*`)
- LCD display and FT6336U touch input
- FATFS based SD card local playback
- CCMRAM / SRAM1 / SRAM2 memory partitioning

## Environment

- MCU: `STM32F407VETx`
- IDE: `Keil MDK-ARM`
- Project file: `MDK-ARM/AD_UART.uvprojx`

## Directory overview

- `MyApp/`: mature application logic, audio engine and player UI
- `LCD_Driver/`: LCD and touch drivers
- `lvgl/`: LVGL library and ports
- `Core/`: CubeMX generated core code
- `FATFS/`: FATFS integration
- `MDK-ARM/`: Keil project files
- `SRC/`: earlier RTOS audio baseline / experiments, retained for reference

## RTOS migration notes / pitfalls

### 1. “音频明显变快”不一定是 I2S 频率本身

- `44.117 KHz` 相对 `44.1 KHz` 的误差只有约 `0.26%`
- 这个误差不足以解释“主观上快很多”
- 如果听起来明显变快，通常更像：
  - PCM 块衔接有跳播/漏播
  - 压缩流补给不稳定
  - 某些帧没有及时送到播放链路

### 2. RTOS + FatFs DMA/queue 模板会显著影响音频稳定性

在本项目中，最明显的差异不是 FreeRTOS 本身，而是 `FATFS/Target/sd_diskio.c`：

- **差的版本**：`sd_diskio_dma_rtos_template`
  - `BSP_SD_ReadBlocks_DMA()`
  - 中断 + 消息队列 + 任务唤醒
  - 读卡返回时延更抖
- **好的版本**：`sd_diskio_template`
  - `BSP_SD_ReadBlocks()`
  - 轮询等待完成
  - 对 MP3 流式解码更稳定

当前分支已经把 `FATFS.USE_DMA_CODE_SD` 改回 `0`，并切回 polling 版 `sd_diskio.c/h`。

### 3. DMA 可访问内存一定要分清

以下对象 **不能放在 CCMRAM**：

- `FATFS` 对象
- 音频压缩流缓冲 `stream_buf`
- PCM DMA 播放缓冲
- 任何会作为 SDIO / I2S DMA 源或目的地址的内存

原因：

- `CCMRAM` 不能被 DMA 直接访问
- 放错后会出现：
  - 挂载异常
  - 读文件偶发错误
  - 炸音 / 卡顿 / 音质明显变差

当前经验分配：

- `CCMRAM`: FreeRTOS heap、纯状态控制结构
- `SRAM2`: MP3 压缩流缓冲优先
- `SRAM1`: FATFS 对象、LVGL 大块内存、PCM DMA 缓冲

### 4. 8KB MP3 流缓冲比 16KB 更容易卡

已经实测：

- `16KB`：相对更稳
- `8KB`：更容易卡顿

说明：

- MP3 流缓冲大小主要影响“稳不稳”
- 它不是“整体明显加速”的主因

### 5. 不要同时维护两套主播放链路

迁移期间做过一套 `SRC/audio_*` 的 RTOS 基线链路，用于定位问题。

结论：

- `SRC/audio_*` 适合作为试验和回归参考
- 当前实际应继续以 `MyApp/audio_player.c` 这套成熟播放引擎为主

### 6. LVGL 在 RTOS 中运行时要自己补 tick

本工程 `lvgl/lv_conf.h` 中：

- `LV_TICK_CUSTOM = 0`

所以必须手动调用：

- `lv_tick_inc(...)`

当前做法：

- 在 `MyApp/app_main.c` 中基于 `HAL_GetTick()` 的增量补 LVGL tick
- 然后再周期调用 `lv_timer_handler()`

### 7. 当前 FreeRTOS 入口方式

当前不是在 `main()` 里直接跑 `app_main()`，而是：

- 在 `Core/Src/freertos.c` 中创建 `AppMainTask`
- 再在任务中调用 `app_main()`

这样可以尽量复用成熟的 MyApp 主循环，同时逐步把 UI / RTOS 结构收拢起来。

## Known-good direction at this point

At the time of this snapshot, the most reliable direction is:

- FreeRTOS enabled
- `MyApp/audio_player.c` used as the main audio engine
- FATFS SD disk layer switched back to polling version
- LVGL/LCD_Driver/MyApp groups re-enabled

## Notes

- Build output and user-specific Keil files are ignored by `.gitignore`.
- This branch is specifically for the RTOS migration line.
