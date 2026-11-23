#ifndef AIR_H_
#define AIR_H_
#include <stdint.h>
#include <stdbool.h>
void air_init(void);
// Đọc gyro mỗi 10 ms. Trả true nếu có dịch chuyển mới (dx, dy tính theo điểm ảnh).
bool air_poll(uint32_t now_ms, bool active, int *dx, int *dy);
#endif
