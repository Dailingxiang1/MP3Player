#ifndef SRC_AUDIO_SHARED_H
#define SRC_AUDIO_SHARED_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "fatfs.h"
#include "i2s.h"
#include "main.h"
#include "minimp3.h"

#include <stdint.h>

/* ================================
 * File / path configuration
 * ================================ */
#define AUDIO_SCAN_PATH                    "/"
#define AUDIO_FILE_PATH_MAX_LEN            128U
#define AUDIO_PLAYLIST_MAX_SONGS           64U

/* ================================
 * MP3 compressed stream buffer
 * ================================
 *
 * 说明：
 * 1. 这里的 ring buffer 存放“压缩后的 MP3 字节流”；
 * 2. SD 预取任务只负责往里写；
 * 3. 解码任务只负责从里读。
 *
 * 取 16KB 的原因：
 * - 这是你前面实测较稳定的档位；
 * - 同时也正好适合放进 SRAM2。
 *
 * 注意：
 * - 缓冲区大小必须是 2 的幂，方便用掩码做环形回绕。
 */
#define AUDIO_STREAM_BUFFER_SIZE           (16U * 1024U)
#define AUDIO_STREAM_BUFFER_MASK           (AUDIO_STREAM_BUFFER_SIZE - 1U)
#define AUDIO_STREAM_READ_CHUNK            (4U * 1024U)
#define AUDIO_STREAM_REFILL_WATERMARK      (8U * 1024U)
#define AUDIO_STREAM_PREFILL_TARGET        (12U * 1024U)

/*
 * 当环形缓冲跨尾部时，解码任务需要把一段数据线性化后再丢给 minimp3。
 * 这里准备一个“线性视图缓冲区”大小。
 */
#define AUDIO_STREAM_LINEAR_VIEW_SIZE      (4U * 1024U)

/* minimp3 搜帧保护上限。 */
#define AUDIO_DECODE_SEARCH_GUARD          256

/* ================================
 * PCM buffer configuration
 * ================================ */
#define AUDIO_PCM_BLOCK_COUNT              3U
#define AUDIO_PCM_BLOCK_SAMPLES            MINIMP3_MAX_SAMPLES_PER_FRAME
#define AUDIO_PLAY_PREFILL_BLOCKS          2U

/* ================================
 * FreeRTOS task configuration
 * ================================ */
#define AUDIO_SD_TASK_STACK_WORDS          1536U
#define AUDIO_DECODE_TASK_STACK_WORDS      2048U
#define AUDIO_PLAY_TASK_STACK_WORDS        1024U
#define AUDIO_LVGL_TASK_STACK_WORDS        2048U

/*
 * 任务优先级说明：
 * - 播放任务最高，保证 DMA 续播最及时；
 * - LVGL 任务次高，确保触摸/UI 不会被长时间饿死；
 * - 解码任务再次；
 * - SD 预取最低，因为它最容易阻塞在文件 I/O 上。
 */
#define AUDIO_DECODE_TASK_PRIORITY         (configMAX_PRIORITIES - 5)
#define AUDIO_LVGL_TASK_PRIORITY           (configMAX_PRIORITIES - 4)
#define AUDIO_SD_TASK_PRIORITY             (configMAX_PRIORITIES - 3)
#define AUDIO_PLAY_TASK_PRIORITY           (configMAX_PRIORITIES - 2)

/* ================================
 * Task notification bits
 * ================================ */
#define AUDIO_PLAY_NOTIFY_PCM_READY        (1UL << 0)
#define AUDIO_PLAY_NOTIFY_DMA_DONE         (1UL << 1)
#define AUDIO_PLAY_NOTIFY_PAUSE_TOGGLE     (1UL << 2)
#define AUDIO_PLAY_NOTIFY_STOP             (1UL << 3)
#define AUDIO_PLAY_NOTIFY_ERROR            (1UL << 4)
#define AUDIO_PLAY_NOTIFY_DECODE_DONE      (1UL << 5)

#define AUDIO_SD_NOTIFY_REFILL             (1UL << 0)
#define AUDIO_SD_NOTIFY_NEXT_SONG          (1UL << 1)

/* ================================
 * Task / buffer state
 * ================================ */
typedef enum
{
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_PLAY,
    AUDIO_STATE_PAUSE,
    AUDIO_STATE_ERROR
} audio_state_t;

typedef enum
{
    AUDIO_PCM_EMPTY = 0,
    AUDIO_PCM_FILLING,
    AUDIO_PCM_READY,
    AUDIO_PCM_PLAYING
} audio_pcm_state_t;

/* ================================
 * Shared audio context
 * ================================
 *
 * 原则：
 * - 所有跨任务共享状态集中放这里；
 * - 文件系统句柄由 SD 预取任务独占使用；
 * - 压缩流 ring buffer 的读写指针由互斥锁保护；
 * - PCM block 状态由播放/解码两端协作维护。
 */
typedef struct
{
    /* ==== Hardware / file objects ==== */
    I2S_HandleTypeDef *i2s;
    FATFS *fs;
    FIL *file;

    /* ==== Shared stream buffer lock ==== */
    SemaphoreHandle_t stream_mutex;

    /* ==== File / song state ==== */
    volatile uint8_t fs_mounted;
    volatile uint8_t file_open;
    volatile int8_t requested_track_step;
    volatile uint8_t toggle_pause_request;
    char file_path[AUDIO_FILE_PATH_MAX_LEN];
    volatile uint16_t playlist_count;
    volatile uint16_t current_song_index;

    /* ==== MP3 decoder ==== */
    mp3dec_t decoder;
    mp3dec_frame_info_t frame_info;

    /* ==== Audio format info ==== */
    volatile uint32_t current_sample_rate;
    volatile uint16_t current_channels;
    volatile uint16_t current_bitrate_kbps;

    /* ==== Compressed stream ring buffer ==== */
    volatile uint32_t stream_read_index;
    volatile uint32_t stream_write_index;
    volatile uint32_t stream_level_bytes;
    volatile uint8_t stream_eof;

    /* ==== Playback pipeline state ==== */
    volatile uint8_t decode_done;
    volatile uint32_t song_generation;
    volatile audio_state_t state;

    /* ==== PCM block queue ==== */
    volatile uint8_t pcm_state[AUDIO_PCM_BLOCK_COUNT];
    volatile uint16_t pcm_sample_count[AUDIO_PCM_BLOCK_COUNT];
    volatile uint8_t dma_active_block;
    volatile uint8_t dma_idle;

    /* ==== Statistics ==== */
    volatile uint32_t decoded_frame_count;
    volatile uint32_t sd_read_bytes;
    volatile uint32_t sd_refill_count;
    volatile uint32_t dma_complete_count;
    volatile uint32_t underrun_count;
    volatile uint32_t low_buffer_count;
    volatile uint32_t decode_skip_count;

    /* ==== Last error ==== */
    volatile FRESULT last_fres;

    /* ==== Task handles ==== */
    TaskHandle_t sd_task_handle;
    TaskHandle_t decode_task_handle;
    TaskHandle_t play_task_handle;
    TaskHandle_t lvgl_task_handle;
} audio_context_t;

/* ================================
 * Shared object access
 * ================================ */
audio_context_t *AudioApp_GetContext(void);
uint8_t *AudioApp_GetStreamBuffer(void);
uint8_t *AudioApp_GetLinearViewBuffer(void);
int16_t *AudioApp_GetPcmBlock(uint8_t index);

/* ================================
 * Common state / helper functions
 * ================================ */
void AudioApp_ResetPipeline(audio_context_t *ctx);
void AudioApp_ResetSongState(audio_context_t *ctx);
void AudioApp_FlushForSongSwitch(audio_context_t *ctx);
FRESULT AudioApp_CloseFile(audio_context_t *ctx);
void AudioApp_StopDma(audio_context_t *ctx);

FRESULT AudioApp_FindFirstMp3(audio_context_t *ctx, const char *path);
FRESULT AudioApp_FindNextMp3(audio_context_t *ctx, const char *path);
FRESULT AudioApp_FindPreviousMp3(audio_context_t *ctx, const char *path);

uint32_t AudioApp_StreamGetLevel(const audio_context_t *ctx);
uint32_t AudioApp_StreamGetFree(const audio_context_t *ctx);
uint32_t AudioApp_StreamGetContiguousRead(const audio_context_t *ctx);
uint32_t AudioApp_StreamGetContiguousWrite(const audio_context_t *ctx);
void AudioApp_StreamConsume(audio_context_t *ctx, uint32_t bytes);
void AudioApp_StreamCommitWrite(audio_context_t *ctx, uint32_t bytes);
uint32_t AudioApp_StreamLinearizeForDecode(audio_context_t *ctx);

uint8_t AudioApp_PcmReadyCount(const audio_context_t *ctx);
uint8_t AudioApp_PcmBusyCount(const audio_context_t *ctx);
int AudioApp_FindNextReadyBlock(const audio_context_t *ctx, uint8_t after_index);
int AudioApp_ReserveEmptyBlock(audio_context_t *ctx);
void AudioApp_ReleaseBlock(audio_context_t *ctx, uint8_t index);
void AudioApp_PublishBlock(audio_context_t *ctx, uint8_t index, uint16_t sample_count);

const char *AudioApp_StateString(audio_state_t state);

void AudioApp_CreateTasks(void);

/* ================================
 * 4 tasks
 * ================================ */
void AudioSdTask(void *argument);
void AudioDecodeTask(void *argument);
void AudioPlayTask(void *argument);
void AudioLvglTask(void *argument);

#endif /* SRC_AUDIO_SHARED_H */
