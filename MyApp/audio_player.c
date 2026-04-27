#include "audio_player.h"
#include "app_memory.h"

#include <stdio.h>
#include <string.h>

#define MP3_REFILL_THRESHOLD      ((int)(AUDIO_PLAYER_STREAM_BUF_SZ / 2U))
#define MP3_READ_CHUNK            (2U * 1024U)
#define MP3_DECODE_SEARCH_LIMIT   256
#define AUDIO_PCM_EMPTY           0U
#define AUDIO_PCM_FILLING         1U
#define AUDIO_PCM_READY           2U
#define AUDIO_PCM_PLAYING         3U
#define AUDIO_PROCESS_FILL_BUDGET AUDIO_PLAYER_PCM_BUF_COUNT

static audio_player_t *s_active_player;

static uint8_t *stream_buf(audio_player_t *player)
{
    return player->workmem->stream_buf;
}


static char (*playlist(audio_player_t *player))[AUDIO_PLAYER_MAX_PATH_LEN]
{
    return player->workmem->playlist;
}

static const char (*playlist_const(const audio_player_t *player))[AUDIO_PLAYER_MAX_PATH_LEN]
{
    return player->workmem->playlist;
}

static int16_t *pcm_buf(audio_player_t *player, uint8_t index)
{
    return player->dmabuf->pcm_buf[index];
}

static uint32_t irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void irq_restore(uint32_t primask)
{
    if (primask == 0U) {
        __enable_irq();
    }
}

static int ascii_tolower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch + ('a' - 'A');
    }
    return ch;
}

static int str_case_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (ascii_tolower((unsigned char)*a) != ascii_tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int has_ext(const char *name, const char *ext)
{
    const char *p = strrchr(name, '.');
    return (p != NULL) && str_case_equal(p, ext);
}

static int is_audio_file(const char *name)
{
    return has_ext(name, ".mp3") || has_ext(name, ".wav");
}

static audio_format_t detect_format(const char *path)
{
    if (has_ext(path, ".mp3")) {
        return AUDIO_FORMAT_MP3;
    }
    if (has_ext(path, ".wav")) {
        return AUDIO_FORMAT_WAV;
    }
    return AUDIO_FORMAT_UNKNOWN;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void clear_pcm_states(audio_player_t *player)
{
    uint8_t i;

    for (i = 0; i < AUDIO_PLAYER_PCM_BUF_COUNT; i++) {
        player->pcm_state[i] = AUDIO_PCM_EMPTY;
        player->pcm_len[i] = 0;
    }
}

static void runtime_reset(audio_player_t *player)
{
    player->format = AUDIO_FORMAT_UNKNOWN;
    player->state = AUDIO_PLAYER_STOPPED;
    clear_pcm_states(player);
    player->active_buf = 0;
    player->dma_idle = 1;
    player->mp3_data_pos = 0;
    player->mp3_data_remain = 0;
    player->mp3_eof = 0;
    player->channels = 0;
    player->sample_rate = 0;
    player->bits_per_sample = 0;
    memset(&player->mp3_info, 0, sizeof(player->mp3_info));
    memset(&player->wav, 0, sizeof(player->wav));
}

static FRESULT close_file(audio_player_t *player)
{
    if (player->file_open) {
        player->file_open = 0;
        return f_close(&player->file);
    }
    return FR_OK;
}

static void stop_dma(audio_player_t *player)
{
    if (player->i2s != NULL) {
        HAL_I2S_DMAStop(player->i2s);
    }
    player->dma_idle = 1;
    clear_pcm_states(player);
}

static uint8_t pcm_active_or_ready_count(const audio_player_t *player)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < AUDIO_PLAYER_PCM_BUF_COUNT; i++) {
        if (player->pcm_state[i] == AUDIO_PCM_READY ||
            player->pcm_state[i] == AUDIO_PCM_PLAYING ||
            player->pcm_state[i] == AUDIO_PCM_FILLING) {
            count++;
        }
    }

    return count;
}

static uint8_t pcm_ready_count(const audio_player_t *player)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < AUDIO_PLAYER_PCM_BUF_COUNT; i++) {
        if (player->pcm_state[i] == AUDIO_PCM_READY) {
            count++;
        }
    }

    return count;
}

static int find_ready_buffer_from(const audio_player_t *player, uint8_t after)
{
    uint8_t n;

    for (n = 1; n <= AUDIO_PLAYER_PCM_BUF_COUNT; n++) {
        uint8_t idx = (uint8_t)((after + n) % AUDIO_PLAYER_PCM_BUF_COUNT);
        if (player->pcm_state[idx] == AUDIO_PCM_READY && player->pcm_len[idx] != 0U) {
            return (int)idx;
        }
    }

    return -1;
}

static int reserve_empty_buffer(audio_player_t *player)
{
    uint8_t n;
    uint8_t start;
    uint32_t primask;
    int result = -1;

    start = player->dma_idle ? 0U : (uint8_t)((player->active_buf + 1U) % AUDIO_PLAYER_PCM_BUF_COUNT);

    primask = irq_save();
    for (n = 0; n < AUDIO_PLAYER_PCM_BUF_COUNT; n++) {
        uint8_t idx = (uint8_t)((start + n) % AUDIO_PLAYER_PCM_BUF_COUNT);
        if (player->pcm_state[idx] == AUDIO_PCM_EMPTY) {
            player->pcm_state[idx] = AUDIO_PCM_FILLING;
            result = (int)idx;
            break;
        }
    }
    irq_restore(primask);

    return result;
}

static void publish_buffer(audio_player_t *player, uint8_t idx)
{
    uint32_t primask = irq_save();
    if (player->pcm_len[idx] != 0U) {
        player->pcm_state[idx] = AUDIO_PCM_READY;
    } else {
        player->pcm_state[idx] = AUDIO_PCM_EMPTY;
    }
    irq_restore(primask);
}

static void release_reserved_buffer(audio_player_t *player, uint8_t idx)
{
    uint32_t primask = irq_save();
    player->pcm_len[idx] = 0;
    player->pcm_state[idx] = AUDIO_PCM_EMPTY;
    irq_restore(primask);
}

static FRESULT parse_wav_header(audio_player_t *player)
{
    uint8_t hdr[12];
    UINT br = 0;
    FRESULT res = f_read(&player->file, hdr, sizeof(hdr), &br);
    if (res != FR_OK || br != sizeof(hdr)) {
        return (res == FR_OK) ? FR_INVALID_OBJECT : res;
    }

    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(&hdr[8], "WAVE", 4) != 0) {
        return FR_INVALID_OBJECT;
    }

    while (!f_eof(&player->file)) {
        uint8_t chunk[8];
        uint32_t chunk_size;
        uint32_t data_pos;

        br = 0;
        res = f_read(&player->file, chunk, sizeof(chunk), &br);
        if (res != FR_OK || br != sizeof(chunk)) {
            return (res == FR_OK) ? FR_INVALID_OBJECT : res;
        }

        chunk_size = le32(&chunk[4]);
        data_pos = f_tell(&player->file);

        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk_size < sizeof(fmt)) {
                return FR_INVALID_OBJECT;
            }

            br = 0;
            res = f_read(&player->file, fmt, sizeof(fmt), &br);
            if (res != FR_OK || br != sizeof(fmt)) {
                return (res == FR_OK) ? FR_INVALID_OBJECT : res;
            }

            player->wav.audio_format = le16(&fmt[0]);
            player->wav.channels = le16(&fmt[2]);
            player->wav.sample_rate = le32(&fmt[4]);
            player->wav.bits_per_sample = le16(&fmt[14]);

            data_pos += chunk_size + (chunk_size & 1U);
            res = f_lseek(&player->file, data_pos);
            if (res != FR_OK) {
                return res;
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            player->wav.data_start = data_pos;
            player->wav.data_size = chunk_size;
            player->wav.data_left = chunk_size;
            break;
        } else {
            data_pos += chunk_size + (chunk_size & 1U);
            res = f_lseek(&player->file, data_pos);
            if (res != FR_OK) {
                return res;
            }
        }
    }

    if (player->wav.audio_format != 1U ||
        player->wav.bits_per_sample != 16U ||
        (player->wav.channels != 1U && player->wav.channels != 2U) ||
        player->wav.data_size == 0U) {
        return FR_INVALID_OBJECT;
    }

    player->channels = player->wav.channels;
    player->sample_rate = player->wav.sample_rate;
    player->bits_per_sample = player->wav.bits_per_sample;
    return f_lseek(&player->file, player->wav.data_start);
}

static FRESULT mp3_refill(audio_player_t *player)
{
    int tail_free;
    int to_read;
    UINT br = 0;
    FRESULT res;
    uint8_t *buf = stream_buf(player);

    if (player->mp3_eof || player->mp3_data_remain >= MP3_REFILL_THRESHOLD) {
        return FR_OK;
    }

    if (player->mp3_data_pos > (int)(AUDIO_PLAYER_STREAM_BUF_SZ / 2U)) {
        if (player->mp3_data_remain > 0) {
            memmove(buf, buf + player->mp3_data_pos, (size_t)player->mp3_data_remain);
        }
        player->mp3_data_pos = 0;
    }

    tail_free = (int)AUDIO_PLAYER_STREAM_BUF_SZ -
                (player->mp3_data_pos + player->mp3_data_remain);

    if (tail_free < (int)MP3_READ_CHUNK) {
        if (player->mp3_data_remain > 0) {
            memmove(buf, buf + player->mp3_data_pos, (size_t)player->mp3_data_remain);
        }
        player->mp3_data_pos = 0;
        tail_free = (int)AUDIO_PLAYER_STREAM_BUF_SZ - player->mp3_data_remain;
    }

    to_read = tail_free;
    if (to_read > (int)MP3_READ_CHUNK) {
        to_read = (int)MP3_READ_CHUNK;
    }
    to_read &= ~511;

    if (to_read <= 0) {
        return FR_OK;
    }

    res = f_read(&player->file,
                 buf + player->mp3_data_pos + player->mp3_data_remain,
                 (UINT)to_read,
                 &br);
    if (res != FR_OK) {
        return res;
    }

    if (br == 0U) {
        player->mp3_eof = 1;
    } else {
        player->mp3_data_remain += (int)br;
    }

    return FR_OK;
}

static void mono_to_stereo(int16_t *buf, int samples)
{
    int i;
    for (i = samples - 1; i >= 0; i--) {
        int16_t s = buf[i];
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
    }
}

static int decode_mp3_to_buffer(audio_player_t *player, uint8_t buf_index)
{
    int guard = 0;
    uint8_t *buf = stream_buf(player);
    int16_t *pcm = pcm_buf(player, buf_index);

    while (guard++ < MP3_DECODE_SEARCH_LIMIT) {
        int samples;
        FRESULT res = mp3_refill(player);
        if (res != FR_OK) {
            player->state = AUDIO_PLAYER_ERROR;
            return 0;
        }

        if (player->mp3_data_remain <= 0) {
            return 0;
        }

        samples = mp3dec_decode_frame(&player->mp3d,
                                      buf + player->mp3_data_pos,
                                      player->mp3_data_remain,
                                      pcm,
                                      &player->mp3_info);

        if (player->mp3_info.frame_bytes > 0) {
            player->mp3_data_pos += player->mp3_info.frame_bytes;
            player->mp3_data_remain -= player->mp3_info.frame_bytes;
        }

        if (samples > 0 && player->mp3_info.frame_bytes > 0) {
            player->channels = (uint16_t)player->mp3_info.channels;
            player->sample_rate = (uint32_t)player->mp3_info.hz;
            player->bits_per_sample = 16;

            if (player->channels == 1U) {
                mono_to_stereo(pcm, samples);
                player->pcm_len[buf_index] = (uint16_t)(samples * 2);
            } else {
                player->pcm_len[buf_index] =
                    (uint16_t)(samples * player->mp3_info.channels);
            }

            return 1;
        }

        if (player->mp3_info.frame_bytes <= 0) {
            player->mp3_data_pos++;
            player->mp3_data_remain--;
        }
    }

    if (player->mp3_eof) {
        player->mp3_data_pos = 0;
        player->mp3_data_remain = 0;
    }

    return 0;
}

static int read_wav_to_buffer(audio_player_t *player, uint8_t buf_index)
{
    UINT bytes_to_read;
    UINT br = 0;
    FRESULT res;
    int16_t *pcm = pcm_buf(player, buf_index);

    if (player->wav.data_left == 0U) {
        return 0;
    }

    if (player->wav.channels == 1U) {
        bytes_to_read = (UINT)((AUDIO_PLAYER_PCM_SAMPLES / 2U) * sizeof(int16_t));
    } else {
        bytes_to_read = (UINT)(AUDIO_PLAYER_PCM_SAMPLES * sizeof(int16_t));
    }

    if (bytes_to_read > player->wav.data_left) {
        bytes_to_read = (UINT)player->wav.data_left;
    }
    bytes_to_read &= ~1U;
    if (bytes_to_read == 0U) {
        return 0;
    }

    res = f_read(&player->file, pcm, bytes_to_read, &br);
    if (res != FR_OK) {
        player->state = AUDIO_PLAYER_ERROR;
        return 0;
    }

    player->wav.data_left -= br;
    if (br == 0U) {
        return 0;
    }

    if (player->wav.channels == 1U) {
        int samples = (int)(br / sizeof(int16_t));
        mono_to_stereo(pcm, samples);
        player->pcm_len[buf_index] = (uint16_t)(samples * 2);
    } else {
        player->pcm_len[buf_index] = (uint16_t)(br / sizeof(int16_t));
    }

    return 1;
}

static int fill_reserved_buffer(audio_player_t *player, uint8_t buf_index)
{
    int ok = 0;

    player->pcm_len[buf_index] = 0;

    if (player->format == AUDIO_FORMAT_MP3) {
        ok = decode_mp3_to_buffer(player, buf_index);
    } else if (player->format == AUDIO_FORMAT_WAV) {
        ok = read_wav_to_buffer(player, buf_index);
    }

    if (ok && player->pcm_len[buf_index] != 0U) {
        publish_buffer(player, buf_index);
        return 1;
    }

    release_reserved_buffer(player, buf_index);
    return 0;
}

static int fill_one_free_buffer(audio_player_t *player)
{
    int idx = reserve_empty_buffer(player);
    if (idx < 0) {
        return 0;
    }
    return fill_reserved_buffer(player, (uint8_t)idx);
}

static HAL_StatusTypeDef start_dma_buffer(audio_player_t *player, uint8_t buf_index)
{
    HAL_StatusTypeDef status;
    uint32_t primask;

    if (player->pcm_state[buf_index] != AUDIO_PCM_READY || player->pcm_len[buf_index] == 0U) {
        return HAL_ERROR;
    }

    primask = irq_save();
    player->pcm_state[buf_index] = AUDIO_PCM_PLAYING;
    player->active_buf = buf_index;
    player->dma_idle = 0;
    irq_restore(primask);

    status = HAL_I2S_Transmit_DMA(player->i2s,
                                  (uint16_t *)pcm_buf(player, buf_index),
                                  player->pcm_len[buf_index]);
    if (status != HAL_OK) {
        primask = irq_save();
        player->pcm_state[buf_index] = AUDIO_PCM_READY;
        player->state = AUDIO_PLAYER_ERROR;
        player->dma_idle = 1;
        irq_restore(primask);
    }

    return status;
}

static void start_ready_if_idle(audio_player_t *player)
{
    int idx;

    if (!player->dma_idle || player->state != AUDIO_PLAYER_PLAYING) {
        return;
    }

    idx = find_ready_buffer_from(player, player->active_buf);
    if (idx >= 0) {
        (void)start_dma_buffer(player, (uint8_t)idx);
    }
}

static int stream_finished(const audio_player_t *player)
{
    if (player->format == AUDIO_FORMAT_MP3) {
        return (player->mp3_eof != 0U) && (player->mp3_data_remain <= 0);
    }
    if (player->format == AUDIO_FORMAT_WAV) {
        return player->wav.data_left == 0U;
    }
    return 1;
}

static void finish_or_next(audio_player_t *player)
{
    if (player->playlist_len > 0U) {
        AudioPlayer_Next(player);
    } else {
        AudioPlayer_Stop(player);
        player->state = AUDIO_PLAYER_FINISHED;
    }
}

static void apply_i2s_sample_rate(audio_player_t *player)
{
    uint32_t hz = player->sample_rate;

    if (player->i2s == NULL || hz == 0U) {
        return;
    }

    if (player->i2s->Init.AudioFreq == hz) {
        return;
    }

    if (hz < I2S_AUDIOFREQ_8K || hz > I2S_AUDIOFREQ_192K) {
        return;
    }

    HAL_I2S_DeInit(player->i2s);
    player->i2s->Init.AudioFreq = hz;
    if (HAL_I2S_Init(player->i2s) != HAL_OK) {
        player->state = AUDIO_PLAYER_ERROR;
    }
}

static FRESULT open_current(audio_player_t *player)
{
    FRESULT res;
    UINT br = 0;
    char (*pl)[AUDIO_PLAYER_MAX_PATH_LEN];
    uint8_t *buf;

    if (player->workmem == NULL || player->dmabuf == NULL ||
        APP_PTR_IN_CCMRAM(player->dmabuf) || !APP_PTR_IN_DMA_RAM(player->dmabuf)) {
        player->state = AUDIO_PLAYER_ERROR;
        return FR_INVALID_OBJECT;
    }

    pl = playlist(player);
    buf = stream_buf(player);

    if (player->playlist_len == 0U) {
        player->state = AUDIO_PLAYER_STOPPED;
        return FR_NO_FILE;
    }

    stop_dma(player);
    close_file(player);
    runtime_reset(player);

    player->format = detect_format(pl[player->current_index]);
    if (player->format == AUDIO_FORMAT_UNKNOWN) {
        player->state = AUDIO_PLAYER_ERROR;
        return FR_INVALID_NAME;
    }

    res = f_open(&player->file, pl[player->current_index], FA_READ);
    if (res != FR_OK) {
        player->state = AUDIO_PLAYER_ERROR;
        return res;
    }
    player->file_open = 1;

    if (player->format == AUDIO_FORMAT_WAV) {
        res = parse_wav_header(player);
        if (res != FR_OK) {
            player->state = AUDIO_PLAYER_ERROR;
            return res;
        }
    } else {
        mp3dec_init(&player->mp3d);
        res = f_read(&player->file, buf, AUDIO_PLAYER_STREAM_BUF_SZ, &br);
        if (res != FR_OK) {
            player->state = AUDIO_PLAYER_ERROR;
            return res;
        }
        player->mp3_data_remain = (int)br;
        player->mp3_eof = (br == 0U);
    }

    player->state = AUDIO_PLAYER_PLAYING;

    if (fill_one_free_buffer(player)) {
        apply_i2s_sample_rate(player);
        start_ready_if_idle(player);
        AudioPlayer_Process(player);
    } else if (player->state == AUDIO_PLAYER_PLAYING) {
        player->state = AUDIO_PLAYER_FINISHED;
    }

    return FR_OK;
}

void AudioPlayer_Init(audio_player_t *player,
                      I2S_HandleTypeDef *i2s,
                      audio_player_workmem_t *workmem,
                      audio_player_dma_buffers_t *dmabuf)
{
    memset(player, 0, sizeof(*player));
    player->i2s = i2s;
    player->workmem = workmem;
    player->dmabuf = dmabuf;
    player->dma_idle = 1;
    player->state = AUDIO_PLAYER_STOPPED;

    if (workmem == NULL || dmabuf == NULL || APP_PTR_IN_CCMRAM(dmabuf) || !APP_PTR_IN_DMA_RAM(dmabuf)) {
        player->state = AUDIO_PLAYER_ERROR;
    }

    s_active_player = player;
}

FRESULT AudioPlayer_BuildPlaylist(audio_player_t *player, const char *path)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;
    char (*pl)[AUDIO_PLAYER_MAX_PATH_LEN];

    if (player->workmem == NULL) {
        return FR_INVALID_OBJECT;
    }

    pl = playlist(player);
    player->playlist_len = 0;
    player->current_index = 0;

    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return res;
    }

    for (;;) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == '\0') {
            break;
        }

        if (fno.fname[0] == '.' || (fno.fattrib & AM_DIR) != 0U) {
            continue;
        }

        if (!is_audio_file(fno.fname)) {
            continue;
        }

        if (player->playlist_len < AUDIO_PLAYER_MAX_SONGS) {
            int written = snprintf(pl[player->playlist_len],
                                   AUDIO_PLAYER_MAX_PATH_LEN,
                                   "%s/%s",
                                   path,
                                   fno.fname);
            if (written > 0 && written < (int)AUDIO_PLAYER_MAX_PATH_LEN) {
                player->playlist_len++;
            }
        }
    }

    f_closedir(&dir);
    return res;
}

FRESULT AudioPlayer_Start(audio_player_t *player)
{
    player->current_index = 0;
    return open_current(player);
}

FRESULT AudioPlayer_PlayIndex(audio_player_t *player, uint16_t index)
{
    if (index >= player->playlist_len) {
        return FR_INVALID_PARAMETER;
    }
    player->current_index = index;
    return open_current(player);
}

void AudioPlayer_Process(audio_player_t *player)
{
    uint8_t fills = 0;

    if (player->state != AUDIO_PLAYER_PLAYING) {
        return;
    }

    start_ready_if_idle(player);

    while (fills < AUDIO_PROCESS_FILL_BUDGET &&
           player->state == AUDIO_PLAYER_PLAYING &&
           pcm_active_or_ready_count(player) < AUDIO_PLAYER_PCM_BUF_COUNT) {
        if (!fill_one_free_buffer(player)) {
            break;
        }
        fills++;
        start_ready_if_idle(player);
    }

    if (player->dma_idle && pcm_ready_count(player) == 0U && stream_finished(player)) {
        finish_or_next(player);
    }
}

void AudioPlayer_Stop(audio_player_t *player)
{
    stop_dma(player);
    close_file(player);
    runtime_reset(player);
}

void AudioPlayer_Next(audio_player_t *player)
{
    if (player->playlist_len == 0U) {
        return;
    }
    player->current_index = (uint16_t)((player->current_index + 1U) % player->playlist_len);
    open_current(player);
}

void AudioPlayer_Previous(audio_player_t *player)
{
    if (player->playlist_len == 0U) {
        return;
    }
    if (player->current_index == 0U) {
        player->current_index = player->playlist_len - 1U;
    } else {
        player->current_index--;
    }
    open_current(player);
}

void AudioPlayer_Pause(audio_player_t *player)
{
    if (player->state == AUDIO_PLAYER_PLAYING) {
        HAL_I2S_DMAPause(player->i2s);
        player->state = AUDIO_PLAYER_PAUSED;
    }
}

void AudioPlayer_Resume(audio_player_t *player)
{
    if (player->state == AUDIO_PLAYER_PAUSED) {
        HAL_I2S_DMAResume(player->i2s);
        player->state = AUDIO_PLAYER_PLAYING;
    }
}

void AudioPlayer_TogglePause(audio_player_t *player)
{
    if (player->state == AUDIO_PLAYER_PLAYING) {
        AudioPlayer_Pause(player);
    } else if (player->state == AUDIO_PLAYER_PAUSED) {
        AudioPlayer_Resume(player);
    } else if (player->playlist_len > 0U) {
        open_current(player);
    }
}

const char *AudioPlayer_GetCurrentPath(const audio_player_t *player)
{
    const char (*pl)[AUDIO_PLAYER_MAX_PATH_LEN];

    if (player->playlist_len == 0U || player->current_index >= player->playlist_len || player->workmem == NULL) {
        return "";
    }

    pl = playlist_const(player);
    return pl[player->current_index];
}

const char *AudioPlayer_GetCurrentName(const audio_player_t *player)
{
    const char *path = AudioPlayer_GetCurrentPath(player);
    const char *slash = strrchr(path, '/');
    return (slash != NULL) ? slash + 1 : path;
}

const char *AudioPlayer_FormatName(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_MP3:
        return "MP3";
    case AUDIO_FORMAT_WAV:
        return "WAV";
    default:
        return "UNKNOWN";
    }
}

const char *AudioPlayer_StateName(audio_player_state_t state)
{
    switch (state) {
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

static uint32_t take_counter(volatile uint32_t *counter)
{
    uint32_t primask = irq_save();
    uint32_t value = *counter;

    *counter = 0;
    irq_restore(primask);

    return value;
}

uint32_t AudioPlayer_TakeDmaCpltCount(audio_player_t *player)
{
    return take_counter(&player->dma_cplt_count);
}

uint32_t AudioPlayer_TakeUnderflowCount(audio_player_t *player)
{
    return take_counter(&player->underflow_count);
}

uint8_t AudioPlayer_GetPcmBufferedCount(const audio_player_t *player)
{
    return pcm_active_or_ready_count(player);
}

uint8_t AudioPlayer_IsBufferLow(const audio_player_t *player)
{
    if (player->state != AUDIO_PLAYER_PLAYING) {
        return 0;
    }
    return AudioPlayer_GetPcmBufferedCount(player) <= 1U;
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    audio_player_t *player = s_active_player;
    uint8_t done;
    int next;

    if (player == NULL || hi2s != player->i2s) {
        return;
    }

    done = player->active_buf;
    player->dma_cplt_count++;
    player->pcm_len[done] = 0;
    player->pcm_state[done] = AUDIO_PCM_EMPTY;

    next = find_ready_buffer_from(player, done);
    if (player->state == AUDIO_PLAYER_PLAYING && next >= 0) {
        (void)start_dma_buffer(player, (uint8_t)next);
    } else {
        player->active_buf = (uint8_t)((done + 1U) % AUDIO_PLAYER_PCM_BUF_COUNT);
        player->dma_idle = 1;
        if (player->state == AUDIO_PLAYER_PLAYING && !stream_finished(player)) {
            player->underflow_count++;
        }
    }
}
