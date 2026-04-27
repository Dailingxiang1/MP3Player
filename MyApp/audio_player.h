#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "fatfs.h"
#include "i2s.h"
#include "minimp3.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PLAYER_MAX_SONGS      64U
#define AUDIO_PLAYER_MAX_PATH_LEN   64U
#define AUDIO_PLAYER_STREAM_BUF_SZ  (8U * 1024U)
#define AUDIO_PLAYER_PCM_SAMPLES    (1152U * 2U)
#define AUDIO_PLAYER_PCM_BUF_COUNT  3U

typedef enum {
    AUDIO_FORMAT_UNKNOWN = 0,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_WAV
} audio_format_t;

typedef enum {
    AUDIO_PLAYER_STOPPED = 0,
    AUDIO_PLAYER_PLAYING,
    AUDIO_PLAYER_PAUSED,
    AUDIO_PLAYER_FINISHED,
    AUDIO_PLAYER_ERROR
} audio_player_state_t;

typedef struct {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_start;
    uint32_t data_size;
    uint32_t data_left;
} audio_wav_info_t;

/* CPU-only working memory. Current FatFs SD path uses polling reads, so this
 * buffer is safe in CCMRAM. If SDIO DMA file reads are enabled later, move
 * stream_buf back to APP_DMA_RAM/SRAM.
 */
typedef struct {
    uint8_t stream_buf[AUDIO_PLAYER_STREAM_BUF_SZ];
    char playlist[AUDIO_PLAYER_MAX_SONGS][AUDIO_PLAYER_MAX_PATH_LEN];
} audio_player_workmem_t;

/* I2S DMA reads pcm_buf directly. This object must stay in SRAM1/SRAM2, not CCMRAM. */
typedef struct {
    int16_t pcm_buf[AUDIO_PLAYER_PCM_BUF_COUNT][AUDIO_PLAYER_PCM_SAMPLES];
} audio_player_dma_buffers_t;

typedef struct {
    I2S_HandleTypeDef *i2s;
    FIL file;
    uint8_t file_open;

    audio_player_workmem_t *workmem;
    audio_player_dma_buffers_t *dmabuf;

    uint16_t playlist_len;
    uint16_t current_index;

    audio_format_t format;
    audio_player_state_t state;

    volatile uint8_t pcm_state[AUDIO_PLAYER_PCM_BUF_COUNT];
    uint16_t pcm_len[AUDIO_PLAYER_PCM_BUF_COUNT];
    volatile uint8_t active_buf;
    volatile uint8_t dma_idle;

    mp3dec_t mp3d;
    mp3dec_frame_info_t mp3_info;
    int mp3_data_pos;
    int mp3_data_remain;
    uint8_t mp3_eof;

    audio_wav_info_t wav;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;

    volatile uint32_t dma_cplt_count;
    volatile uint32_t underflow_count;
} audio_player_t;

void AudioPlayer_Init(audio_player_t *player,
                      I2S_HandleTypeDef *i2s,
                      audio_player_workmem_t *workmem,
                      audio_player_dma_buffers_t *dmabuf);
FRESULT AudioPlayer_BuildPlaylist(audio_player_t *player, const char *path);
FRESULT AudioPlayer_Start(audio_player_t *player);
FRESULT AudioPlayer_PlayIndex(audio_player_t *player, uint16_t index);
void AudioPlayer_Process(audio_player_t *player);
void AudioPlayer_Stop(audio_player_t *player);
void AudioPlayer_Next(audio_player_t *player);
void AudioPlayer_Previous(audio_player_t *player);
void AudioPlayer_Pause(audio_player_t *player);
void AudioPlayer_Resume(audio_player_t *player);
void AudioPlayer_TogglePause(audio_player_t *player);

const char *AudioPlayer_GetCurrentPath(const audio_player_t *player);
const char *AudioPlayer_GetCurrentName(const audio_player_t *player);
const char *AudioPlayer_FormatName(audio_format_t format);
const char *AudioPlayer_StateName(audio_player_state_t state);
uint32_t AudioPlayer_TakeDmaCpltCount(audio_player_t *player);
uint32_t AudioPlayer_TakeUnderflowCount(audio_player_t *player);
uint8_t AudioPlayer_GetPcmBufferedCount(const audio_player_t *player);
uint8_t AudioPlayer_IsBufferLow(const audio_player_t *player);

#ifdef __cplusplus
}
#endif

#endif
