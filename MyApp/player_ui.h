#ifndef PLAYER_UI_H
#define PLAYER_UI_H

#include "audio_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_UI_CMD_NONE = 0,
    PLAYER_UI_CMD_PREVIOUS,
    PLAYER_UI_CMD_TOGGLE_PAUSE,
    PLAYER_UI_CMD_NEXT
} player_ui_cmd_t;

void PlayerUi_Init(audio_player_t *player);
void PlayerUi_Update(audio_player_t *player);
void PlayerUi_SetStatus(const char *text);
player_ui_cmd_t PlayerUi_TakeCommand(void);

#ifdef __cplusplus
}
#endif

#endif
