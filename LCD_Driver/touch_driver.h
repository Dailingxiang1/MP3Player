#ifndef __TOUCH_DRIVER__H
#define __TOUCH_DRIVER__H

#include <stdint.h>

typedef struct {
    uint8_t ready;
    uint8_t chip_id;
    uint8_t last_status;
    uint8_t last_points;
    int16_t last_x;
    int16_t last_y;
    uint32_t read_ok_count;
    uint32_t read_fail_count;
} ft6336u_debug_t;

void FT6336U_Init(void);
uint8_t FT6336U_Scan(int16_t *x, int16_t *y);
void FT6336U_GetDebug(ft6336u_debug_t *debug);

#endif

