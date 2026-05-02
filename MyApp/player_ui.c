#include "player_ui.h"

#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define PLAYER_UI_UPDATE_MS       500U

#define PLAYER_UI_SCREEN_W        240
#define PLAYER_UI_CONTENT_X       8
#define PLAYER_UI_CONTENT_W       224

#define PLAYER_UI_CARD_Y          54
#define PLAYER_UI_CARD_H          128

#define PLAYER_UI_META_Y          192
#define PLAYER_UI_META_H          60

#define PLAYER_UI_DOCK_Y          260
#define PLAYER_UI_DOCK_H          48

#define PLAYER_UI_BTN_H           42
#define PLAYER_UI_SIDE_W          58
#define PLAYER_UI_PLAY_W          74
#define PLAYER_UI_BTN_GAP         10

static lv_obj_t *s_title;
static lv_obj_t *s_subtitle;
static lv_obj_t *s_card;
static lv_obj_t *s_track_label;
static lv_obj_t *s_song;
static lv_obj_t *s_status;
static lv_obj_t *s_state_badge;
static lv_obj_t *s_state_text;
static lv_obj_t *s_playlist_ring;
static lv_obj_t *s_disc_text;
static lv_obj_t *s_meta_panel;
static lv_obj_t *s_format_text;
static lv_obj_t *s_rate_text;
static lv_obj_t *s_channel_text;
static lv_obj_t *s_buffer_title;
static lv_obj_t *s_buffer_value;
static lv_obj_t *s_buffer_bar;
static lv_obj_t *s_play_btn;
static lv_obj_t *s_play_label;

static uint32_t s_last_update;
static player_ui_cmd_t s_pending_cmd;
static audio_player_state_t s_last_state;
static uint8_t s_last_buffered;

static void label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *old_text;

    if (label == NULL || text == NULL) {
        return;
    }

    old_text = lv_label_get_text(label);
    if (old_text == NULL || strcmp(old_text, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *create_label(lv_obj_t *parent,
                              lv_coord_t width,
                              lv_label_long_mode_t long_mode,
                              const lv_font_t *font,
                              lv_color_t color,
                              lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, long_mode);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);

    return label;
}

static void style_panel(lv_obj_t *obj,
                        lv_color_t bg0,
                        lv_color_t bg1,
                        lv_color_t border,
                        lv_coord_t radius,
                        lv_opa_t shadow_opa)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, bg0, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(obj, bg1, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);

    if (shadow_opa != LV_OPA_TRANSP) {
        lv_obj_set_style_shadow_width(obj, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_x(obj, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(obj, 6, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(obj, lv_color_hex(0x04070C), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(obj, shadow_opa, LV_PART_MAIN);
    }
}

static lv_obj_t *create_chip(lv_obj_t *parent,
                             lv_coord_t x,
                             lv_coord_t y,
                             lv_coord_t w,
                             lv_color_t bg,
                             lv_color_t border,
                             lv_color_t text_color,
                             lv_obj_t **label_out)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_t *label;

    style_panel(chip, bg, bg, border, 11, LV_OPA_TRANSP);
    lv_obj_set_size(chip, w, 22);
    lv_obj_set_pos(chip, x, y);

    label = create_label(chip, w - 8, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, text_color, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(label);

    if (label_out != NULL) {
        *label_out = label;
    }

    return chip;
}

static void set_button_palette(lv_obj_t *btn,
                               lv_color_t bg0,
                               lv_color_t bg1,
                               lv_color_t border,
                               lv_color_t text)
{
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, bg0, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(btn, bg1, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, border, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x05080E), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, text, LV_PART_MAIN);
}

static void prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        s_pending_cmd = PLAYER_UI_CMD_PREVIOUS;
    }
}

static void play_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        s_pending_cmd = PLAYER_UI_CMD_TOGGLE_PAUSE;
    }
}

static void next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        s_pending_cmd = PLAYER_UI_CMD_NEXT;
    }
}

static lv_obj_t *create_button(lv_obj_t *parent,
                               const char *text,
                               lv_coord_t x,
                               lv_coord_t width,
                               uint8_t primary,
                               lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_pos(btn, x, 3);
    lv_obj_set_size(btn, width, PLAYER_UI_BTN_H);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

    if (primary) {
        set_button_palette(btn,
                           lv_color_hex(0xD97706),
                           lv_color_hex(0xF59E0B),
                           lv_color_hex(0xF7B44B),
                           lv_color_hex(0xFFF7ED));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xB45309), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0xD97706), LV_PART_MAIN | LV_STATE_PRESSED);
    } else {
        set_button_palette(btn,
                           lv_color_hex(0x162232),
                           lv_color_hex(0x1E2D40),
                           lv_color_hex(0x384A63),
                           lv_color_hex(0xE5EDF7));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x223146), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x2A3B53), LV_PART_MAIN | LV_STATE_PRESSED);
    }

    lv_obj_add_event_cb(btn, cb, LV_EVENT_RELEASED, NULL);

    label = create_label(btn, width - 8, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0xFFFFFF), LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    if (primary) {
        s_play_label = label;
    }

    return btn;
}

static const char *state_short_name(audio_player_state_t state)
{
    switch (state) {
    case AUDIO_PLAYER_PLAYING:
        return "PLAYING";
    case AUDIO_PLAYER_PAUSED:
        return "PAUSED";
    case AUDIO_PLAYER_ERROR:
        return "ERROR";
    case AUDIO_PLAYER_FINISHED:
        return "FINISHED";
    case AUDIO_PLAYER_STOPPED:
    default:
        return "STOPPED";
    }
}

static lv_color_t state_color(audio_player_state_t state)
{
    switch (state) {
    case AUDIO_PLAYER_PLAYING:
        return lv_color_hex(0x16A34A);
    case AUDIO_PLAYER_PAUSED:
        return lv_color_hex(0xD97706);
    case AUDIO_PLAYER_ERROR:
        return lv_color_hex(0xDC2626);
    case AUDIO_PLAYER_FINISHED:
        return lv_color_hex(0x2563EB);
    case AUDIO_PLAYER_STOPPED:
    default:
        return lv_color_hex(0x64748B);
    }
}

static void sample_rate_to_text(char *buf, size_t size, uint32_t sample_rate)
{
    if (sample_rate >= 1000U) {
        if ((sample_rate % 1000U) == 0U) {
            snprintf(buf, size, "%luK", (unsigned long)(sample_rate / 1000U));
        } else {
            snprintf(buf, size, "%lu.%luK",
                     (unsigned long)(sample_rate / 1000U),
                     (unsigned long)((sample_rate % 1000U) / 100U));
        }
    } else {
        snprintf(buf, size, "%luHz", (unsigned long)sample_rate);
    }
}

static const char *channel_text(uint8_t channels)
{
    if (channels <= 1U) {
        return "MONO";
    }
    if (channels == 2U) {
        return "STEREO";
    }
    return "MULTI";
}

static void update_state_visuals(audio_player_state_t state)
{
    lv_color_t accent = state_color(state);

    if (s_last_state == state) {
        return;
    }
    s_last_state = state;

    if (s_state_badge != NULL) {
        lv_obj_set_style_bg_color(s_state_badge, accent, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(s_state_badge, lv_color_mix(lv_color_hex(0xFFFFFF), accent, LV_OPA_20), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_state_badge, accent, LV_PART_MAIN);
    }
    label_set_text_if_changed(s_state_text, state_short_name(state));

    if (s_playlist_ring != NULL) {
        lv_obj_set_style_arc_color(s_playlist_ring, accent, LV_PART_INDICATOR);
    }

    if (s_play_btn != NULL) {
        if (state == AUDIO_PLAYER_PLAYING) {
            set_button_palette(s_play_btn,
                               lv_color_hex(0xD97706),
                               lv_color_hex(0xF59E0B),
                               lv_color_hex(0xF7B44B),
                               lv_color_hex(0xFFF7ED));
            lv_obj_set_style_bg_color(s_play_btn, lv_color_hex(0xB45309), LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_bg_grad_color(s_play_btn, lv_color_hex(0xD97706), LV_PART_MAIN | LV_STATE_PRESSED);
        } else {
            set_button_palette(s_play_btn,
                               lv_color_hex(0x0F766E),
                               lv_color_hex(0x14B8A6),
                               lv_color_hex(0x3DD5C7),
                               lv_color_hex(0xECFEFF));
            lv_obj_set_style_bg_color(s_play_btn, lv_color_hex(0x0D5D58), LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_bg_grad_color(s_play_btn, lv_color_hex(0x0F766E), LV_PART_MAIN | LV_STATE_PRESSED);
        }
    }
}

static void update_buffer_bar(uint8_t count)
{
    char line[16];
    uint32_t percent;

    if (s_buffer_bar == NULL) {
        return;
    }

    if (count > AUDIO_PLAYER_PCM_BUF_COUNT) {
        count = AUDIO_PLAYER_PCM_BUF_COUNT;
    }

    if (s_last_buffered == count) {
        return;
    }
    s_last_buffered = count;

    lv_bar_set_value(s_buffer_bar, (int32_t)count, LV_ANIM_OFF);

    if (count <= 1U) {
        lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0x223047), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0xDC2626), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(s_buffer_bar, lv_color_hex(0xF87171), LV_PART_INDICATOR);
    } else if (count == 2U) {
        lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0x223047), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0xD97706), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(s_buffer_bar, lv_color_hex(0xFBBF24), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0x223047), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0x059669), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(s_buffer_bar, lv_color_hex(0x34D399), LV_PART_INDICATOR);
    }

    percent = ((uint32_t)count * 100U) / AUDIO_PLAYER_PCM_BUF_COUNT;
    snprintf(line, sizeof(line), "%lu%%", (unsigned long)percent);
    label_set_text_if_changed(s_buffer_value, line);
}

///* 声明字体（指向SD卡中的bin文件）*/
//static lv_font_t *my_font;

///* 在LVGL初始化后的某个函数中 */
//void test_sd_font(void)
//{

//	FIL test_file;
//	if(f_open(&test_file, "0:/output.bin", FA_READ) == FR_OK) {
//		printf("FatFS Read Normal\n");
//		f_close(&test_file);
//	} else {
//		printf("FatFS 底层报错，请检查物理连接或挂载(f_mount)！\n");
//	}
//	
//	lv_fs_file_t f;
//	lv_fs_res_t res = lv_fs_open(&f, "S:output.bin", LV_FS_MODE_RD);
//	if(res != LV_FS_RES_OK) {
//		// 如果这里报错 12，说明 LVGL 的盘符映射逻辑没跑通
//		printf("LVGL FS reflect failed: %d\n", res); 
//	}
//	else
//		printf("LVGL FS reflect success\n"); 
//	
//	lv_mem_monitor_t mon;
//	lv_mem_monitor(&mon);
//	printf("Free memory before load: %d bytes\n", mon.free_size);
//	
//	
//	uint8_t header[4];
//	uint32_t br;
//	lv_fs_open(&f, "S:output.bin", LV_FS_MODE_RD);
//	lv_fs_read(&f, header, 4, &br);
//	printf("Font Magic: %02X %02X %02X %02X\n", header[0], header[1], header[2], header[3]);
//	lv_fs_close(&f);
//    /* 加载SD卡中的字体文件，盘符' S:' 需与配置一致 */
//    my_font = lv_font_load("S:small.bin");
//    if(my_font == NULL) {
//        printf("Font load failed!\n");
//        return;
//    }
//	else
//		printf("Font load success!\n");
//	
//	lv_mem_monitor(&mon);
//	printf("Free memory after load: %d bytes\n", mon.free_size);
//    
//	 lv_obj_t *label = lv_label_create(lv_scr_act());

//    lv_obj_set_width(label, 240);
//    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
//    lv_obj_set_style_text_font(label, my_font, LV_PART_MAIN);
//    lv_obj_set_style_text_color(label, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
//    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
//    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
//	
//    lv_label_set_text(label, "0123456789");
//    lv_obj_set_pos(label, 0, 120);
//    /* 创建一个标签来测试字体 */
////    s_subtitle = create_label(scr, PLAYER_UI_SCREEN_W, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0x8DA3C2), LV_TEXT_ALIGN_CENTER);
////    lv_label_set_text(s_subtitle, "Author-DaiLingxiang");
////    lv_obj_set_pos(s_subtitle, 0, 34);
//	
////    lv_obj_t *label = lv_label_create(lv_scr_act());
////    lv_obj_set_style_text_font(label, my_font, 0);
////    lv_label_set_text(label, "你好，LVGL！Hello World!");
//}
void PlayerUi_Init(audio_player_t *player)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *disc_wrap;
    lv_obj_t *disc_core;
    lv_obj_t *disc_hub;
    lv_obj_t *dock;

    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x07101A), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x101B2A), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xE5ECF6), LV_PART_MAIN);

    s_title = create_label(scr, PLAYER_UI_SCREEN_W, LV_LABEL_LONG_CLIP, &lv_font_montserrat_20, lv_color_hex(0xF8FAFC), LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(s_title, "MP3 Player RTOS");
    lv_obj_set_pos(s_title, 0, 10);

    s_subtitle = create_label(scr, PLAYER_UI_SCREEN_W, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0x8DA3C2), LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(s_subtitle, "Authored by DaiLingxiang");
    lv_obj_set_pos(s_subtitle, 0, 34);
	
    s_card = lv_obj_create(scr);
    style_panel(s_card,
                lv_color_hex(0x121C2A),
                lv_color_hex(0x182638),
                lv_color_hex(0x32465F),
                20,
                LV_OPA_20);
    lv_obj_set_size(s_card, PLAYER_UI_CONTENT_W, PLAYER_UI_CARD_H);
    lv_obj_set_pos(s_card, PLAYER_UI_CONTENT_X, PLAYER_UI_CARD_Y);

    disc_wrap = lv_obj_create(s_card);
    style_panel(disc_wrap,
                lv_color_hex(0x0E1621),
                lv_color_hex(0x132232),
                lv_color_hex(0x283A4F),
                18,
                LV_OPA_TRANSP);
    lv_obj_set_size(disc_wrap, 92, 92);
    lv_obj_set_pos(disc_wrap, 12, 18);

    s_playlist_ring = lv_arc_create(disc_wrap);
    lv_obj_set_size(s_playlist_ring, 82, 82);
    lv_obj_center(s_playlist_ring);
    lv_obj_clear_flag(s_playlist_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_playlist_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_style(s_playlist_ring, NULL, LV_PART_KNOB);
    lv_arc_set_rotation(s_playlist_ring, 135);
    lv_arc_set_bg_angles(s_playlist_ring, 0, 270);
    lv_arc_set_range(s_playlist_ring, 0, 100);
    lv_arc_set_value(s_playlist_ring, 0);
    lv_obj_set_style_arc_width(s_playlist_ring, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_playlist_ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_playlist_ring, lv_color_hex(0x253447), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_playlist_ring, lv_color_hex(0x16A34A), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_playlist_ring, 1, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_playlist_ring, 1, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_playlist_ring, LV_OPA_TRANSP, LV_PART_MAIN);

    disc_core = lv_obj_create(disc_wrap);
    style_panel(disc_core,
                lv_color_hex(0x1C3047),
                lv_color_hex(0x0F1A27),
                lv_color_hex(0x425D7B),
                32,
                LV_OPA_TRANSP);
    lv_obj_set_size(disc_core, 60, 60);
    lv_obj_center(disc_core);

    s_disc_text = create_label(disc_core, 50, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0xEAF2FF), LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_disc_text, LV_ALIGN_CENTER, 0, -6);

    disc_hub = lv_obj_create(disc_core);
    style_panel(disc_hub,
                lv_color_hex(0xF59E0B),
                lv_color_hex(0xD97706),
                lv_color_hex(0xF7B44B),
                10,
                LV_OPA_TRANSP);
    lv_obj_set_size(disc_hub, 14, 14);
    lv_obj_align(disc_hub, LV_ALIGN_CENTER, 0, 16);

    s_track_label = create_label(s_card, 106, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0x8FA7C3), LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_track_label, 106, 18);

    s_song = create_label(s_card, 104, LV_LABEL_LONG_DOT, &lv_font_montserrat_16, lv_color_hex(0xF8FAFC), LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_song, 106, 42);

    s_state_badge = lv_obj_create(s_card);
    style_panel(s_state_badge,
                lv_color_hex(0x16A34A),
                lv_color_hex(0x15803D),
                lv_color_hex(0x16A34A),
                13,
                LV_OPA_TRANSP);
    lv_obj_set_size(s_state_badge, 90, 24);
    lv_obj_set_pos(s_state_badge, 106, 74);

    s_state_text = create_label(s_state_badge, 82, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0xFFFFFF), LV_TEXT_ALIGN_CENTER);
    lv_obj_center(s_state_text);

    s_status = create_label(s_card, 104, LV_LABEL_LONG_DOT, &lv_font_montserrat_14, lv_color_hex(0xB7C4D8), LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_status, 106, 106);

    s_meta_panel = lv_obj_create(scr);
    style_panel(s_meta_panel,
                lv_color_hex(0x101927),
                lv_color_hex(0x152233),
                lv_color_hex(0x2C3B52),
                18,
                LV_OPA_20);
    lv_obj_set_size(s_meta_panel, PLAYER_UI_CONTENT_W, PLAYER_UI_META_H);
    lv_obj_set_pos(s_meta_panel, PLAYER_UI_CONTENT_X, PLAYER_UI_META_Y);

    create_chip(s_meta_panel, 10, 8, 60,
                lv_color_hex(0x352313),
                lv_color_hex(0x8C5A1B),
                lv_color_hex(0xFFD7A3),
                &s_format_text);
    create_chip(s_meta_panel, 79, 8, 66,
                lv_color_hex(0x172536),
                lv_color_hex(0x365A82),
                lv_color_hex(0xB8D7FF),
                &s_rate_text);
    create_chip(s_meta_panel, 154, 8, 60,
                lv_color_hex(0x173126),
                lv_color_hex(0x2E6E58),
                lv_color_hex(0xBFF4D7),
                &s_channel_text);

    s_buffer_title = create_label(s_meta_panel, 144, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0xD9E3F2), LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_buffer_title, 12, 34);

    s_buffer_value = create_label(s_meta_panel, 44, LV_LABEL_LONG_CLIP, &lv_font_montserrat_14, lv_color_hex(0xD9E3F2), LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_buffer_value, 168, 34);

    s_buffer_bar = lv_bar_create(s_meta_panel);
    lv_obj_set_pos(s_buffer_bar, 12, 48);
    lv_obj_set_size(s_buffer_bar, 200, 6);
    lv_bar_set_range(s_buffer_bar, 0, AUDIO_PLAYER_PCM_BUF_COUNT);
    lv_bar_set_value(s_buffer_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0x223047), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_buffer_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_buffer_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_buffer_bar, lv_color_hex(0x16A34A), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_buffer_bar, lv_color_hex(0x34D399), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_buffer_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_buffer_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(s_buffer_bar, 0, LV_PART_MAIN);

    dock = lv_obj_create(scr);
    style_panel(dock,
                lv_color_hex(0x101826),
                lv_color_hex(0x172334),
                lv_color_hex(0x304156),
                18,
                LV_OPA_20);
    lv_obj_set_size(dock, PLAYER_UI_CONTENT_W, PLAYER_UI_DOCK_H);
    lv_obj_set_pos(dock, PLAYER_UI_CONTENT_X, PLAYER_UI_DOCK_Y);

    create_button(dock, "Prev", 10, PLAYER_UI_SIDE_W, 0U, prev_event_cb);
    s_play_btn = create_button(dock, "Pause", 75, PLAYER_UI_PLAY_W, 1U, play_event_cb);
    create_button(dock, "Next", 156, PLAYER_UI_SIDE_W, 0U, next_event_cb);

    s_last_update = HAL_GetTick() - PLAYER_UI_UPDATE_MS;
    s_last_state = (audio_player_state_t)0xFF;
    s_last_buffered = 0xFFU;
    PlayerUi_SetStatus("");
    PlayerUi_Update(player);
}

player_ui_cmd_t PlayerUi_TakeCommand(void)
{
    player_ui_cmd_t cmd = s_pending_cmd;

    s_pending_cmd = PLAYER_UI_CMD_NONE;
    return cmd;
}

void PlayerUi_Update(audio_player_t *player)
{
    char line[96];
    char rate_text[16];
    uint32_t now = HAL_GetTick();
    uint32_t playlist_percent = 0U;
    uint8_t buffered;

    if (player == NULL || s_song == NULL) {
        return;
    }

    if ((now - s_last_update) < PLAYER_UI_UPDATE_MS) {
        return;
    }
    s_last_update = now;

    if (player->playlist_len == 0U) {
        label_set_text_if_changed(s_track_label, "TRACK -- / --");
        label_set_text_if_changed(s_song, "No audio file");
        label_set_text_if_changed(s_disc_text, "IDLE");
        playlist_percent = 0U;
    } else {
        snprintf(line, sizeof(line), "TRACK %02u / %02u",
                 (unsigned int)(player->current_index + 1U),
                 (unsigned int)player->playlist_len);
        label_set_text_if_changed(s_track_label, line);
        label_set_text_if_changed(s_song, AudioPlayer_GetCurrentName(player));
        label_set_text_if_changed(s_disc_text, AudioPlayer_FormatName(player->format));
        playlist_percent = ((uint32_t)(player->current_index + 1U) * 100U) / player->playlist_len;
    }

    lv_arc_set_value(s_playlist_ring, (int16_t)playlist_percent);
    update_state_visuals(player->state);

    label_set_text_if_changed(s_play_label,
                              player->state == AUDIO_PLAYER_PLAYING ? "Pause" : "Play");

    label_set_text_if_changed(s_format_text, AudioPlayer_FormatName(player->format));

    sample_rate_to_text(rate_text, sizeof(rate_text), player->sample_rate);
    label_set_text_if_changed(s_rate_text, rate_text);
    label_set_text_if_changed(s_channel_text, channel_text(player->channels));

    buffered = AudioPlayer_GetPcmBufferedCount(player);
    snprintf(line, sizeof(line), "PCM Buffer  %u/%u",
             (unsigned int)buffered,
             (unsigned int)AUDIO_PLAYER_PCM_BUF_COUNT);
    label_set_text_if_changed(s_buffer_title, line);
    update_buffer_bar(buffered);
}

void PlayerUi_SetStatus(const char *text)
{
    const char *fallback = "Touch controls active";

    if (text != NULL && text[0] != '\0') {
        label_set_text_if_changed(s_status, text);
    } else {
        label_set_text_if_changed(s_status, fallback);
    }
}
