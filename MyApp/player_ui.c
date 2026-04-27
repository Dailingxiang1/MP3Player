#include "player_ui.h"

#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#define PLAYER_UI_UPDATE_MS       500U

#define PLAYER_UI_SCREEN_W        240
#define PLAYER_UI_CARD_W          224
#define PLAYER_UI_CARD_H          122
#define PLAYER_UI_TRACK_W         200
#define PLAYER_UI_TRACK_H         8

#define PLAYER_UI_BTN_H           42
#define PLAYER_UI_SIDE_W          58
#define PLAYER_UI_PLAY_W          74
#define PLAYER_UI_BTN_GAP         10
#define PLAYER_UI_BAR_W           (PLAYER_UI_SIDE_W + PLAYER_UI_BTN_GAP + PLAYER_UI_PLAY_W + PLAYER_UI_BTN_GAP + PLAYER_UI_SIDE_W)
#define PLAYER_UI_BAR_H           PLAYER_UI_BTN_H

static lv_obj_t *s_title;
static lv_obj_t *s_card;
static lv_obj_t *s_song;
static lv_obj_t *s_state_badge;
static lv_obj_t *s_state_text;
static lv_obj_t *s_info;
static lv_obj_t *s_status;
static lv_obj_t *s_buffer;
static lv_obj_t *s_buffer_track;
static lv_obj_t *s_buffer_fill;
static lv_obj_t *s_play_label;

static uint32_t s_last_update;
static player_ui_cmd_t s_pending_cmd;

static void label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *old_text;

    if (label == NULL) {
        return;
    }

    old_text = lv_label_get_text(label);
    if (old_text == NULL || strcmp(old_text, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *create_plain_label(lv_obj_t *parent,
                                    lv_coord_t width,
                                    lv_label_long_mode_t long_mode,
                                    lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, long_mode);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    return label;
}

static void style_panel(lv_obj_t *obj, lv_color_t bg, lv_color_t border)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, bg, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 12, LV_PART_MAIN);
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

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_coord_t width, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(btn, width, PLAYER_UI_BTN_H);
    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x253140), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xD97706), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x516174), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_RELEASED, NULL);

    label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

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
        return lv_color_hex(0x475569);
    }
}

static void update_state_badge(audio_player_state_t state)
{
    if (s_state_badge == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(s_state_badge, state_color(state), LV_PART_MAIN);
    label_set_text_if_changed(s_state_text, state_short_name(state));
}

static void update_buffer_bar(uint8_t count)
{
    lv_coord_t width;

    if (s_buffer_fill == NULL) {
        return;
    }

    if (count > AUDIO_PLAYER_PCM_BUF_COUNT) {
        count = AUDIO_PLAYER_PCM_BUF_COUNT;
    }

    width = (lv_coord_t)((PLAYER_UI_TRACK_W * count) / AUDIO_PLAYER_PCM_BUF_COUNT);
    if (count != 0U && width < 2) {
        width = 2;
    }

    lv_obj_set_width(s_buffer_fill, width);

    if (count <= 1U) {
        lv_obj_set_style_bg_color(s_buffer_fill, lv_color_hex(0xDC2626), LV_PART_MAIN);
    } else if (count == 2U) {
        lv_obj_set_style_bg_color(s_buffer_fill, lv_color_hex(0xD97706), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(s_buffer_fill, lv_color_hex(0x16A34A), LV_PART_MAIN);
    }
}

void PlayerUi_Init(audio_player_t *player)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *button_bar;
    lv_obj_t *prev_btn;
    lv_obj_t *play_btn;
    lv_obj_t *next_btn;

    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1017), LV_PART_MAIN);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xE5E7EB), LV_PART_MAIN);

    s_title = create_plain_label(scr, PLAYER_UI_SCREEN_W, LV_LABEL_LONG_CLIP, lv_color_hex(0xF8FAFC));
    lv_label_set_text(s_title, "MP3 Player");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 10);

    s_card = lv_obj_create(scr);
    style_panel(s_card, lv_color_hex(0x151C26), lv_color_hex(0x334155));
    lv_obj_set_size(s_card, PLAYER_UI_CARD_W, PLAYER_UI_CARD_H);
    lv_obj_align(s_card, LV_ALIGN_TOP_MID, 0, 38);

    s_song = create_plain_label(s_card, 202, LV_LABEL_LONG_DOT, lv_color_hex(0xF8FAFC));
    lv_obj_set_style_text_align(s_song, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_song, LV_ALIGN_TOP_MID, 0, 16);

    s_state_badge = lv_obj_create(s_card);
    lv_obj_remove_style_all(s_state_badge);
    lv_obj_clear_flag(s_state_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_state_badge, 86, 24);
    lv_obj_set_style_bg_opa(s_state_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_state_badge, 12, LV_PART_MAIN);
    lv_obj_align(s_state_badge, LV_ALIGN_TOP_MID, 0, 52);

    s_state_text = create_plain_label(s_state_badge, 82, LV_LABEL_LONG_CLIP, lv_color_hex(0xFFFFFF));
    lv_obj_center(s_state_text);

    s_info = create_plain_label(s_card, 204, LV_LABEL_LONG_DOT, lv_color_hex(0xCBD5E1));
    lv_obj_align(s_info, LV_ALIGN_TOP_MID, 0, 90);

    s_status = create_plain_label(scr, 220, LV_LABEL_LONG_DOT, lv_color_hex(0x94A3B8));
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 170);

    s_buffer = create_plain_label(scr, 220, LV_LABEL_LONG_CLIP, lv_color_hex(0xCBD5E1));
    lv_obj_align(s_buffer, LV_ALIGN_TOP_MID, 0, 198);

    s_buffer_track = lv_obj_create(scr);
    lv_obj_remove_style_all(s_buffer_track);
    lv_obj_clear_flag(s_buffer_track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_buffer_track, PLAYER_UI_TRACK_W, PLAYER_UI_TRACK_H);
    lv_obj_set_style_bg_opa(s_buffer_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_buffer_track, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_radius(s_buffer_track, 4, LV_PART_MAIN);
    lv_obj_align(s_buffer_track, LV_ALIGN_TOP_MID, 0, 222);

    s_buffer_fill = lv_obj_create(s_buffer_track);
    lv_obj_remove_style_all(s_buffer_fill);
    lv_obj_clear_flag(s_buffer_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_buffer_fill, 0, PLAYER_UI_TRACK_H);
    lv_obj_set_style_bg_opa(s_buffer_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_buffer_fill, lv_color_hex(0x16A34A), LV_PART_MAIN);
    lv_obj_set_style_radius(s_buffer_fill, 4, LV_PART_MAIN);
    lv_obj_set_pos(s_buffer_fill, 0, 0);

    button_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(button_bar);
    lv_obj_clear_flag(button_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(button_bar, PLAYER_UI_BAR_W, PLAYER_UI_BAR_H);
    lv_obj_align(button_bar, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_flex_flow(button_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_bar,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(button_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(button_bar, PLAYER_UI_BTN_GAP, LV_PART_MAIN);

    prev_btn = create_button(button_bar, "Prev", PLAYER_UI_SIDE_W, prev_event_cb);

    play_btn = create_button(button_bar, "Pause", PLAYER_UI_PLAY_W, play_event_cb);
    s_play_label = lv_obj_get_child(play_btn, 0);

    next_btn = create_button(button_bar, "Next", PLAYER_UI_SIDE_W, next_event_cb);

    /* Fallback positions. If Flex is initialized correctly it will override these
     * during layout. If a stale incremental build misses lv_flex_init(), these
     * positions still keep the buttons separated instead of stacked at (0,0).
     */
    lv_obj_set_pos(prev_btn, 0, 0);
    lv_obj_set_pos(play_btn, PLAYER_UI_SIDE_W + PLAYER_UI_BTN_GAP, 0);
    lv_obj_set_pos(next_btn, PLAYER_UI_SIDE_W + PLAYER_UI_BTN_GAP + PLAYER_UI_PLAY_W + PLAYER_UI_BTN_GAP, 0);
    lv_obj_update_layout(button_bar);

    s_last_update = HAL_GetTick() - PLAYER_UI_UPDATE_MS;
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
    char line[112];
    uint32_t now = HAL_GetTick();
    uint8_t buffered;

    if (player == NULL || s_song == NULL) {
        return;
    }

    if ((now - s_last_update) < PLAYER_UI_UPDATE_MS) {
        return;
    }
    s_last_update = now;

    if (player->playlist_len == 0U) {
        label_set_text_if_changed(s_song, "No audio file");
    } else {
        snprintf(line, sizeof(line), "%u/%u  %s",
                 (unsigned int)(player->current_index + 1U),
                 (unsigned int)player->playlist_len,
                 AudioPlayer_GetCurrentName(player));
        label_set_text_if_changed(s_song, line);
    }

    update_state_badge(player->state);

    snprintf(line, sizeof(line), "%s  |  %lu Hz  |  %u ch",
             AudioPlayer_FormatName(player->format),
             (unsigned long)player->sample_rate,
             (unsigned int)player->channels);
    label_set_text_if_changed(s_info, line);

    label_set_text_if_changed(s_play_label,
                              player->state == AUDIO_PLAYER_PLAYING ? "Pause" : "Play");

    buffered = AudioPlayer_GetPcmBufferedCount(player);
    snprintf(line, sizeof(line), "Audio buffer  %u/%u",
             (unsigned int)buffered,
             (unsigned int)AUDIO_PLAYER_PCM_BUF_COUNT);
    label_set_text_if_changed(s_buffer, line);
    update_buffer_bar(buffered);
}

void PlayerUi_SetStatus(const char *text)
{
    label_set_text_if_changed(s_status, text == NULL ? "" : text);
}
