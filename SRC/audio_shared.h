#ifndef SRC_AUDIO_SHARED_H
#define SRC_AUDIO_SHARED_H

#include "FreeRTOS.h"
#include "task.h"
#include "fatfs.h"
#include "i2s.h"
#include "main.h"
#include "minimp3.h"

#include <stdint.h>

#define AUDIO_FILE_SCAN_PATH              "/"
#define AUDIO_FILE_PATH_MAX_LEN           128U

/**
 * @brief MP3 压缩流缓冲配置档位。
 *
 * 作用：
 * - 只影响“读卡/补流”这一层，不会直接改变 I2S 播放时钟
 * - 适合你现在做 8KB / 16KB A/B 对比测试
 *
 * 使用方法：
 * - 想测 8KB：把 AUDIO_MP3_STREAM_PROFILE 改成 AUDIO_MP3_STREAM_PROFILE_8KB
 * - 想测 16KB：把 AUDIO_MP3_STREAM_PROFILE 改成 AUDIO_MP3_STREAM_PROFILE_16KB
 */
#define AUDIO_MP3_STREAM_PROFILE_8KB      0U
#define AUDIO_MP3_STREAM_PROFILE_16KB     1U

/**
 * @brief 当前选择的 MP3 流缓冲档位。
 *
 * 默认先保持你当前正在使用的 16KB，方便和之前行为保持一致。
 */
#define AUDIO_MP3_STREAM_PROFILE          AUDIO_MP3_STREAM_PROFILE_16KB

#if (AUDIO_MP3_STREAM_PROFILE == AUDIO_MP3_STREAM_PROFILE_8KB)
    /**
     * 8KB 档：
     * - 更适合做“缓冲变小会不会影响卡顿”的对比实验
     * - 读卡更频繁，memmove/refill 触发也更频繁
     */
    #define AUDIO_MP3_STREAM_BUFFER_SIZE  (8U * 1024U)
    #define AUDIO_MP3_READ_CHUNK          (2U * 1024U)
    #define AUDIO_MP3_REFILL_THRESHOLD    (2U * 1024U)
    #define AUDIO_MP3_STREAM_PROFILE_TEXT "8KB"
#else
    /**
     * 16KB 档：
     * - 读卡频率更低
     * - 更偏稳妥
     * - 也正是你之前“刚好占满 SRAM2”的版本
     */
    #define AUDIO_MP3_STREAM_BUFFER_SIZE  (16U * 1024U)
    #define AUDIO_MP3_READ_CHUNK          (4U * 1024U)
    #define AUDIO_MP3_REFILL_THRESHOLD    (4U * 1024U)
    #define AUDIO_MP3_STREAM_PROFILE_TEXT "16KB"
#endif

#define AUDIO_MP3_SEARCH_GUARD            256

#define AUDIO_PCM_BLOCK_COUNT             3U
#define AUDIO_PCM_BLOCK_SAMPLES           MINIMP3_MAX_SAMPLES_PER_FRAME
#define AUDIO_PREFILL_BLOCKS              2U

#define AUDIO_DECODE_TASK_STACK_WORDS     2048U
#define AUDIO_PLAY_TASK_STACK_WORDS       1024U
#define AUDIO_DECODE_TASK_PRIORITY        (configMAX_PRIORITIES - 3)
#define AUDIO_PLAY_TASK_PRIORITY          (configMAX_PRIORITIES - 2)

#define AUDIO_PLAY_NOTIFY_DATA_READY      (1UL << 0)
#define AUDIO_PLAY_NOTIFY_DMA_DONE        (1UL << 1)
#define AUDIO_PLAY_NOTIFY_DECODE_DONE     (1UL << 2)
#define AUDIO_PLAY_NOTIFY_ERROR           (1UL << 3)

typedef enum
{
    AUDIO_TASK_STATE_IDLE = 0,
    AUDIO_TASK_STATE_BUFFERING,
    AUDIO_TASK_STATE_PLAYING,
    AUDIO_TASK_STATE_FINISHED,
    AUDIO_TASK_STATE_ERROR
} audio_task_state_t;

typedef enum
{
    AUDIO_PCM_EMPTY = 0,
    AUDIO_PCM_FILLING,
    AUDIO_PCM_READY,
    AUDIO_PCM_PLAYING
} audio_pcm_state_t;

typedef struct
{
    I2S_HandleTypeDef *i2s;

    FATFS *fs;
    FIL *file;
    uint8_t fs_mounted;
    uint8_t file_open;
    char file_path[AUDIO_FILE_PATH_MAX_LEN];

    mp3dec_t decoder;
    mp3dec_frame_info_t frame_info;
    uint32_t current_sample_rate;
    uint16_t current_channels;
    uint16_t current_bitrate_kbps;
    uint8_t sample_rate_configured;

    int stream_offset;
    int stream_bytes;
    uint8_t eof;
    uint8_t decode_done;
    volatile uint8_t next_song_request;

    volatile audio_task_state_t run_state;
    volatile uint8_t pcm_state[AUDIO_PCM_BLOCK_COUNT];
    volatile uint16_t pcm_sample_count[AUDIO_PCM_BLOCK_COUNT];
    volatile uint8_t active_index;
    volatile uint8_t dma_idle;

    volatile uint32_t decoded_frame_count;
    volatile uint32_t sd_read_bytes;
    volatile uint32_t dma_complete_count;
    volatile uint32_t underrun_count;
    volatile uint32_t low_buffer_count;
    volatile uint32_t decode_skip_count;

    volatile FRESULT last_fres;

    TaskHandle_t decode_task_handle;
    TaskHandle_t play_task_handle;
} audio_context_t;

audio_context_t *AudioApp_GetContext(void);
uint8_t *AudioApp_GetStreamBuffer(void);
int16_t *AudioApp_GetPcmBlock(uint8_t index);

void AudioApp_ResetPipeline(audio_context_t *ctx);
void AudioApp_CloseFile(audio_context_t *ctx);
void AudioApp_StopDma(audio_context_t *ctx);

FRESULT AudioApp_FindFirstMp3(audio_context_t *ctx, const char *path);
FRESULT AudioApp_FindNextMp3(audio_context_t *ctx, const char *path);
HAL_StatusTypeDef AudioApp_ReconfigI2S(audio_context_t *ctx, uint32_t sample_rate);

uint8_t AudioApp_PcmReadyCount(const audio_context_t *ctx);
uint8_t AudioApp_PcmBusyCount(const audio_context_t *ctx);
int AudioApp_FindNextReadyBlock(const audio_context_t *ctx, uint8_t after_index);
int AudioApp_ReserveEmptyBlock(audio_context_t *ctx);
void AudioApp_ReleaseBlock(audio_context_t *ctx, uint8_t index);
void AudioApp_PublishBlock(audio_context_t *ctx, uint8_t index, uint16_t sample_count);

const char *AudioApp_StateString(audio_task_state_t state);

void AudioDecodeTask(void *argument);
void AudioPlayTask(void *argument);

#endif /* SRC_AUDIO_SHARED_H */
