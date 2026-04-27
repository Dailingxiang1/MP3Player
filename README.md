# MP3Player-PLUS

STM32F407VETx based local audio player project.

This branch extends the original MP3 player project with a touchscreen LCD UI and a reusable audio player layer.

## Features

- MP3 and WAV playback framework through `audio_player`
- LVGL integrated basic player UI
- LCD display and touch input support
- FATFS based local file playback
- CCMRAM/SRAM memory split for audio and UI workload

## Environment

- MCU: `STM32F407VETx`
- IDE: `Keil MDK-ARM`
- Project file: `MDK-ARM/AD_UART.uvprojx`

## Directory Overview

- `MyApp/`: app logic, audio player, UI
- `LCD_Driver/`: LCD and touch drivers
- `lvgl/`: LVGL library and ports
- `Core/`: CubeMX generated core code
- `FATFS/`: FATFS integration
- `MDK-ARM/`: Keil project files

## Notes

- Build output and user-specific Keil files are ignored by `.gitignore`.
- The `main` branch contains the current project source state.
- This `MP3Player-PLUS` branch adds project documentation on top of `main`.
