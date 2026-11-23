#ifndef PET_H_
#define PET_H_
#include <stdint.h>
#include <stdbool.h>

// Tamagotchi trên OLED 128x64.
//  - Thú sống theo thời gian BẬT MÁY (không có RTC): tắt bàn phím = thú "đóng băng".
//  - Chế độ tương tác (FN + phím PET): các phím biến thành menu chăm sóc.
//  - Chế độ nền: bàn phím rảnh PET_IDLE_MS thì hiện thú; bấm phím bất kỳ là thoát,
//    phím đó vẫn hoạt động bình thường.
//  - Trạng thái lưu trong flash riêng (4 sector xoay vòng), KHÔNG nằm trong extra_t
//    nên không ảnh hưởng bố cục cấu hình mà app Studio đang dùng.

#define PET_IDLE_MS 20000u      // rảnh bao lâu thì thú hiện ra

void pet_init(void);                    // gọi sau cfg_init()
void pet_task(uint32_t now_ms);         // gọi liên tục trong vòng lặp chính

void pet_start(void);                   // vào chế độ tương tác (chiếm phím)
void pet_stop(void);
bool pet_active(void);                  // đang ở chế độ tương tác

bool pet_ambient_allowed(void);         // người dùng chưa tắt chế độ nền
void pet_ambient(bool on);              // main bật khi bàn phím rảnh
bool pet_showing(void);                 // tương tác hoặc nền: thú đang chiếm OLED

void pet_key(uint8_t idx, bool down);   // idx = hàng*7 + cột
void pet_typed(void);                   // báo mỗi lần gõ phím bình thường
#endif
