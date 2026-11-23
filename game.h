#ifndef GAME_H_
#define GAME_H_
#include <stdint.h>
#include <stdbool.h>
// Tetris trên OLED 128x64: bàn 10x20, ô 3 px, bên phải là NEXT / HOLD / điểm.
void game_start(void);
void game_stop(void);
bool game_active(void);
void game_key(uint8_t idx, bool down);   // idx = hàng*7 + cột
void game_task(uint32_t now_ms);          // gọi liên tục; tự vẽ khi OLED rảnh
#endif
