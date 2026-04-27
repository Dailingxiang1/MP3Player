#include "lvgl.h"

/* 定义一个结构体来保存球的速度信息 */
typedef struct {
    lv_obj_t * ball_obj;  // 球的图形对象
    int32_t vx;           // X轴速度 (每次移动多少像素)
    int32_t vy;           // Y轴速度
} ball_data_t;

/* 必须定义为静态变量，保证函数退出后数据还在 */
static ball_data_t my_ball; 

/**
 * @brief 定时器回调函数：负责处理物理运动逻辑
 * @param timer LVGL 传入的定时器指针
 */
static void ball_move_callback(lv_timer_t * timer)
{
    /* 1. 获取传入的用户数据 */
    ball_data_t * data = (ball_data_t *)timer->user_data;
    lv_obj_t * ball = data->ball_obj;

    /* 2. 获取屏幕和球的尺寸 */
    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());
    lv_coord_t ball_w = lv_obj_get_width(ball);
    lv_coord_t ball_h = lv_obj_get_height(ball);

    /* 3. 获取当前位置 */
    lv_coord_t x = lv_obj_get_x(ball);
    lv_coord_t y = lv_obj_get_y(ball);

    /* 4. 计算新位置 (物理位移) */
    x += data->vx;
    y += data->vy;

    /* 5. 碰撞检测 (碰到墙壁就反弹 -> 速度取反) */
    
    // 碰到右墙 或 左墙
    if(x >= (scr_w - ball_w)) {
        x = scr_w - ball_w; // 防止出界
        data->vx = -data->vx; // 速度反向
    } else if(x <= 0) {
        x = 0;
        data->vx = -data->vx;
    }

    // 碰到下墙 或 上墙
    if(y >= (scr_h - ball_h)) {
        y = scr_h - ball_h;
        data->vy = -data->vy;
    } else if(y <= 0) {
        y = 0;
        data->vy = -data->vy;
    }

    /* 6. 应用新坐标 */
    lv_obj_set_pos(ball, x, y);
}

/**
 * @brief 初始化弹球动画
 */
void lv_example_bouncing_ball(void)
{
    /* --- 1. 创建球 (其实就是一个圆角的矩形) --- */
    lv_obj_t * ball = lv_obj_create(lv_scr_act());
    
    /* 设置大小 */
    lv_obj_set_size(ball, 60, 60); 
    
    /* 设置为圆形 (半径 = 宽度的一半) */
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0); 
    
    /* 设置颜色 (红色) */
    lv_obj_set_style_bg_color(ball, lv_palette_main(LV_PALETTE_RED), 0);
    
    /* 去除滚动条和边框，为了好看 */
    lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(ball, 0, 0);

    /* 初始位置 */
    lv_obj_set_pos(ball, 50, 50);

    /* --- 2. 初始化运动数据 --- */
    my_ball.ball_obj = ball;
    my_ball.vx = 8; // 每次 X 轴移动 4 像素
    my_ball.vy = 7; // 每次 Y 轴移动 3 像素

    /* --- 3. 创建定时器 --- */
    /* 参数1: 回调函数
     * 参数2: 周期 (毫秒)，20ms 大约是 50fps
     * 参数3: 用户数据 (把我们的球传进去)
     */
    lv_timer_create(ball_move_callback, 10, &my_ball);
}