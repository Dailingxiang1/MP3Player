#include "lv_port_indev.h"

#include "lcd_driver.h"
#include "lvgl.h"
#include "touch_driver.h"

#ifndef LV_TOUCH_SWAP_XY
#define LV_TOUCH_SWAP_XY 0
#endif

#ifndef LV_TOUCH_INVERT_X
#define LV_TOUCH_INVERT_X 0
#endif

#ifndef LV_TOUCH_INVERT_Y
#define LV_TOUCH_INVERT_Y 0
#endif

static void touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
static lv_port_touch_debug_t s_touch_debug;

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    FT6336U_Init();

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
}

void lv_port_indev_get_touch_debug(lv_port_touch_debug_t *debug)
{
    ft6336u_debug_t hw_debug;

    if (debug == NULL) {
        return;
    }

    *debug = s_touch_debug;
    FT6336U_GetDebug(&hw_debug);
    debug->touch_ready = hw_debug.ready;
    debug->chip_id = hw_debug.chip_id;
    debug->i2c_status = hw_debug.last_status;
    debug->points = hw_debug.last_points;
    debug->raw_x = hw_debug.last_x;
    debug->raw_y = hw_debug.last_y;
    debug->read_ok_count = hw_debug.read_ok_count;
    debug->read_fail_count = hw_debug.read_fail_count;
}

static void touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static int16_t last_x;
    static int16_t last_y;
    static uint8_t was_pressed;
    int16_t x = 0;
    int16_t y = 0;
    int16_t tx;
    int16_t ty;

    (void)indev_drv;

    if (FT6336U_Scan(&x, &y)) {
#if LV_TOUCH_SWAP_XY
        tx = y;
        ty = x;
#else
        tx = x;
        ty = y;
#endif

#if LV_TOUCH_INVERT_X
        tx = (int16_t)(LCD_WIDTH - 1U - tx);
#endif

#if LV_TOUCH_INVERT_Y
        ty = (int16_t)(LCD_HEIGHT - 1U - ty);
#endif

        if (tx < 0) {
            tx = 0;
        } else if (tx >= (int16_t)LCD_WIDTH) {
            tx = (int16_t)LCD_WIDTH - 1;
        }

        if (ty < 0) {
            ty = 0;
        } else if (ty >= (int16_t)LCD_HEIGHT) {
            ty = (int16_t)LCD_HEIGHT - 1;
        }

        last_x = tx;
        last_y = ty;
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_PR;

        s_touch_debug.pressed = 1U;
        s_touch_debug.raw_x = x;
        s_touch_debug.raw_y = y;
        s_touch_debug.x = last_x;
        s_touch_debug.y = last_y;
        if (!was_pressed) {
            s_touch_debug.press_count++;
            was_pressed = 1U;
        }
    } else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;

        s_touch_debug.pressed = 0U;
        s_touch_debug.x = last_x;
        s_touch_debug.y = last_y;
        if (was_pressed) {
            s_touch_debug.release_count++;
            was_pressed = 0U;
        }
    }
}
