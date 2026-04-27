#include "lcd_driver.h"

// 假设你在 CubeMX 里配置了 PB0 为 LCD_RES, PB1 为 LCD_BLK
// 请根据实际硬件修改引脚

// 硬件复位
void LCD_Reset(void)
{
    // 如果使用了硬件复位引脚
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_RESET); // RES 低电平
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_SET);   // RES 高电平
    HAL_Delay(50);
}

// ST7789 初始化序列 (标准流程)
void LCD_Init(void)
{
    LCD_Reset(); // 硬件复位

    // 开启背光
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);

    LCD_WR_REG(0x11); // Sleep Out
    HAL_Delay(120);

    LCD_WR_REG(0x36); // Memory Data Access Control
    LCD_WR_DATA(0x00); // 具体的方向配置看你的屏幕安装方向

    LCD_WR_REG(0x3A); // Interface Pixel Format
    LCD_WR_DATA(0x55); // 16bit/pixel (65k colors)

    LCD_WR_REG(0xB2); // Porch Setting
    LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x33);
    LCD_WR_DATA(0x33);

    LCD_WR_REG(0xB7); // Gate Control
    LCD_WR_DATA(0x35);

    LCD_WR_REG(0xBB); // VCOM Setting
    LCD_WR_DATA(0x19);

    LCD_WR_REG(0xC0); // LCM Control
    LCD_WR_DATA(0x2C);

    LCD_WR_REG(0xC2); // VDV and VRH Command Enable
    LCD_WR_DATA(0x01);

    LCD_WR_REG(0xC3); // VRH Set
    LCD_WR_DATA(0x12);

    LCD_WR_REG(0xC4); // VDV Set
    LCD_WR_DATA(0x20);

    LCD_WR_REG(0xC6); // Frame Rate Control in Normal Mode
    LCD_WR_DATA(0x0F); // 60Hz

    LCD_WR_REG(0xD0); // Power Control 1
    LCD_WR_DATA(0xA4);
    LCD_WR_DATA(0xA1);

    LCD_WR_REG(0xE0); // Positive Voltage Gamma Control
    LCD_WR_DATA(0xD0);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x0D);
    LCD_WR_DATA(0x11);
    LCD_WR_DATA(0x13);
    LCD_WR_DATA(0x2B);
    LCD_WR_DATA(0x3F);
    LCD_WR_DATA(0x54);
    LCD_WR_DATA(0x4C);
    LCD_WR_DATA(0x18);
    LCD_WR_DATA(0x0D);
    LCD_WR_DATA(0x0B);
    LCD_WR_DATA(0x1F);
    LCD_WR_DATA(0x23);

    LCD_WR_REG(0xE1); // Negative Voltage Gamma Control
    LCD_WR_DATA(0xD0);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x11);
    LCD_WR_DATA(0x13);
    LCD_WR_DATA(0x2C);
    LCD_WR_DATA(0x3F);
    LCD_WR_DATA(0x44);
    LCD_WR_DATA(0x51);
    LCD_WR_DATA(0x2F);
    LCD_WR_DATA(0x1F);
    LCD_WR_DATA(0x1F);
    LCD_WR_DATA(0x20);
    LCD_WR_DATA(0x23);

    LCD_WR_REG(0x21); // Display Inversion On
    LCD_WR_REG(0x29); // Display On
}

// 简单的清屏函数，测试用
void LCD_Clear(uint16_t Color)
{
    uint32_t index = 0;
    uint32_t total_point = 240 * 320;
    
    // 1. 设置全屏窗口 (0,0) 到 (239,319)
    LCD_WR_REG(0x2A); 
    LCD_WR_DATA(0); LCD_WR_DATA(0); 
    LCD_WR_DATA((240-1) >> 8); LCD_WR_DATA((240-1) & 0xFF);

    LCD_WR_REG(0x2B); 
    LCD_WR_DATA(0); LCD_WR_DATA(0); 
    LCD_WR_DATA((320-1) >> 8); LCD_WR_DATA((320-1) & 0xFF);

    LCD_WR_REG(0x2C); // 开始写入显存

    // 2. 获取数据寄存器的直接地址 (避免每次都计算地址)
    // 注意：这里假设你的 LCD_DATA_ADDR 宏定义已经正确
    __IO uint16_t *pLCD_Data = LCD_DATA_ADDR;

    // 3. 循环展开 (Loop Unrolling) - 核心加速部分
    // 一次循环连续写 16 个像素，减少 if判 断和 i++ 的次数
    for(index = 0; index < total_point / 16; index++)
    {
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
        *pLCD_Data = Color;
    }
    
    // 处理剩下的余数 (如果有的话，240*320能被16整除，这里其实不会执行)
    for(index = 0; index < total_point % 16; index++)
    {
        *pLCD_Data = Color;
    }
}
/**
 * @brief  设置光标窗口
 * @param  x1, y1: 起始坐标
 * @param  x2, y2: 结束坐标
 */
void LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    // 1. 设置列地址 (Column Address Set) - X坐标
    LCD_WR_REG(0x2A); 
    // 这里的关键：必须先发高8位，再发低8位
    LCD_WR_DATA(x1 >> 8);   // 起始 X 高8位
    LCD_WR_DATA(x1 & 0xFF); // 起始 X 低8位
    LCD_WR_DATA(x2 >> 8);   // 结束 X 高8位
    LCD_WR_DATA(x2 & 0xFF); // 结束 X 低8位

    // 2. 设置行地址 (Row Address Set) - Y坐标
    LCD_WR_REG(0x2B); 
    LCD_WR_DATA(y1 >> 8);   // 起始 Y 高8位
    LCD_WR_DATA(y1 & 0xFF); // 起始 Y 低8位
    LCD_WR_DATA(y2 >> 8);   // 结束 Y 高8位
    LCD_WR_DATA(y2 & 0xFF); // 结束 Y 低8位

    // 3. 准备写显存
    LCD_WR_REG(0x2C); 
}
/**
 * @brief  在指定区域填充单一颜色
 * @param  x1, y1: 起点坐标
 * @param  x2, y2: 终点坐标
 * @param  color:  要填充的颜色 (比如 0xFFFF 白色)
 */
void LCD_Fill_Color_Area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color_p)
{
    uint32_t index;
    // 计算总像素点数
    uint32_t total_point = (x2 - x1 + 1) * (y2 - y1 + 1);

		uint16_t color = *color_p;
    // 1. 设置窗口 (调用刚才修正过的函数)
    LCD_SetWindow(x1, y1, x2, y2);

    // 2. 获取数据地址 (和你的 LCD_Clear 保持一致)
    // 务必加上 volatile (__IO)
    volatile uint16_t *pLCD_Data = (volatile uint16_t *)LCD_DATA_ADDR;

    // 3. 循环写入 (抄你 LCD_Clear 的作业，带循环展开)
    for(index = 0; index < total_point / 16; index++)
    {
        *pLCD_Data = color; *pLCD_Data = color;
        *pLCD_Data = color; *pLCD_Data = color;
        *pLCD_Data = color; *pLCD_Data = color;
        *pLCD_Data = color; *pLCD_Data = color;
        *pLCD_Data = color; *pLCD_Data = color;
        *pLCD_Data = color; *pLCD_Data = color;
        *pLCD_Data = color; *pLCD_Data = color;
        *pLCD_Data = color; *pLCD_Data = color;
    }

    // 处理余数
    for(index = 0; index < total_point % 16; index++)
    {
        *pLCD_Data = color;
    }
}

void LCD_DrawRGB565Image(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *color_p)
{
    uint32_t index;
    uint32_t total_point;
    volatile uint16_t *pLCD_Data = (volatile uint16_t *)LCD_DATA_ADDR;

    if (color_p == 0) {
        return;
    }

    if (x1 >= LCD_WIDTH || y1 >= LCD_HEIGHT) {
        return;
    }

    if (x2 >= LCD_WIDTH) {
        x2 = LCD_WIDTH - 1U;
    }
    if (y2 >= LCD_HEIGHT) {
        y2 = LCD_HEIGHT - 1U;
    }
    if (x2 < x1 || y2 < y1) {
        return;
    }

    total_point = (uint32_t)(x2 - x1 + 1U) * (uint32_t)(y2 - y1 + 1U);
    LCD_SetWindow(x1, y1, x2, y2);

    for (index = 0; index < total_point; index++) {
        *pLCD_Data = color_p[index];
    }
}

static uint16_t color_temp;
//extern DMA_HandleTypeDef hdma_memtomem_dma2_stream0;
//void LCD_Clear_DMA(uint16_t Color)
//{
//    // 1. 准备一个变量存放颜色
//    color_temp = Color;
//    
//    // 2. 设置窗口
//    LCD_WR_REG(0x2A); 
//    LCD_WR_DATA(0); LCD_WR_DATA(0); 
//    LCD_WR_DATA((240-1) >> 8); LCD_WR_DATA((240-1) & 0xFF);

//    LCD_WR_REG(0x2B); 
//    LCD_WR_DATA(0); LCD_WR_DATA(0); 
//    LCD_WR_DATA((320-1) >> 8); LCD_WR_DATA((320-1) & 0xFF);

//    LCD_WR_REG(0x2C);

//    // 3. 临时修改 DMA 配置：关闭源地址自增
//    // 注意：直接操作寄存器最快，HAL库改配置太繁琐
//    // MINC 是 Memory Increment 的意思，置 0 关闭
//    //hdma_memtomem_dma2_stream0.Instance->CR &= ~DMA_SxCR_MINC; 

//    // 4. 启动 DMA
//    // 源地址：颜色变量的地址
//    // 长度：76800
//    // F4 的 DMA 长度寄存器只有 16位 (最大65535)，所以全屏必须分两次传！
//    
//    // 第一半 (38400个点)
//    HAL_DMA_Start(&hdma_memtomem_dma2_stream0, (uint32_t)&color_temp, (uint32_t)LCD_DATA_ADDR, 38400);
//    HAL_DMA_PollForTransfer(&hdma_memtomem_dma2_stream0, HAL_DMA_FULL_TRANSFER, 10);
//    
//    // 第二半 (38400个点)
//    HAL_DMA_Start(&hdma_memtomem_dma2_stream0, (uint32_t)&color_temp, (uint32_t)LCD_DATA_ADDR, 38400);
//    HAL_DMA_PollForTransfer(&hdma_memtomem_dma2_stream0, HAL_DMA_FULL_TRANSFER, 10);

//    // 5. 用完记得把源地址自增改回来，以免下次刷图片出错
//    //hdma_memtomem_dma2_stream0.Instance->CR |= DMA_SxCR_MINC;
//}

#include "fonts.h"
/**
 * @brief  [高效版] 在指定位置显示一个 32x32 的字符
 * @param  x, y:   起始坐标
 * @param  index:  字库数组的索引 (对应之前定义的 my_font_3232)
 * @param  color:  字体颜色
 * @param  bgcolor:背景颜色
 */
/**
 * @brief  [修正版] 32x32 字符显示
 * @note   取模方式：逐行式、低位在前 (LSB First)、阴码 (1=笔画)
 */
void LCD_ShowChar3232_Window(uint16_t x, uint16_t y, uint8_t index, uint16_t color, uint16_t bgcolor)
{
    uint8_t i, j, k;
    uint8_t temp;
    
    // 1. 设置窗口：32x32
    LCD_SetWindow(x, y, x + 32 - 1, y + 32 - 1);

    // 2. 获取数据地址
    volatile uint16_t *lcd_reg_data = (volatile uint16_t *)LCD_DATA_ADDR;

    // 3. 开始遍历字库数据
    for(i = 0; i < 32; i++) // 行循环
    {
        for(j = 0; j < 4; j++) // 列字节循环 (32像素 / 8位 = 4字节)
        {
            // 取出当前字节
            temp = my_font_3232[index][i * 4 + j];
            
            // 4. 位循环：因为是【低位在前】，所以先判断 bit0，最后判断 bit7
            for(k = 0; k < 8; k++)
            {
                // 【修改点1】判断最低位 (LSB) 是否为 1
                if(temp & 0x01) 
                {
                    // 阴码：1 代表有笔画，写字体色
                    *lcd_reg_data = color;   
                }
                else
                {
                    // 阴码：0 代表无笔画，写背景色
                    *lcd_reg_data = bgcolor; 
                }
                
                // 【修改点2】向右移位，把 bit1 移到 bit0 的位置，准备下一次判断
                temp >>= 1; 
            }
        }
    }
}

/**
 * @brief  显示 32x32 的汉字 (专用)
 * @param  x, y:   起始坐标
 * @param  index:  汉字在数组中的索引 (0="播", 1="放", 2="器")
 * @param  color:  字体颜色
 * @param  bgcolor:背景颜色
 */
void LCD_ShowChinese32x32(uint16_t x, uint16_t y, uint8_t index, uint16_t color, uint16_t bgcolor)
{
    uint8_t i, j, k;
    uint8_t temp;
    
    // 1. 设置窗口：必须是 32x32
    // x2 = x + 32 - 1
    LCD_SetWindow(x, y, x + 31, y + 31);

    // 2. 获取 FSMC 地址
    volatile uint16_t *lcd_reg_data = (volatile uint16_t *)LCD_DATA_ADDR;

    // 3. 循环 32 行
    for(i = 0; i < 32; i++)
    {
        // 4. 循环 4 个字节 (32像素宽 / 8 = 4字节)
        // 之前只显示1/4就是因为这里可能写成了 j<2
        for(j = 0; j < 4; j++) 
        {
            // 取出当前字节
            temp = my_font_3232[index][i * 4 + j];
            
            // 5. 循环 8 个 bit
            // 默认按“高位在前”写，因为大部分取模软件默认 0x60 是左边有字
            for(k = 0; k < 8; k++)
            {
                // 如果是【低位在前】，请把 0x80 改成 0x01
                if(temp & 0x01) 
                {
                    *lcd_reg_data = color;   // 有笔画
                }
                else
                {
                    *lcd_reg_data = bgcolor; // 背景
                }
                
                // 如果是【低位在前】，请把 << 1 改成 >> 1
                temp >>= 1; 
            }
        }
    }
//		for (int i = 0; i < 32; i++)
//			for (int j = 0; j < 32; j++)
//					*lcd_reg_data = WHITE;
}

