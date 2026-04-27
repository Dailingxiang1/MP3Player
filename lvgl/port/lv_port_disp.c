/**
 * @file lv_port_disp.c
 *
 */

#if 1

/*********************
 * INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>

#include "app_memory.h"
#include "lcd_driver.h" 



/*********************
 * DEFINES
 *********************/
#ifndef MY_DISP_HOR_RES
    #define MY_DISP_HOR_RES    LCD_WIDTH
#endif

#ifndef MY_DISP_VER_RES
    #define MY_DISP_VER_RES    LCD_HEIGHT
#endif

#define DISP_BUF_HEIGHT    6

/**********************
 * TYPEDEFS
 **********************/

/**********************
 * STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

/**********************
 * STATIC VARIABLES
 **********************/
static APP_CCMRAM lv_disp_draw_buf_t s_draw_buf_dsc;
static APP_CCMRAM lv_color_t s_draw_buf_1[MY_DISP_HOR_RES * DISP_BUF_HEIGHT];
static APP_CCMRAM lv_disp_drv_t s_disp_drv;

/**********************
 * MACROS
 **********************/

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/
    
    /* 初始化绘制缓冲区 */
    lv_disp_draw_buf_init(&s_draw_buf_dsc, s_draw_buf_1, NULL, MY_DISP_HOR_RES * DISP_BUF_HEIGHT);   

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    lv_disp_drv_init(&s_disp_drv);                  /*Basic initialization*/

    /*Set the resolution of the display*/
    s_disp_drv.hor_res = MY_DISP_HOR_RES;
    s_disp_drv.ver_res = MY_DISP_VER_RES;

    /*Used to copy the buffer's content to the display*/
    s_disp_drv.flush_cb = disp_flush;

    /*Set a display buffer*/
    s_disp_drv.draw_buf = &s_draw_buf_dsc;

    /*Finally register the driver*/
    lv_disp_drv_register(&s_disp_drv);
}

/**********************
 * STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    LCD_Init(); 
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    int32_t orig_x1 = area->x1;
    int32_t orig_y1 = area->y1;
    int32_t orig_w = area->x2 - area->x1 + 1;
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    if(disp_flush_enabled) {
        if (x2 >= 0 && y2 >= 0 && x1 < (int32_t)LCD_WIDTH && y1 < (int32_t)LCD_HEIGHT) {
            if (x1 < 0) {
                x1 = 0;
            }
            if (y1 < 0) {
                y1 = 0;
            }
            if (x2 >= (int32_t)LCD_WIDTH) {
                x2 = (int32_t)LCD_WIDTH - 1;
            }
            if (y2 >= (int32_t)LCD_HEIGHT) {
                y2 = (int32_t)LCD_HEIGHT - 1;
            }

            color_p += (y1 - orig_y1) * orig_w + (x1 - orig_x1);
            while (y1 <= y2) {
                LCD_DrawRGB565Image((uint16_t)x1,
                                    (uint16_t)y1,
                                    (uint16_t)x2,
                                    (uint16_t)y1,
                                    (const uint16_t *)color_p);
                color_p += orig_w;
                y1++;
            }
        }
    }

    lv_disp_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
