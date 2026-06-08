# MP3Player-RTOS

这是 `MP3Player-RTOS` 分支的专用 README。该分支基于 STM32F407VETx MP3 播放器工程，重点是把原有播放器迁移到 FreeRTOS 环境下，并继续保留已经验证过的 `MyApp/audio_player.c` 播放链路作为主线。

> 如果你正在看 main 分支说明，请注意：本分支不是裸机主循环版本，而是 RTOS 迁移版本。

## 当前分支定位

- MCU: `STM32F407VETx`
- IDE: `Keil MDK-ARM`
- 工程文件: `MDK-ARM/AD_UART.uvprojx`
- UI: `LVGL` + LCD + FT6336U 触摸
- 文件系统: `FatFs` + SD 卡
- 音频链路: `MyApp/audio_player.*` 为当前主播放引擎
- 调试辅助: 已加入 `SEGGER RTT`

## 当前目标

- 让成熟的 `MyApp` 播放器逻辑运行在 FreeRTOS 任务中
- 恢复并维护 LVGL/LCD/touch UI
- 保持 MP3/WAV 本地 SD 卡播放稳定
- 记录 RTOS 迁移过程中的坑，避免后续重复踩坑
- 将 `SRC/audio_*` 中的 RTOS 实验链路保留为参考，而不是当前主播放路径

## 分支和 main 的主要区别

- `main()` 不再直接调用 `app_main()`
- `Core/Src/freertos.c` 中创建 `AppMainTask`，任务里再调用 `app_main()`
- FreeRTOS 相关源码、配置和 Keil 工程分组已接入
- LVGL tick 在应用任务中基于 `HAL_GetTick()` 手动补偿
- SD 卡 FatFs 层当前使用 polling 读卡路径，避免 DMA/queue 版模板带来的音频抖动
- 内存分区需要同时考虑 FreeRTOS、LVGL、FatFs、SDIO DMA、I2S DMA

## 目录说明

- `Core/`: STM32CubeMX 生成的启动、外设和 FreeRTOS 入口代码
- `Drivers/`: STM32 HAL/CMSIS 驱动
- `FATFS/`: FatFs 配置和 SD 卡磁盘接口
- `LCD_Driver/`: LCD 和触摸驱动
- `lvgl/`: LVGL 源码、配置和移植层
- `MDK-ARM/`: Keil 工程文件、MP3 解码库和部分工程辅助文件
- `Middlewares/`: FreeRTOS、FatFs 等中间件
- `MyApp/`: 当前主应用逻辑、播放器、UI 适配和应用主循环
- `SRC/`: 迁移期间保留的 RTOS 音频实验链路，可用于对照和回归分析
- `SEGGER_RTT_V782/`: SEGGER RTT 调试组件

## 播放器主线

当前建议继续以 `MyApp/audio_player.c` 为主播放引擎：

- 支持 MP3/WAV 文件识别和播放
- 使用 FatFs 从 SD 卡读取本地音频文件
- 使用 I2S/DMA 输出 PCM 数据
- 使用三块 PCM buffer 维持播放节奏
- 由 `MyApp/app_main.c` 负责初始化 UI、播放器和主循环节拍

`SRC/audio_*` 目录中也存在一套 RTOS 化的音频任务拆分实验代码，包含 SD 预取、解码、播放和 LVGL 任务等模块。它目前更适合作为迁移参考，不建议和 `MyApp/audio_player.c` 同时维护为两条正式主链路。

## RTOS 入口

当前入口关系如下：

1. `Core/Src/main.c` 初始化 HAL、时钟、外设和文件系统
2. `MX_FREERTOS_Init()` 创建 RTOS 任务
3. `Core/Src/freertos.c` 创建 `AppMainTask`
4. `AppMainTask` 调用 `app_main()`
5. `MyApp/app_main.c` 运行播放器、UI 和周期性处理逻辑

这种方式可以尽量复用原来裸机版 `app_main()` 的结构，同时把调度权交给 FreeRTOS。

## 迁移注意点

### 1. LVGL tick

当前 `lvgl/lv_conf.h` 中 `LV_TICK_CUSTOM = 0`，因此需要应用层主动调用：

```c
lv_tick_inc(delta_ms);
```

本分支在 `MyApp/app_main.c` 中基于 `HAL_GetTick()` 计算增量，并周期调用 `lv_timer_handler()`。

### 2. FatFs SD 卡读路径

在音频流式播放场景下，`sd_diskio_dma_rtos_template` 的 DMA + 中断 + 消息队列路径可能带来更明显的读卡时延抖动。

当前分支更倾向使用 polling 版 `sd_diskio.c/h`：

- `FATFS.USE_DMA_CODE_SD = 0`
- `BSP_SD_ReadBlocks()`
- 轮询等待读卡完成

这对 MP3 流式解码更容易保持稳定。

### 3. DMA 可访问内存

STM32F407 的 CCMRAM 不能被 DMA 直接访问。下面这些对象不要放进 CCMRAM：

- FatFs 对象
- SDIO DMA 的读写缓冲
- I2S DMA 的 PCM 播放缓冲
- 音频压缩流缓冲 `stream_buf`
- LVGL 中可能被 DMA 或外设间接访问的大块缓存

当前更稳妥的经验分配：

- `CCMRAM`: FreeRTOS heap、纯状态结构、不会被 DMA 访问的数据
- `SRAM1`: FatFs 对象、LVGL 大块内存、PCM DMA buffer
- `SRAM2`: 音频压缩流缓冲优先

### 4. 音频变快问题

如果听感上明显变快，不要只盯 I2S 采样率。比如 `44.117 KHz` 相对 `44.1 KHz` 的误差只有约 `0.26%`，通常不足以解释明显加速。

更应该优先排查：

- PCM block 是否跳播或漏播
- MP3 压缩流补给是否稳定
- 解码输出是否按帧完整送入播放链路
- DMA 完成回调和下一块 buffer 的切换是否连续

### 5. MP3 stream buffer

实测中，较小的 MP3 stream buffer 更容易造成卡顿。当前 `MyApp/audio_player.h` 中主播放器使用：

```c
#define AUDIO_PLAYER_STREAM_BUF_SZ  (8U * 1024U)
```

如果后续继续优化音频稳定性，可以把该参数作为重点观察项之一。

## 构建方式

1. 使用 Keil MDK-ARM 打开 `MDK-ARM/AD_UART.uvprojx`
2. 选择当前目标工程配置
3. 编译并下载到 STM32F407VETx 板卡
4. 准备带 MP3/WAV 文件的 SD 卡
5. 通过 LCD/touch UI 或串口/RTT 日志观察运行状态

## 当前建议维护方向

- 主播放链路继续收敛在 `MyApp/audio_player.*`
- `SRC/audio_*` 只作为 RTOS 任务化实验和问题定位参考
- 修改音频 buffer、内存 section、FatFs SDIO 路径前，先确认是否会影响 DMA 可访问性
- 修改 LVGL 调度时，确认 `lv_tick_inc()` 和 `lv_timer_handler()` 的调用节奏
- 修改 FreeRTOS 优先级、栈大小、heap 分配时，优先检查音频连续性和 UI 响应

## Git 分支说明

本 README 只描述 `MP3Player-RTOS` 分支当前状态。main 分支和其他功能分支如果有不同实现，应分别维护各自 README，避免再复用同一份说明。
