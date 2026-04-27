#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdint.h>

#define APP_PHASE_BOOT          0x00000001U
#define APP_PHASE_LV_TIMER      0x00000002U
#define APP_PHASE_UI_CMD        0x00000003U
#define APP_PHASE_AUDIO_PROCESS 0x00000004U
#define APP_PHASE_UI_UPDATE     0x00000005U
#define APP_PHASE_MONITOR       0x00000006U
#define APP_PHASE_IDLE          0x00000007U

extern volatile uint32_t g_app_phase;
extern volatile uint32_t g_app_last_cmd;

int app_main(void);

#endif
