// Cấu hình qua HID thô 32 byte + tự lưu flash.
//  [1,l,r]               đọc hàng keymap  -> [1,l,r, 7x4]
//  [2,l,r, 7x4]          ghi hàng keymap
//  [3]                   khôi phục mặc định
//  [4,offH,offL]         đọc 28 byte vùng extra -> [4,offH,offL, 28]
//  [5,offH,offL,n,data]  ghi n (<=28) byte vùng extra
//  firmware gửi [0x80] khi đã ghi xong flash
#include <string.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "tusb.h"
#include "config.h"
#include "player.h"

#define CFG_OFFSET    (2 * 1024 * 1024 - FLASH_SECTOR_SIZE)
#define CFG_MAGIC     0x4E503337u      // đổi magic = bỏ cấu hình cũ khi đổi bố cục
#define SAVE_DELAY_MS 1500

typedef struct { uint32_t magic; action_t km[NLAYER][NROW][NCOL]; extra_t ex; } blob_t;
_Static_assert(sizeof(blob_t) <= 4096, "blob too big");

action_t km[NLAYER][NROW][NCOL];
extra_t  ex;
static uint8_t reply[32];
static bool reply_pending, save_pending, notify_pending;
static uint32_t save_at;
static bool changed, test_mode;
static uint8_t ubuf[SONG_SECTOR] __attribute__((aligned(4)));
static bool commit_pending, song_notify;
static uint8_t commit_slot;
static void song_job(void *p) {
    (void)p;
    uint32_t off = SONG_BASE + (uint32_t)commit_slot * SONG_SECTOR;
    flash_range_erase(off, SONG_SECTOR);
    flash_range_program(off, ubuf, SONG_SECTOR);
}
static uint8_t kq[16][3], kq_h, kq_t;
void cfg_key_event(int r, int c, bool down) {
    if (!test_mode) return;
    uint8_t n = (kq_t + 1) & 15;
    if (n == kq_h) return;
    kq[kq_t][0] = (uint8_t)r; kq[kq_t][1] = (uint8_t)c; kq[kq_t][2] = down;
    kq_t = n;
}
bool cfg_take_changed(void) { bool c = changed; changed = false; return c; }

static uint16_t midi_hz(int m) {
    static const uint16_t c4[12] = {262,277,294,311,330,349,370,392,415,440,466,494};
    int o = m / 12;
    uint16_t f = c4[m % 12];
    return o >= 5 ? (uint16_t)(f << (o - 5)) : (uint16_t)(f >> (5 - o));
}

static void load_defaults(void) {
    memcpy(km[0], keymap_base, sizeof km[0]);
    memcpy(km[1], keymap_fn, sizeof km[1]);
    for (int l = 2; l < NLAYER; l++)
        for (int r = 0; r < NROW; r++)
            for (int c = 0; c < NCOL; c++) km[l][r][c] = (action_t){ ACT_TRANS, 0, 0 };
    memset(&ex, 0, sizeof ex);
    for (int i = 0; i < NCOMBO; i++) ex.combo[i].a = ex.combo[i].b = 255;
    // Trống: 1 kick, 2 snare, 3 hat đóng, 4 hat mở, 5-7 tom cao/giữa/thấp, 8 crash, 9 ride, 10 clap, 11 rim, 12 cowbell
    static const uint8_t drum[35] = {10,11,12,0,9,8,4, 5,6,7,3,3,4,8, 1,2,1,5,6,7,0, 0,1,0,1,2,3,8, 1,2,1,1,0,3,0};
    static const uint8_t str[5] = {64, 59, 55, 50, 45};   // dây e, B, G, D, A (hàng R0..R4), cột = phím đàn 0..6
    for (int k = 0; k < NROW * NCOL; k++) {
        ex.note[0][k] = midi_hz(60 + k);
        ex.note[1][k] = drum[k];
        ex.note[2][k] = midi_hz(str[k / NCOL] + k % NCOL);
    }
    ex.term_ms = 200;
    ex.volume = 60; ex.bright = 0xCF; ex.sleep_min = 0; ex.flags = 2;
    static const uint8_t air_def[8] = {60, 60, 15, 2, 0, 0, 0, 0};
    memcpy(ex.air, air_def, sizeof air_def);
    strcpy(ex.greet, "NUMPAD30");
    static const char *const nm[NLAYER] = {"BASE", "FN", "L3", "L4", "L5", "L6", "L7", "L8"};
    for (int i = 0; i < NLAYER; i++) strcpy(ex.lname[i], nm[i]);
    ex.nl = 2;
    ex.ss_kind = 0; ex.timer_s = 300;
    ex.midi_on = 1; ex.midi_local = 1; ex.midi_ch = 0; ex.midi_tr = 0; ex.midi_vel = 100; ex.song_tempo = 100; ex.song_loop = 0;
}

void cfg_init(void) {
    load_defaults();
    const blob_t *b = (const blob_t *)(XIP_BASE + CFG_OFFSET);
    if (b->magic == CFG_MAGIC) { memcpy(km, b->km, sizeof km); memcpy(&ex, &b->ex, sizeof ex); }
    if (ex.hiscore == 0xFFFFFFFFu) ex.hiscore = 0;      // cấu hình cũ chưa có trường này
}

uint16_t cfg_note(uint8_t idx) { return idx < NROW * NCOL ? ex.note[ex.board % 3][idx] : 0; }
uint8_t cfg_board(void) { return ex.board % 3; }
const char *cfg_layer_name(int l) { return ex.lname[l < NLAYER ? l : 0]; }

static void flash_job(void *p) {
    (void)p;
    static uint8_t page[4096] __attribute__((aligned(4)));
    memset(page, 0xFF, sizeof page);
    blob_t *b = (blob_t *)page;
    b->magic = CFG_MAGIC;
    memcpy(b->km, km, sizeof km);
    memcpy(&b->ex, &ex, sizeof ex);
    flash_range_erase(CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CFG_OFFSET, page, sizeof page);
}

static void schedule_save(void) {
    save_pending = true;
    save_at = to_ms_since_boot(get_absolute_time()) + SAVE_DELAY_MS;
}

void cfg_set_hiscore(uint32_t v) { ex.hiscore = v; schedule_save(); }

void cfg_next_board(void) { ex.board = (uint8_t)((ex.board + 1) % 3); schedule_save(); }

void cfg_on_raw(const uint8_t *b, uint16_t n) {
    if (n < 1) return;
    memset(reply, 0, sizeof reply);
    reply[0] = b[0];
    uint16_t off = n >= 3 ? (uint16_t)(b[1] << 8 | b[2]) : 0;
    if (b[0] == 1 && n >= 3 && b[1] < NLAYER && b[2] < NROW) {
        reply[1] = b[1]; reply[2] = b[2];
        memcpy(&reply[3], km[b[1]][b[2]], NCOL * sizeof(action_t));
    } else if (b[0] == 2 && n >= 3 + NCOL * 4 && b[1] < NLAYER && b[2] < NROW) {
        memcpy(km[b[1]][b[2]], &b[3], NCOL * sizeof(action_t));
        memcpy(reply, b, 3 + NCOL * 4);
        schedule_save();
    } else if (b[0] == 8 && n >= 5) {            // [8, ô, offH, offL, n, dữ liệu<=27]: ghi vào bộ đệm bài
        uint16_t so = (uint16_t)(b[2] << 8 | b[3]);
        uint8_t len = b[4];
        reply[1] = b[1]; reply[2] = b[2]; reply[3] = b[3];
        if (b[1] < NSONG && len <= 27 && so + len <= SONG_SECTOR && n >= 5 + len) {
            if (so == 0) memset(ubuf, 0xFF, sizeof ubuf);
            memcpy(&ubuf[so], &b[5], len);
        }
    } else if (b[0] == 9 && n >= 2) {            // [9, ô]: ghi bộ đệm vào flash, báo lại [0x82, ô]
        reply[1] = b[1];
        if (b[1] < NSONG) { commit_slot = b[1]; commit_pending = true; }
    } else if (b[0] == 10 && n >= 2) {           // [10, ô]: thông tin ô -> [10, ô, hợp lệ, nốtH, nốtL, tên 18]
        uint16_t cnt = 0;
        char nm[18];
        bool ok = b[1] < NSONG && song_info(b[1], &cnt, nm);
        reply[1] = b[1]; reply[2] = ok; reply[3] = (uint8_t)(cnt >> 8); reply[4] = (uint8_t)cnt;
        if (ok) memcpy(&reply[5], nm, 18);
    } else if (b[0] == 11 && n >= 3) {           // [11, 0 dừng | 1 phát ô | 2 xóa ô, ô]
        reply[1] = b[1];
        if (b[1] == 0) player_stop();
        else if (b[1] == 1 && b[2] < NSONG) player_start(b[2]);
        else if (b[1] == 2 && b[2] < NSONG) { player_stop(); memset(ubuf, 0xFF, sizeof ubuf); commit_slot = b[2]; commit_pending = true; }
    } else if (b[0] == 12) {                     // [12]: xoá điểm cao
        ex.hiscore = 0;
        schedule_save();
    } else if (b[0] == 7 && n >= 2) {
        test_mode = b[1] != 0;
    } else if (b[0] == 6) {
        save_pending = true; save_at = 0;   // lưu ngay
    } else if (b[0] == 3) {
        load_defaults();
        changed = true;
        schedule_save();
    } else if (b[0] == 4 && n >= 3) {
        reply[1] = b[1]; reply[2] = b[2];
        if (off < sizeof ex) {
            size_t k = sizeof ex - off;
            if (k > 28) k = 28;
            memcpy(&reply[3], (uint8_t *)&ex + off, k);
        }
    } else if (b[0] == 5 && n >= 4) {
        uint8_t len = b[3];
        reply[1] = b[1]; reply[2] = b[2];
        if (len <= 28 && off + len <= sizeof ex && n >= 4 + len) {
            uint32_t hs = ex.hiscore;                    // điểm cao chỉ do bàn phím ghi
            memcpy((uint8_t *)&ex + off, &b[4], len);
            ex.hiscore = hs;
            ex.greet[sizeof ex.greet - 1] = 0;
            for (int i = 0; i < NLAYER; i++) ex.lname[i][7] = 0;
            if (ex.nl < 2 || ex.nl > NLAYER) ex.nl = 2;
            if (!ex.timer_s) ex.timer_s = 300;
            if (ex.ss_kind > 2) ex.ss_kind = 0;
            ex.midi_ch &= 15;
            if (ex.midi_vel < 1 || ex.midi_vel > 127) ex.midi_vel = 100;
            if (ex.song_tempo < 25) ex.song_tempo = 100;
            changed = true;
            schedule_save();
        }
    }
    reply_pending = true;
}

void cfg_task(void) {
    if (reply_pending) {
        if (tud_hid_n_ready(1)) { tud_hid_n_report(1, 0, reply, sizeof reply); reply_pending = false; }
    } else if (save_pending && to_ms_since_boot(get_absolute_time()) >= save_at) {
        flash_safe_execute(flash_job, NULL, 200);
        save_pending = false;
        notify_pending = true;
    } else if (commit_pending) {
        flash_safe_execute(song_job, NULL, 500);
        commit_pending = false;
        song_notify = true;
    } else if (song_notify && tud_hid_n_ready(1)) {
        memset(reply, 0, sizeof reply);
        reply[0] = 0x82; reply[1] = commit_slot;
        tud_hid_n_report(1, 0, reply, sizeof reply);
        song_notify = false;
    } else if (kq_h != kq_t && tud_hid_n_ready(1)) {
        memset(reply, 0, sizeof reply);
        reply[0] = 0x81; reply[1] = kq[kq_h][0]; reply[2] = kq[kq_h][1]; reply[3] = kq[kq_h][2];
        tud_hid_n_report(1, 0, reply, sizeof reply);
        kq_h = (kq_h + 1) & 15;
    } else if (notify_pending && tud_hid_n_ready(1)) {
        memset(reply, 0, sizeof reply);
        reply[0] = 0x80;
        tud_hid_n_report(1, 0, reply, sizeof reply);
        notify_pending = false;
    }
}

// ---- Bộ chạy macro ----
static struct { bool run, tapping; uint8_t id, pc, mods, keys[6]; uint32_t until; } mc;

static void mk_add(uint8_t k) { if (!k) return; for (int i = 0; i < 6; i++) if (!mc.keys[i]) { mc.keys[i] = k; return; } }
static void mk_del(uint8_t k) { for (int i = 0; i < 6; i++) if (mc.keys[i] == k) mc.keys[i] = 0; }

void macro_start(uint8_t id) {
    if (id >= NMACRO || mc.run) return;
    memset(&mc, 0, sizeof mc);
    mc.run = true; mc.id = id;
}

bool macro_task(uint32_t now) {
    if (!mc.run || (int32_t)(now - mc.until) < 0) return false;
    mstep_t s = {0, 0, 0};
    if (mc.pc < MSTEP) s = ex.macro[mc.id][mc.pc++];
    switch (s.op) {
    case 1:
        if (!mc.tapping) { mc.mods |= s.mod; mk_add(s.key); mc.tapping = true; mc.pc--; }
        else             { mc.mods &= (uint8_t)~s.mod; mk_del(s.key); mc.tapping = false; }
        mc.until = now + 12;
        return true;
    case 2: mc.mods |= s.mod; mk_add(s.key); mc.until = now + 5; return true;
    case 3: mc.mods &= (uint8_t)~s.mod; mk_del(s.key); mc.until = now + 5; return true;
    case 4: mc.until = now + s.key * 10u; return false;
    default: memset(&mc, 0, sizeof mc); return true;
    }
}

int macro_out(uint8_t *mods, uint8_t *keys, int n) {
    *mods |= mc.mods;
    for (int i = 0; i < 6; i++) if (mc.keys[i] && n < 6) keys[n++] = mc.keys[i];
    return n;
}
