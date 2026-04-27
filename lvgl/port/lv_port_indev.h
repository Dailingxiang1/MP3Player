#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pressed;
    uint8_t touch_ready;
    uint8_t chip_id;
    uint8_t i2c_status;
    uint8_t points;
    int16_t raw_x;
    int16_t raw_y;
    int16_t x;
    int16_t y;
    uint32_t press_count;
    uint32_t release_count;
    uint32_t read_ok_count;
    uint32_t read_fail_count;
} lv_port_touch_debug_t;

void lv_port_indev_init(void);
void lv_port_indev_get_touch_debug(lv_port_touch_debug_t *debug);

#ifdef __cplusplus
}
#endif

#endif
