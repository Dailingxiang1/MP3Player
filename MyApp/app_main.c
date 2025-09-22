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

volatile uint8_t dma_buf_idx = 0;       // ��ǰDMA���ŵĻ���������
volatile uint8_t pcm_ready[2] = {0};    // ÿ���������Ƿ�׼������
volatile uint8_t is_dma_idle = 1;       // DMA�Ƿ����

FRESULT res;
FIL file;
UINT bytesRead;
const char* filename = "wuya.mp3";  // �滻Ϊ����ļ���
uint8_t next_song_flag = 0;
// �ļ�ϵͳ����
FATFS fs;
// �ļ���Ϣ����
FILINFO fno;
// Ŀ¼����
DIR dir;

int current_song_index = 0;
int is_wav = 0;  // �Ƿ���WAV��ʽ

uint32_t now_time;
uint32_t err_time;
// �����б�
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
// �г�SD���е��ļ�

int is_wav_file(const char* filename)
{
    const char* ext = strrchr(filename, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".wav") == 0);
}
void list_files_in_sd_card(void) {
    FRESULT res;

    // ��SD����Ŀ¼
    res = f_opendir(&dir, "/");
    if (res != FR_OK) {
        printf("��Ŀ¼ʧ��: %d\n", res);
        return;
    }

    printf("SD���ļ��б�:\n");

    // ����Ŀ¼�е��ļ�
    for (;;) {
        res = f_readdir(&dir, &fno);  // ��ȡĿ¼��
        if (res != FR_OK || fno.fname[0] == 0) {
            break;  // ���û�и����ļ���Ŀ¼����˳�
        }

        if (fno.fname[0] == '.') {
            continue;  // ���� . �� .. Ŀ¼
        }

        if (!(fno.fattrib & AM_DIR)) {
            printf("�ļ�: %s\r\n", fno.fname);  // ��ӡ�ļ���
        } else {
            printf("Ŀ¼: %s\r\n", fno.fname);  // ��ӡĿ¼��
        }
    }

    // �ر�Ŀ¼
    f_closedir(&dir);
}
// ���� WAV �ļ�֡
void WAV_PlayLoop(void)
{
    if (is_dma_idle) {
        UINT wavRead = 0;
        f_read(&file, pcm_dma_buf[dma_buf_idx], PCM_BUF_SZ * sizeof(int16_t), &wavRead);
        if (wavRead == 0) {
            printf("WAV���Ž���\r\n");
            f_close(&file);
            return;
        }
        HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)pcm_dma_buf[dma_buf_idx], wavRead / 2);  // 2�ֽ�һ������
        dma_buf_idx ^= 1;
        is_dma_idle = 0;
    }
}
int samples = 0;
// ��ʼ������ʼ����
void MP3_StartPlayback(void)
{
    f_mount(&fs, "", 1);
    res = f_open(&file, playlist[current_song_index], FA_READ);
    if (res != FR_OK) {
        printf("����Ƶ�ļ�ʧ��\r\n");
        return;
    }

    is_wav = is_wav_file(playlist[current_song_index]);
    if (is_wav) {
        UINT discard;
        f_read(&file, mp3_buf, 44, &discard);  // ����WAVͷ44�ֽ�
        WAV_PlayLoop();  // �����״β���
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
        is_dma_idle = 1;  // ���û׼���ã�ֻ�ܵȴ���ѭ������
    }
}
void MP3_SwitchSong(void)
{
    f_close(&file);
    HAL_Delay(10);
    current_song_index = (current_song_index + 1) % playlist_len;

    res = f_open(&file, playlist[current_song_index], FA_READ);
    if (res != FR_OK) {
        printf("��ʧ��: %s\r\n", playlist[current_song_index]);
        return;
    }

    is_wav = is_wav_file(playlist[current_song_index]);
    if (is_wav) {
        UINT discard;
        f_read(&file, mp3_buf, 44, &discard);  // ����WAVͷ
        dma_buf_idx = 0;
        is_dma_idle = 1;
        WAV_PlayLoop();
        return;
    }

    // MP3��ʼ��
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
// ��ѭ�����벢���ػ�����
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
    // �����ļ�ϵͳ
    if (f_mount(&fs, "", 1) != FR_OK) {
        printf("SD������ʧ��\n");
        return 1;
    }

    // �г�SD���е��ļ�
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
// �����жϻص�����
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0) {  // ����ǰ�ť����
				next_song_flag = 1;
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);  // �л�LED״̬
    }
}
