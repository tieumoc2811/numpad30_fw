// Chuột bay: gyro MPU-6515 (I2C1: SDA GP26, SCL GP27, địa chỉ 0x68), ±500 °/s.
// ex.air = [độ nhạy X, độ nhạy Y, vùng chết (0,1 °/s), làm mượt 0-9,
//           cờ (bit0 đảo X, bit1 đảo Y, bit2 Y theo trục gy, bit3 luôn bay), 3 dự phòng]
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "config.h"
#include "air.h"

#define MPU_ADDR 0x68
#define SDA_PIN  26
#define SCL_PIN  27

static bool ok;
static float bx, by, bz;        // độ lệch gyro khi đứng yên
static float fx, fy, rx, ry;    // lọc mượt, phần dư thập phân
static uint32_t last_ms;

static bool wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return i2c_write_blocking(i2c1, MPU_ADDR, b, 2, false) == 2;
}

static bool read_gyro(float *gx, float *gy, float *gz) {
    uint8_t reg = 0x43, d[6];
    if (i2c_write_blocking(i2c1, MPU_ADDR, &reg, 1, true) != 1) return false;
    if (i2c_read_blocking(i2c1, MPU_ADDR, d, 6, false) != 6) return false;
    *gx = (int16_t)(d[0] << 8 | d[1]) / 65.5f;
    *gy = (int16_t)(d[2] << 8 | d[3]) / 65.5f;
    *gz = (int16_t)(d[4] << 8 | d[5]) / 65.5f;
    return true;
}

void air_init(void) {
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
    if (!wr(0x6B, 0x01)) return;        // thoát chế độ ngủ, xung nhịp PLL
    wr(0x1A, 0x03);                      // lọc thông thấp ~41 Hz
    wr(0x1B, 0x08);                      // ±500 °/s
    sleep_ms(50);
    float sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int i = 0; i < 100; i++) {      // hiệu chuẩn ~0,5 s: giữ yên bàn phím lúc cắm USB
        float x, y, z;
        if (read_gyro(&x, &y, &z)) { sx += x; sy += y; sz += z; n++; }
        sleep_ms(5);
    }
    if (n < 50) return;
    bx = sx / n; by = sy / n; bz = sz / n;
    ok = true;
}

bool air_poll(uint32_t now, bool active, int *dx, int *dy) {
    *dx = *dy = 0;
    if (!ok || now - last_ms < 10) return false;
    last_ms = now;
    float gx, gy, gz;
    if (!read_gyro(&gx, &gy, &gz)) return false;
    if (!active) { fx = fy = rx = ry = 0; return false; }

    uint8_t sensx = ex.air[0], sensy = ex.air[1], flags = ex.air[4];
    float dz = ex.air[2] * 0.1f;
    float vx = gz - bz;
    float vy = (flags & 4) ? (gy - by) : (gx - bx);
    if (fabsf(vx) < dz) vx = 0;
    if (fabsf(vy) < dz) vy = 0;
    float a = 1.0f / (1 + (ex.air[3] > 9 ? 9 : ex.air[3]));
    fx += a * (vx - fx);
    fy += a * (vy - fy);
    float ox = (flags & 1) ? -fx : fx, oy = (flags & 2) ? -fy : fy;
    rx += ox * sensx / 600.0f;
    ry += oy * sensy / 600.0f;
    int ix = (int)rx, iy = (int)ry;
    rx -= ix; ry -= iy;
    *dx = ix; *dy = iy;
    return ix || iy;
}
