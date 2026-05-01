#include "audio_shared.h"
#include "audio_player.h"

#include <string.h>

const char *AudioPlayer_GetCurrentPath(const audio_player_t *player)
{
    (void)player;
    return AudioApp_GetContext()->file_path;
}

const char *AudioPlayer_GetCurrentName(const audio_player_t *player)
{
    const char *path;
    const char *slash;

    (void)player;
    path = AudioApp_GetContext()->file_path;
    slash = strrchr(path, '/');
    return (slash != NULL) ? (slash + 1) : path;
}

const char *AudioPlayer_FormatName(audio_format_t format)
{
    switch (format)
    {
    case AUDIO_FORMAT_MP3:
        return "MP3";
    case AUDIO_FORMAT_WAV:
        return "WAV";
    case AUDIO_FORMAT_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

const char *AudioPlayer_StateName(audio_player_state_t state)
{
    switch (state)
    {
    case AUDIO_PLAYER_PLAYING:
        return "PLAYING";
    case AUDIO_PLAYER_PAUSED:
        return "PAUSED";
    case AUDIO_PLAYER_FINISHED:
        return "FINISHED";
    case AUDIO_PLAYER_ERROR:
        return "ERROR";
    case AUDIO_PLAYER_STOPPED:
    default:
        return "STOPPED";
    }
}

uint32_t AudioPlayer_TakeDmaCpltCount(audio_player_t *player)
{
    uint32_t value;
    (void)player;
    taskENTER_CRITICAL();
    value = AudioApp_GetContext()->dma_complete_count;
    AudioApp_GetContext()->dma_complete_count = 0U;
    taskEXIT_CRITICAL();
    return value;
}

uint32_t AudioPlayer_TakeUnderflowCount(audio_player_t *player)
{
    uint32_t value;
    (void)player;
    taskENTER_CRITICAL();
    value = AudioApp_GetContext()->underrun_count;
    AudioApp_GetContext()->underrun_count = 0U;
    taskEXIT_CRITICAL();
    return value;
}

uint8_t AudioPlayer_GetPcmBufferedCount(const audio_player_t *player)
{
    (void)player;
    /*
     * 这里给 UI / 缓冲条展示用，按“用户直觉”统计：
     * - READY 的块
     * - 当前正在 PLAYING 的块
     * - 正在 FILLING 的块
     *
     * 因此它更接近“总共有几块 PCM 正在管线里流动”，
     * 会比只统计 READY 更符合你之前裸机版看到的 3/3 习惯。
     */
    return AudioApp_PcmBusyCount(AudioApp_GetContext());
}

uint8_t AudioPlayer_IsBufferLow(const audio_player_t *player)
{
    (void)player;
    return (AudioApp_PcmReadyCount(AudioApp_GetContext()) <= 1U) ? 1U : 0U;
}
