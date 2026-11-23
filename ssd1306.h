#ifndef SSD1306_H_
#define SSD1306_H_

#include <stdint.h>
#include "hardware/i2c.h"

void oled_init(i2c_inst_t *i2c, uint sda, uint scl);
void oled_clear(void);
void oled_text(int x, int y, const char *s, int scale);
void oled_flush(void);     // yêu cầu gửi lại toàn bộ khung hình
void oled_service(void);
void oled_flush_now(void);          // gửi xong cả khung hình (chặn)
void oled_fill(int x, int y, int w, int h);
void oled_set_flip(bool flip);
void oled_set_contrast(uint8_t c);
void oled_power(bool on);
bool oled_busy(void);
void oled_clr(int x, int y, int w, int h);   // xoá điểm ảnh trong hình chữ nhật   // gọi liên tục trong vòng lặp: gửi mỗi lần 1 page

#endif
