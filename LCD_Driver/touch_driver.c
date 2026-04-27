#include "main.h"
#include "touch_driver.h"

extern I2C_HandleTypeDef hi2c1;

/* FT6336U 7-bit address is 0x38. HAL APIs use the left-shifted address. */
#define FT6336U_ADDR             0x70U
#define FT6336U_REG_TD_STATUS    0x02U
#define FT6336U_REG_CHIP_ID      0xA3U
#define FT6336U_I2C_TIMEOUT_MS   5U
#define FT6336U_PROBE_TIMEOUT_MS 5U
#define FT6336U_RETRY_PERIOD_MS  1000U

static uint8_t s_ready;
static uint8_t s_chip_id;
static uint8_t s_last_status = HAL_ERROR;
static uint8_t s_last_points;
static int16_t s_last_x;
static int16_t s_last_y;
static uint32_t s_last_probe_tick;
static uint32_t s_read_ok_count;
static uint32_t s_read_fail_count;

static void touch_mark_fail(HAL_StatusTypeDef status)
{
    s_ready = 0U;
    s_last_status = (uint8_t)status;
    s_last_points = 0U;
    s_read_fail_count++;
}

static void touch_probe(uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    uint8_t id = 0U;

    s_last_probe_tick = HAL_GetTick();
    status = HAL_I2C_IsDeviceReady(&hi2c1, FT6336U_ADDR, 1U, timeout_ms);
    if (status != HAL_OK) {
        touch_mark_fail(status);
        return;
    }

    status = HAL_I2C_Mem_Read(&hi2c1,
                              FT6336U_ADDR,
                              FT6336U_REG_CHIP_ID,
                              I2C_MEMADD_SIZE_8BIT,
                              &id,
                              1U,
                              timeout_ms);
    if (status != HAL_OK) {
        touch_mark_fail(status);
        return;
    }

    s_ready = 1U;
    s_chip_id = id;
    s_last_status = HAL_OK;
}

void FT6336U_Init(void)
{
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100U);

    touch_probe(FT6336U_PROBE_TIMEOUT_MS);
}

uint8_t FT6336U_Scan(int16_t *x, int16_t *y)
{
    uint8_t buf[5] = {0};
    uint8_t points;
    uint16_t temp_x;
    uint16_t temp_y;
    HAL_StatusTypeDef status;
    uint32_t now = HAL_GetTick();

    if (x == NULL || y == NULL) {
        return 0U;
    }

    if (!s_ready) {
        if ((now - s_last_probe_tick) >= FT6336U_RETRY_PERIOD_MS) {
            touch_probe(FT6336U_PROBE_TIMEOUT_MS);
        }

        if (!s_ready) {
            return 0U;
        }
    }

    status = HAL_I2C_Mem_Read(&hi2c1,
                              FT6336U_ADDR,
                              FT6336U_REG_TD_STATUS,
                              I2C_MEMADD_SIZE_8BIT,
                              buf,
                              sizeof(buf),
                              FT6336U_I2C_TIMEOUT_MS);
    if (status != HAL_OK) {
        touch_mark_fail(status);
        return 0U;
    }

    s_read_ok_count++;
    s_last_status = HAL_OK;

    points = (uint8_t)(buf[0] & 0x0FU);
    s_last_points = points;
    if (points == 0U || points > 2U) {
        return 0U;
    }

    temp_x = (uint16_t)(((uint16_t)(buf[1] & 0x0FU) << 8) | buf[2]);
    temp_y = (uint16_t)(((uint16_t)(buf[3] & 0x0FU) << 8) | buf[4]);

    s_last_x = (int16_t)temp_x;
    s_last_y = (int16_t)temp_y;
    *x = s_last_x;
    *y = s_last_y;

    return 1U;
}

void FT6336U_GetDebug(ft6336u_debug_t *debug)
{
    if (debug == NULL) {
        return;
    }

    debug->ready = s_ready;
    debug->chip_id = s_chip_id;
    debug->last_status = s_last_status;
    debug->last_points = s_last_points;
    debug->last_x = s_last_x;
    debug->last_y = s_last_y;
    debug->read_ok_count = s_read_ok_count;
    debug->read_fail_count = s_read_fail_count;
}
