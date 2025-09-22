#include "main.h"
#include "fatfs.h"
#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_storage_if.h"
#include "minimp3.h"
#include "i2s.h"
#include "sdio.h"

#define MP3_BUFFER_SIZE 2048
#define PCM_BUF_SZ 1152 * 2

uint8_t mp3_buf[MP3_BUFFER_SIZE];
int16_t pcm_dma_buf[2][PCM_BUF_SZ];

mp3dec_t mp3d;
mp3dec_frame_info_t info;

int mp3_data_pos = 0;
int mp3_data_remain = 0;

volatile uint8_t dma_buf_idx = 0;       // 当前DMA播放的缓冲区索引
volatile uint8_t pcm_ready[2] = {0};    // 每个缓冲区是否准备好了
volatile uint8_t is_dma_idle = 1;       // DMA是否空闲

FRESULT res;
FIL file;
UINT bytesRead;
const char* filename = "wuya.mp3";  // 替换为你的文件名
uint8_t next_song_flag = 0;
// 文件系统对象
FATFS fs;
// 文件信息对象
FILINFO fno;
// 目录对象
DIR dir;

int current_song_index = 0;
int is_wav = 0;  // 是否是WAV格式

uint32_t now_time;
uint32_t err_time;
// 播放列表
const char* playlist[] = {
//    "wuya.mp3",
//    "tanguzhi.mp3",
    "HONGCH~1.mp3",
    "TONGHU~1.mp3",
		"M50000~2.mp3",
		"M500001iYB5M0X9yvq.mp3",
		"M500003BhkKY255LvY.mp3"
};

int playlist_len = sizeof(playlist);
// 列出SD卡中的文件

int is_wav_file(const char* filename)
{
    const char* ext = strrchr(filename, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".wav") == 0);
}
void list_files_in_sd_card(void) {
    FRESULT res;

    // 打开SD卡根目录
    res = f_opendir(&dir, "/");
    if (res != FR_OK) {
        printf("打开目录失败: %d\n", res);
        return;
    }

    printf("SD卡文件列表:\n");

    // 遍历目录中的文件
    for (;;) {
        res = f_readdir(&dir, &fno);  // 读取目录项
        if (res != FR_OK || fno.fname[0] == 0) {
            break;  // 如果没有更多文件或目录项，则退出
        }

        if (fno.fname[0] == '.') {
            continue;  // 忽略 . 和 .. 目录
        }

        if (!(fno.fattrib & AM_DIR)) {
            printf("文件: %s\r\n", fno.fname);  // 打印文件名
        } else {
            printf("目录: %s\r\n", fno.fname);  // 打印目录名
        }
    }

    // 关闭目录
    f_closedir(&dir);
}
// 播放 WAV 文件帧
void WAV_PlayLoop(void)
{
    if (is_dma_idle) {
        UINT wavRead = 0;
        f_read(&file, pcm_dma_buf[dma_buf_idx], PCM_BUF_SZ * sizeof(int16_t), &wavRead);
        if (wavRead == 0) {
            printf("WAV播放结束\r\n");
            f_close(&file);
            return;
        }
        HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)pcm_dma_buf[dma_buf_idx], wavRead / 2);  // 2字节一个样本
        dma_buf_idx ^= 1;
        is_dma_idle = 0;
    }
}
int samples = 0;
// 初始化并开始播放
void MP3_StartPlayback(void)
{
    f_mount(&fs, "", 1);
    res = f_open(&file, playlist[current_song_index], FA_READ);
    if (res != FR_OK) {
        printf("打开音频文件失败\r\n");
        return;
    }

    is_wav = is_wav_file(playlist[current_song_index]);
    if (is_wav) {
        UINT discard;
        f_read(&file, mp3_buf, 44, &discard);  // 跳过WAV头44字节
        WAV_PlayLoop();  // 启动首次播放
        return;
    }

    mp3dec_init(&mp3d);
    mp3_data_pos = 0;
    mp3_data_remain = 0;
		 now_time = HAL_GetTick();
    f_read(&file, mp3_buf, MP3_BUFFER_SIZE, &bytesRead);
		uint32_t err_time =HAL_GetTick() -  now_time  ;
		printf("%d\r\n",err_time);
    mp3_data_remain = bytesRead;

     samples = mp3dec_decode_frame(&mp3d, mp3_buf, mp3_data_remain, pcm_dma_buf[0], &info);
    if (samples > 0 && info.frame_bytes > 0) {
        mp3_data_pos += info.frame_bytes;
        mp3_data_remain -= info.frame_bytes;

        pcm_ready[0] = 1;
        HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)pcm_dma_buf[0], samples * info.channels);
        is_dma_idle = 0;
    }
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    pcm_ready[dma_buf_idx] = 0;
    dma_buf_idx ^= 1;

    uint8_t next_buf = dma_buf_idx;

    if (pcm_ready[next_buf]) {
        int play_samples = PCM_BUF_SZ / info.channels;

        HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)pcm_dma_buf[next_buf], play_samples * info.channels);
        is_dma_idle = 0;
    } else {
        is_dma_idle = 1;  // 如果没准备好，只能等待主循环解码
    }
}
void MP3_SwitchSong(void)
{
    f_close(&file);
    HAL_Delay(10);
    current_song_index = (current_song_index + 1) % playlist_len;

    res = f_open(&file, playlist[current_song_index], FA_READ);
    if (res != FR_OK) {
        printf("打开失败: %s\r\n", playlist[current_song_index]);
        return;
    }

    is_wav = is_wav_file(playlist[current_song_index]);
    if (is_wav) {
        UINT discard;
        f_read(&file, mp3_buf, 44, &discard);  // 跳过WAV头
        dma_buf_idx = 0;
        is_dma_idle = 1;
        WAV_PlayLoop();
        return;
    }

    // MP3初始化
    mp3dec_init(&mp3d);
    mp3_data_pos = 0;
    mp3_data_remain = 0;
    dma_buf_idx = 0;
    pcm_ready[0] = 0;
    pcm_ready[1] = 0;
    is_dma_idle = 1;

    f_read(&file, mp3_buf, MP3_BUFFER_SIZE, &bytesRead);
    mp3_data_remain = bytesRead;

     samples = mp3dec_decode_frame(&mp3d, mp3_buf, mp3_data_remain, pcm_dma_buf[0], &info);
    if (samples > 0 && info.frame_bytes > 0) {
        mp3_data_pos += info.frame_bytes;
        mp3_data_remain -= info.frame_bytes;
        pcm_ready[0] = 1;
        HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)pcm_dma_buf[0], samples * info.channels);
        is_dma_idle = 0;
    }
}
// 主循环解码并加载缓冲区
void MP3_PlayLoop(void)
{
    if (is_wav) {
        WAV_PlayLoop();
        return;
    }

    if (mp3_data_remain < MP3_BUFFER_SIZE / 2) {
        memmove(mp3_buf, mp3_buf + mp3_data_pos, mp3_data_remain);
        mp3_data_pos = 0;
        f_read(&file, mp3_buf + mp3_data_remain, MP3_BUFFER_SIZE - mp3_data_remain, &bytesRead);
        mp3_data_remain += bytesRead;
        if (bytesRead == 0) {
						MP3_SwitchSong();
            return;
        }
    }

    uint8_t idle_buf = dma_buf_idx ^ 1;

    if (!pcm_ready[idle_buf]) {
         samples = mp3dec_decode_frame(&mp3d, mp3_buf + mp3_data_pos, mp3_data_remain, pcm_dma_buf[idle_buf], &info);
			//	printf("samples=%d,frame_bytes=%d\r\n",samples,info.frame_bytes);
        if (samples > 0 && info.frame_bytes > 0) {
            mp3_data_pos += info.frame_bytes;
            mp3_data_remain -= info.frame_bytes;
            pcm_ready[idle_buf] = 1;
        } else {
            mp3_data_pos++;
            mp3_data_remain--;
        }
    }

    if (is_dma_idle && pcm_ready[dma_buf_idx]) {
        int play_samples = PCM_BUF_SZ / info.channels;
        HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)pcm_dma_buf[dma_buf_idx], play_samples * info.channels);
        is_dma_idle = 0;
    }
}

int app_main(void)
{
    // 挂载文件系统
    if (f_mount(&fs, "", 1) != FR_OK) {
        printf("SD卡挂载失败\n");
        return 1;
    }

    // 列出SD卡中的文件
    list_files_in_sd_card();


		MP3_StartPlayback();

		
    while(1)
    {
			MP3_PlayLoop();
			if (next_song_flag) {
				next_song_flag = 0;
				MP3_SwitchSong();
			}
    
		}
		return 0;
}
// 按键中断回调函数
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0) {  // 如果是按钮按下
				next_song_flag = 1;
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);  // 切换LED状态
    }
}
