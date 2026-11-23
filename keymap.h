#ifndef KEYMAP_H_
#define KEYMAP_H_

#include <stdint.h>
#include "tusb.h"

#define NROW 5
#define NCOL 7
#define NLAYER 8

// Vị trí phím Num (giữ = lớp FN, bấm nhanh = NumLock)
#define NUM_R 0
#define NUM_C 3

typedef enum {
    ACT_NONE = 0,   // không làm gì (và là giá trị "chưa nhấn")
    ACT_TRANS,      // lớp FN: dùng phím của lớp nền
    ACT_KEY,        // phím HID (kèm modifier)
    ACT_CONSUMER,   // phím media
    ACT_FN,         // phím Num
    ACT_MUSIC,      // bật/tắt lớp nhạc cụ
    ACT_BOOT,       // vào chế độ nạp firmware (BOOTSEL)
    ACT_NOTE,       // (nội bộ) phím nhạc, code = r*NCOL + c
    ACT_MACRO,      // chạy macro số code (0..15)
    ACT_TD,         // Tap Dance số code (0..3)
    ACT_BOARD,      // đổi bảng nhạc cụ: Piano -> Trống -> Guitar
    ACT_AIR,        // bật/tắt chuột bay
    ACT_AIRH,       // chuột bay khi đang giữ phím
    ACT_MBTN,       // nút chuột, code: 1 trái, 2 phải, 4 giữa
    ACT_MO,         // giữ để vào layer code
    ACT_TG,         // bật/tắt layer code
    ACT_TO,         // chuyển hẳn sang layer code (0 = về lớp nền)
    ACT_LT,         // Layer-Tap: chạm = phím code, giữ = layer mod
    ACT_SEQ,        // Sequence số code: mỗi lần bấm chạy một bước
    ACT_TIMER,      // hẹn giờ: bắt đầu / dừng
    ACT_SONG,       // phát/dừng bài MIDI lưu trong ô code
    ACT_GAME,       // bật/tắt Tetris
    ACT_GKEY,       // (nội bộ) phím điều khiển game, code = hàng*7+cột
} act_type_t;

typedef struct {
    uint8_t  type;
    uint8_t  mod;
    uint16_t code;
} action_t;

#define XX          { ACT_NONE,  0, 0 }
#define ___         { ACT_TRANS, 0, 0 }
#define KC(k)       { ACT_KEY, 0, (k) }
#define MK(m, k)    { ACT_KEY, (m), (k) }
#define CONS(u)     { ACT_CONSUMER, 0, (u) }
#define FNKEY       { ACT_FN, 0, 0 }
#define MUSICKEY    { ACT_MUSIC, 0, 0 }
#define BOOTKEY     { ACT_BOOT, 0, 0 }
#define BOARDKEY    { ACT_BOARD, 0, 0 }
#define GAMEKEY     { ACT_GAME, 0, 0 }
#define AIRKEY      { ACT_AIR, 0, 0 }
#define AIRHOLD     { ACT_AIRH, 0, 0 }
#define MB(b)       { ACT_MBTN, 0, (b) }

#define CTRL KEYBOARD_MODIFIER_LEFTCTRL
#define ALT  KEYBOARD_MODIFIER_LEFTALT
#define GUI  KEYBOARD_MODIFIER_LEFTGUI

// Mã Consumer Control (HID usage page 0x0C)
#define CC_PLAY_PAUSE 0x00CD
#define CC_NEXT       0x00B5
#define CC_PREV       0x00B6
#define CC_MUTE       0x00E2
#define CC_VOL_UP     0x00E9
#define CC_VOL_DOWN   0x00EA

// Lớp nền. Hàng R0..R4, cột C0..C6.
// R3C6 = Enter 2U (chiếm cả R4C6). '0' 2U chiếm R4C3 + R4C4. '+' 2U chiếm R1C6 + R2C6.
static const action_t keymap_base[NROW][NCOL] = {
    { MK(CTRL,HID_KEY_C), MK(CTRL,HID_KEY_V), MK(CTRL,HID_KEY_X), FNKEY,
      KC(HID_KEY_KEYPAD_DIVIDE), KC(HID_KEY_KEYPAD_MULTIPLY), KC(HID_KEY_KEYPAD_SUBTRACT) },
    { MK(CTRL,HID_KEY_Z), MK(CTRL,HID_KEY_Y), MK(CTRL,HID_KEY_A), KC(HID_KEY_KEYPAD_7),
      KC(HID_KEY_KEYPAD_8), KC(HID_KEY_KEYPAD_9), KC(HID_KEY_KEYPAD_ADD) },
    { MK(CTRL,HID_KEY_S), MK(ALT,HID_KEY_TAB), MK(GUI,HID_KEY_D), KC(HID_KEY_KEYPAD_4),
      KC(HID_KEY_KEYPAD_5), KC(HID_KEY_KEYPAD_6), XX },
    { XX, KC(HID_KEY_ESCAPE), XX, KC(HID_KEY_END),
      KC(HID_KEY_INSERT), KC(HID_KEY_DELETE), KC(HID_KEY_KEYPAD_ENTER) },
    { KC(HID_KEY_ARROW_LEFT), KC(HID_KEY_ARROW_DOWN), KC(HID_KEY_ARROW_RIGHT), KC(HID_KEY_KEYPAD_0),
      XX, KC(HID_KEY_KEYPAD_DECIMAL), XX },
};

// Lớp FN (giữ Num). ___ = dùng lại phím lớp nền.
static const action_t keymap_fn[NROW][NCOL] = {
    { GAMEKEY, ___, ___, ___, CONS(CC_MUTE), CONS(CC_VOL_DOWN), CONS(CC_VOL_UP) },
    { ___, ___, ___, KC(HID_KEY_F1), KC(HID_KEY_F2), KC(HID_KEY_F3), CONS(CC_PLAY_PAUSE) },
    { ___, ___, ___, KC(HID_KEY_F4), KC(HID_KEY_F5), KC(HID_KEY_F6), ___ },
    { ___, BOARDKEY, ___, KC(HID_KEY_HOME), KC(HID_KEY_PAGE_UP), KC(HID_KEY_PAGE_DOWN), MUSICKEY },
    { CONS(CC_PREV), MB(1), MB(2), AIRKEY, ___, BOOTKEY, ___ },
};

#endif
