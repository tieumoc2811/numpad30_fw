#ifndef CONFIG_H_
#define CONFIG_H_
#include <stdint.h>
#include <stdbool.h>
#include "keymap.h"

#define NMACRO 16
#define MSTEP  12
#define NTD    4
#define NCOMBO 4
#define NSEQ   4
#define SEQLEN 8

// op: 0 kết thúc, 1 nhấn+nhả, 2 giữ xuống, 3 nhả, 4 chờ (key x 10 ms)
typedef struct __attribute__((packed)) { uint8_t op, mod, key; } mstep_t;
typedef struct __attribute__((packed)) { action_t tap, hold, dtap; } td_t;
typedef struct __attribute__((packed)) { uint8_t a, b; action_t out; } combo_t;   // a,b = r*NCOL+c, 255 = chưa chọn

typedef struct __attribute__((packed)) {
    mstep_t  macro[NMACRO][MSTEP];   // offset 0
    td_t     td[NTD];                // 576
    combo_t  combo[NCOMBO];          // 624
    uint16_t note[3][NROW * NCOL];   // 648  3 bảng: 0 piano (Hz), 1 trống (id 0..12), 2 guitar (Hz)
    uint16_t term_ms;                // 858  (ngưỡng Tap Dance)
    uint8_t  board;                  // 860  bảng đang dùng
    uint8_t  volume;                 // 861  0..100
    uint8_t  bright;                 // 862  độ sáng OLED 0..255
    uint8_t  sleep_min;              // 863  tự tắt màn hình sau n phút (0 = không)
    uint8_t  flags;                  // 864  bit0 xoay 180°, bit1 logo khởi động, bit2 lời chào, bit3 TẮT tiếng gõ phím (mặc định bật)
    uint8_t  air[8];                 // 865  chuột bay (xem air.c)
    char     greet[24];              // 873  lời chào chạy chữ (kết thúc bằng 0)
    char     lname[NLAYER][8];       // 897  tên layer (tối đa 7 ký tự)
    uint8_t  nl;                     // 961  số layer đang dùng (2..NLAYER)
    action_t seq[NSEQ][SEQLEN];      // 962  Sequence (chạy tới bước trống đầu tiên)
    uint8_t  ss_kind;                // 1090 màn chờ: 0 tắt màn hình, 1 logo nảy, 2 sao bay
    uint16_t timer_s;                // 1091 hẹn giờ (giây)
    uint8_t  midi_on;                // 1093 gửi USB-MIDI khi ở lớp nhạc cụ
    uint8_t  midi_local;             // 1094 vẫn phát tiếng trên loa khi đang gửi MIDI
    uint8_t  midi_ch;                // 1095 kênh MIDI 0..15
    int8_t   midi_tr;                // 1096 dịch quãng (bán cung)
    uint8_t  midi_vel;               // 1097 độ mạnh 1..127
    uint8_t  song_tempo;             // 1098 tốc độ phát bài (%)
    uint8_t  song_loop;              // 1099 lặp bài
    uint32_t hiscore;                // 1100 điểm cao Tetris (app chỉ đọc)
} extra_t;                           // tổng 1104 byte
_Static_assert(sizeof(extra_t) == 1104, "extra_t layout");

extern action_t km[NLAYER][NROW][NCOL];   // 0 = nền, 1 = FN, 2.. = layer tự thêm
extern extra_t  ex;

void cfg_init(void);
void cfg_task(void);
void cfg_on_raw(const uint8_t *buf, uint16_t len);
uint16_t cfg_note(uint8_t idx);   // giá trị của phím idx ở bảng đang dùng
uint8_t  cfg_board(void);
const char *cfg_layer_name(int l);
void     cfg_next_board(void);
void     cfg_set_hiscore(uint32_t v);
void     cfg_key_event(int r, int c, bool down);   // báo phím cho app (chế độ thử)
bool     cfg_take_changed(void);    // true một lần sau khi app đổi cấu hình

void macro_start(uint8_t id);
bool macro_task(uint32_t now_ms);                         // true nếu báo cáo phím đổi
int  macro_out(uint8_t *mods, uint8_t *keys, int n);      // trộn phím macro vào báo cáo
#endif
