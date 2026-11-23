#ifndef PLAYER_H_
#define PLAYER_H_
#include <stdint.h>
#include <stdbool.h>

// Bài MIDI lưu trong flash: NSONG ô, mỗi ô 1 sector 4 KB, nằm ngay dưới sector cấu hình.
#define NSONG       4
#define SONG_SECTOR 4096
#define SONG_BASE   (2 * 1024 * 1024 - SONG_SECTOR * (1 + NSONG))

bool song_info(uint8_t slot, uint16_t *count, char *name18);   // false nếu ô trống
void player_start(uint8_t slot);
void player_stop(void);
bool player_active(void);
const char *player_name(void);
void player_task(uint32_t now_ms);
#endif
