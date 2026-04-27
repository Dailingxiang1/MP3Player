#ifndef __LCD_DRIVER_H
#define __LCD_DRIVER_H

#include "main.h"

// ST7789 FSMC 16位 并口地址定义
// NE1 基地址 0x60000000
// A16 作为 RS 引脚，由于是 16位数据宽，地址需要偏移 (1 << 17)
#define LCD_BASE        (0x60000000 | 0x00000000)
#define LCD_CMD_ADDR    (__IO uint16_t *)(LCD_BASE)
#define LCD_DATA_ADDR   (__IO uint16_t *)(LCD_BASE | (1 << 17)) // 0x60020000

// 简单的写函数宏
#define LCD_WR_REG(cmd)   (*LCD_CMD_ADDR = cmd)
#define LCD_WR_DATA(data) (*LCD_DATA_ADDR = data)

#define LCD_WIDTH        240U
#define LCD_HEIGHT       320U

// 常用颜色定义
#define WHITE         	 0xFFFF
#define BLACK         	 0x0000
#define BLUE         	 0x001F
#define RED           	 0xF800

void LCD_Init(void);
void LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_Fill_Color_Area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color_p);
void LCD_DrawRGB565Image(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *color_p);
void LCD_Fill_DMA(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color_p);
void LCD_Clear_DMA(uint16_t Color);

void LCD_ShowChar3232_Window(uint16_t x, uint16_t y, uint8_t index, uint16_t color, uint16_t bgcolor);
void LCD_ShowChinese32x32(uint16_t x, uint16_t y, uint8_t index, uint16_t color, uint16_t bgcolor);
// ... 其他绘图函数声明

#endif

