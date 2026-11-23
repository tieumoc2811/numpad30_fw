// Numpad30 — RP2350-Zero, ma trận 5x7, OLED SSD1306, USB HID
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "ssd1306.h"
#include "keymap.h"
#include "config.h"
#include "audio.h"
#include "air.h"
#include "midi.h"
#include "player.h"
#include "game.h"
#include "pet.h"

// ---- Chân theo HARDWARE.md ----
#define ROW0        0      // R0..R4 = GP0..GP4 (ra)
#define COL0        5      // C0..C6 = GP5..GP11 (vào, kéo xuống)
#define OLED_SDA    12
#define OLED_SCL    13

#define DEBOUNCE_MS 5
#define TAP_MS      250    // bấm Num nhanh hơn mức này (và không kèm phím khác) = NumLock

static bool     stable[NROW][NCOL];
static uint8_t  cnt[NROW][NCOL];
static action_t held[NROW][NCOL];    // hành động đã chốt lúc nhấn, dùng lại lúc nhả

static bool     num_down, num_used, music_on, host_numlock, last_mounted;
static uint32_t num_t_ms, tap_until_ms;
static bool     tap_active, kb_dirty, cons_dirty, ui_dirty = true;

static inline uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static uint32_t act_ms;
static bool     oled_asleep;
static bool     air_on, mouse_dirty, greet_active, greet_done;
static int      air_hold, pend_dx, pend_dy, greet_x;
static bool air_active(void) { return (ex.air[4] & 8) || air_on || air_hold > 0; }

// Móc cho lớp nhạc cụ (âm thanh I2S/MAX98357A). Định nghĩa lại ở file khác để ghi đè.
__attribute__((weak)) void music_note(uint8_t idx, bool on) { (void)idx; (void)on; }

static uint8_t mom[NLAYER], tog, num_layer = 1;

static bool layer_on(int l) { return l == 0 || mom[l] || ((tog >> l) & 1); }
static int  top_layer(void) { for (int l = NLAYER - 1; l > 0; l--) if (layer_on(l)) return l; return 0; }

static action_t resolve(int r, int c) {
    if (r == NUM_R && c == NUM_C) return km[0][r][c];
    for (int l = NLAYER - 1; l > 0; l--) {
        if (!layer_on(l)) continue;
        action_t a = km[l][r][c];
        if (a.type != ACT_TRANS) return a;
    }
    if (pet_active()) return (action_t){ ACT_PKEY, 0, (uint16_t)(r * NCOL + c) };
    if (game_active()) return (action_t){ ACT_GKEY, 0, (uint16_t)(r * NCOL + c) };
    if (music_on) return (action_t){ ACT_NOTE, 0, (uint16_t)(r * NCOL + c) };
    action_t a = km[0][r][c];
    return a.type == ACT_TRANS ? (action_t){ ACT_NONE, 0, 0 } : a;
}

// ---- Tap Dance: ph 0 rảnh, 1 đang nhấn lần 1, 2 chờ lần 2, 3 nhấn lần 2, 4 giữ, 5 xung "chạm" ----
typedef struct { uint8_t ph; uint32_t t; action_t out; } tds_t;
static tds_t tds[NTD];
static const action_t NOACT = { ACT_NONE, 0, 0 };
static uint16_t td_term(void) { return ex.term_ms ? ex.term_ms : 200; }

// ---- xung phím (Sequence, chạm của Layer-Tap) ----
static action_t pulse;
static uint32_t pulse_until;
static bool pulse_on;
static void fire_pulse(action_t a) { pulse = a; pulse_on = true; pulse_until = now_ms() + 15; kb_dirty = cons_dirty = true; }

// ---- Layer-Tap: ph 0 rảnh, 1 chờ quyết định, 2 đang giữ layer ----
static uint8_t  lt_ph[NROW][NCOL], seq_pos[NSEQ];
static uint32_t lt_t[NROW][NCOL];
static void lt_hold_all(void) {
    for (int r = 0; r < NROW; r++)
        for (int c = 0; c < NCOL; c++)
            if (lt_ph[r][c] == 1) { lt_ph[r][c] = 2; mom[held[r][c].mod % NLAYER]++; ui_dirty = true; }
}
static void lt_task(uint32_t now) {
    for (int r = 0; r < NROW; r++)
        for (int c = 0; c < NCOL; c++)
            if (lt_ph[r][c] == 1 && now - lt_t[r][c] >= td_term()) {
                lt_ph[r][c] = 2; mom[held[r][c].mod % NLAYER]++; ui_dirty = true;
            }
}

// ---- hẹn giờ và báo thức ----
static bool timer_run, alarm_on;
static uint32_t timer_end, alarm_t, alarm_rel, alarm_start, last_sec;
static uint8_t alarm_step;

// ---- màn chờ: 1 logo nảy, 2 sao bay ----
static bool ss_active;
static int ss_x, ss_y, ss_dx = 2, ss_dy = 1;
static uint8_t st_x[24], st_y[24], st_s[24];
static uint32_t ss_rng = 12345;
static uint32_t ss_rand(void) { ss_rng = ss_rng * 1664525u + 1013904223u; return ss_rng >> 8; }
static void ss_init(void) {
    ss_x = 10; ss_y = 20;
    for (int i = 0; i < 24; i++) { st_x[i] = ss_rand() % 128; st_y[i] = ss_rand() % 64; st_s[i] = 1 + ss_rand() % 3; }
}
static void ss_frame(void) {
    oled_clear();
    if (ex.ss_kind == 1) {
        ss_x += ss_dx; ss_y += ss_dy;
        if (ss_x < 0 || ss_x > 80) { ss_dx = -ss_dx; ss_x += ss_dx; }
        if (ss_y < 0 || ss_y > 56) { ss_dy = -ss_dy; ss_y += ss_dy; }
        oled_text(ss_x, ss_y, "NUMPAD30", 1);
    } else {
        for (int i = 0; i < 24; i++) {
            int sz = st_s[i] > 2 ? 2 : 1;
            oled_fill(st_x[i], st_y[i], sz, sz);
            st_x[i] = (uint8_t)(st_x[i] - st_s[i]);
            if (st_x[i] >= 128) { st_x[i] = 127; st_y[i] = ss_rand() % 64; }
        }
    }
    oled_flush();
}

static void td_event(uint8_t i, bool down) {
    if (i >= NTD) return;
    tds_t *s = &tds[i];
    if (down) {
        if (s->ph == 0)      { s->ph = 1; s->t = now_ms(); }
        else if (s->ph == 2) { s->ph = 3; s->out = ex.td[i].dtap; kb_dirty = cons_dirty = true; }
    } else if (s->ph == 1) { s->ph = 2; s->t = now_ms(); }
    else if (s->ph == 3 || s->ph == 4) { s->ph = 0; s->out = NOACT; kb_dirty = cons_dirty = true; }
}

static void td_task(uint32_t now) {
    for (int i = 0; i < NTD; i++) {
        tds_t *s = &tds[i];
        if (s->ph == 1 && now - s->t >= td_term())      { s->ph = 4; s->out = ex.td[i].hold; kb_dirty = cons_dirty = true; }
        else if (s->ph == 2 && now - s->t >= td_term()) { s->ph = 5; s->t = now; s->out = ex.td[i].tap; kb_dirty = cons_dirty = true; }
        else if (s->ph == 5 && now - s->t >= 15)        { s->ph = 0; s->out = NOACT; kb_dirty = cons_dirty = true; }
    }
}

// ---- Combo: giữ phím đầu tối đa COMBO_MS để chờ phím thứ hai ----
#define COMBO_MS 40
static int pend = -1, cb_on = -1, cb_a, cb_b;
static uint32_t pend_t;
static action_t cb_out;

static bool combo_uses(int k) {
    for (int i = 0; i < NCOMBO; i++) { combo_t cb = ex.combo[i]; if (cb.out.type && (cb.a == k || cb.b == k)) return true; }
    return false;
}
static int combo_find(int x, int y) {
    for (int i = 0; i < NCOMBO; i++) {
        combo_t cb = ex.combo[i];
        if (cb.out.type && ((cb.a == x && cb.b == y) || (cb.a == y && cb.b == x))) return i;
    }
    return -1;
}

static void press(int r, int c) {
    if (tud_suspended()) tud_remote_wakeup();
    lt_hold_all();
    if (num_down && !(r == NUM_R && c == NUM_C)) num_used = true;
    action_t a = resolve(r, c);
    switch (a.type) {
    case ACT_KEY:      held[r][c] = a; kb_dirty = true;   break;
    case ACT_CONSUMER: held[r][c] = a; cons_dirty = true; break;
    case ACT_NOTE:     held[r][c] = a; music_note((uint8_t)a.code, true); break;
    case ACT_TD:       held[r][c] = a; td_event((uint8_t)a.code, true); break;
    case ACT_MACRO:    macro_start((uint8_t)a.code); break;
    case ACT_FN:
        num_layer = (a.code > 0 && a.code < NLAYER) ? (uint8_t)a.code : 1;
        mom[num_layer]++;
        num_down = true; num_used = false; num_t_ms = now_ms(); ui_dirty = true;
        break;
    case ACT_MO:       if (a.code < NLAYER) { held[r][c] = a; mom[a.code]++; ui_dirty = true; } break;
    case ACT_TG:       if (a.code > 0 && a.code < NLAYER) tog ^= (uint8_t)(1u << a.code); ui_dirty = true; break;
    case ACT_TO:       tog = (a.code > 0 && a.code < NLAYER) ? (uint8_t)(1u << a.code) : 0; ui_dirty = true; break;
    case ACT_MUSIC:    music_on = !music_on; ui_dirty = true; break;
    case ACT_BOARD:    cfg_next_board(); ui_dirty = true; break;
    case ACT_AIR:      air_on = !air_on; ui_dirty = true; break;
    case ACT_AIRH:     held[r][c] = a; air_hold++; ui_dirty = true; break;
    case ACT_MBTN:     held[r][c] = a; mouse_dirty = true; break;
    case ACT_LT:       held[r][c] = a; lt_ph[r][c] = 1; lt_t[r][c] = now_ms(); break;
    case ACT_SONG:     if (player_active()) player_stop(); else player_start((uint8_t)a.code); ui_dirty = true; break;
    case ACT_GAME:     if (game_active()) game_stop(); else { if (pet_active()) pet_stop(); music_on = false; game_start(); } ui_dirty = true; break;
    case ACT_PET:      if (pet_active()) pet_stop(); else { if (game_active()) game_stop(); music_on = false; pet_start(); } ui_dirty = true; break;
    case ACT_PKEY:     held[r][c] = a; pet_key((uint8_t)a.code, true); break;
    case ACT_GKEY:     held[r][c] = a; game_key((uint8_t)a.code, true); break;
    case ACT_SEQ: {
        int i = a.code % NSEQ, n = 0;
        while (n < SEQLEN && ex.seq[i][n].type) n++;
        if (n) { action_t st = ex.seq[i][seq_pos[i] % n]; seq_pos[i] = (uint8_t)((seq_pos[i] + 1) % n); fire_pulse(st); }
        break; }
    case ACT_TIMER:
        if (alarm_on) alarm_on = false;
        else if (timer_run) timer_run = false;
        else { timer_run = true; timer_end = now_ms() + (uint32_t)ex.timer_s * 1000u; }
        ui_dirty = true;
        break;
    case ACT_BOOT:     reset_usb_boot(0, 0); break;
    default: break;
    }
}

static void release(int r, int c) {
    if (r == NUM_R && c == NUM_C) {
        num_down = false;
        if (mom[num_layer]) mom[num_layer]--;
        ui_dirty = true;
        if (!num_used && (now_ms() - num_t_ms) < TAP_MS) {
            tap_active = true; tap_until_ms = now_ms() + 15; kb_dirty = true;
        }
        return;
    }
    action_t a = held[r][c];
    if (a.type == ACT_KEY)           kb_dirty = true;
    else if (a.type == ACT_CONSUMER) cons_dirty = true;
    else if (a.type == ACT_NOTE)     music_note((uint8_t)a.code, false);
    else if (a.type == ACT_TD)       td_event((uint8_t)a.code, false);
    else if (a.type == ACT_AIRH)   { if (air_hold > 0) air_hold--; ui_dirty = true; }
    else if (a.type == ACT_MBTN)     mouse_dirty = true;
    else if (a.type == ACT_MO)     { if (mom[a.code]) mom[a.code]--; ui_dirty = true; }
    else if (a.type == ACT_LT) {
        if (lt_ph[r][c] == 2) { uint8_t l = a.mod % NLAYER; if (mom[l]) mom[l]--; ui_dirty = true; }
        else if (lt_ph[r][c] == 1) fire_pulse((action_t){ ACT_KEY, 0, a.code });
        lt_ph[r][c] = 0;
    }
    else if (a.type == ACT_GKEY) game_key((uint8_t)a.code, false);
    else if (a.type == ACT_PKEY) pet_key((uint8_t)a.code, false);
    held[r][c] = NOACT;
}

static void flush_pend(void) { int k = pend; pend = -1; if (k >= 0) press(k / NCOL, k % NCOL); }

static void on_key(int r, int c, bool down) {
    int k = r * NCOL + c;
    act_ms = now_ms();
    if (oled_asleep) { oled_power(true); oled_asleep = false; }
    cfg_key_event(r, c, down);
    if (ss_active) { ss_active = false; ui_dirty = true; }
    if (pet_showing() && !pet_active()) { pet_ambient(false); ui_dirty = true; }
    if (down && !pet_active()) pet_typed();
    if (down && alarm_on) { alarm_on = false; music_note(200, false); ui_dirty = true; return; }
    if (down) {
        if (pend >= 0) {
            int i = combo_find(pend, k);
            if (i >= 0) {
                combo_t cb = ex.combo[i];
                cb_on = i; cb_a = pend; cb_b = k; cb_out = cb.out; pend = -1;
                kb_dirty = cons_dirty = true;
                return;
            }
            flush_pend();
        }
        if (top_layer() == 0 && !music_on && !game_active() && !pet_active() && combo_uses(k)) { pend = k; pend_t = now_ms(); return; }
        press(r, c);
    } else {
        if (pend == k) flush_pend();
        if (cb_on >= 0 && (k == cb_a || k == cb_b)) { cb_on = -1; cb_out = NOACT; kb_dirty = cons_dirty = true; return; }
        release(r, c);
    }
}

static void scan(void) {
    for (int r = 0; r < NROW; r++) {
        gpio_put(ROW0 + r, 1);
        busy_wait_us_32(4);
        uint32_t in = (gpio_get_all() >> COL0) & 0x7F;
        gpio_put(ROW0 + r, 0);
        for (int c = 0; c < NCOL; c++) {
            bool raw = (in >> c) & 1;
            if (raw == stable[r][c]) { cnt[r][c] = 0; continue; }
            if (++cnt[r][c] >= DEBOUNCE_MS) {
                stable[r][c] = raw;
                cnt[r][c] = 0;
                on_key(r, c, raw);
            }
        }
    }
}

static void kb_add(action_t a, uint8_t *mods, uint8_t *keys, int *n) {
    if (a.type != ACT_KEY) return;
    *mods |= a.mod;
    if (a.code && *n < 6) keys[(*n)++] = (uint8_t)a.code;
}

static void send_kb(void) {
    uint8_t mods = 0, keys[6] = {0};
    int n = 0;
    for (int r = 0; r < NROW; r++)
        for (int c = 0; c < NCOL; c++) kb_add(held[r][c], &mods, keys, &n);
    for (int i = 0; i < NTD; i++) kb_add(tds[i].out, &mods, keys, &n);
    if (cb_on >= 0) kb_add(cb_out, &mods, keys, &n);
    if (pulse_on) kb_add(pulse, &mods, keys, &n);
    n = macro_out(&mods, keys, n);
    if (tap_active && n < 6) keys[n++] = HID_KEY_NUM_LOCK;
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, mods, keys);
}

static uint16_t cons_of(action_t a) { return a.type == ACT_CONSUMER ? a.code : 0; }

static void send_cons(void) {
    uint16_t usage = 0;
    for (int r = 0; r < NROW; r++)
        for (int c = 0; c < NCOL; c++) if (!usage) usage = cons_of(held[r][c]);
    for (int i = 0; i < NTD; i++) if (!usage) usage = cons_of(tds[i].out);
    if (!usage && cb_on >= 0) usage = cons_of(cb_out);
    if (!usage && pulse_on) usage = cons_of(pulse);
    tud_hid_report(REPORT_ID_CONSUMER, &usage, sizeof usage);
}

static void send_mouse(void) {
    uint8_t btn = 0;
    for (int r = 0; r < NROW; r++)
        for (int c = 0; c < NCOL; c++)
            if (held[r][c].type == ACT_MBTN) btn |= (uint8_t)held[r][c].code;
    int cx = pend_dx < -127 ? -127 : pend_dx > 127 ? 127 : pend_dx;
    int cy = pend_dy < -127 ? -127 : pend_dy > 127 ? 127 : pend_dy;
    pend_dx -= cx; pend_dy -= cy;
    tud_hid_mouse_report(REPORT_ID_MOUSE, btn, (int8_t)cx, (int8_t)cy, 0, 0);
    mouse_dirty = false;
}

static void draw_ui(void) {
    oled_clear();
    oled_text(0, 0, "NUMPAD30", 1);
    static const char *const BN[3] = { "PIANO", "DRUM", "GUITAR" };
    oled_text(0, 14, music_on ? BN[cfg_board() % 3] : cfg_layer_name(top_layer()), 3);
    oled_text(0, 44, host_numlock ? "NUMLOCK: ON" : "NUMLOCK: OFF", 1);
    char tb[24];
    if (alarm_on) snprintf(tb, sizeof tb, "HET GIO! BAM PHIM");
    else if (timer_run) {
        uint32_t s = (timer_end - now_ms() + 999) / 1000;
        snprintf(tb, sizeof tb, "TIMER %02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    } else if (player_active()) snprintf(tb, sizeof tb, "> %s", player_name());
    else snprintf(tb, sizeof tb, "%s", !tud_mounted() ? "USB: --" : air_active() ? "USB: OK  AIR" : "USB: OK");
    oled_text(0, 56, tb, 1);
    oled_flush();
}

static void apply_oled(void) {
    oled_set_flip(ex.flags & 1);
    oled_set_contrast(ex.bright);
}

static void splash(void) {
    for (int i = 0; i <= 8; i++) { oled_clear(); oled_text(128 - i * 14, 14, "NUMPAD30", 2); oled_flush_now(); sleep_ms(20); }
    oled_text(16, 38, "30 KEYS - RP2350", 1);
    for (int i = 0; i <= 8; i++) { oled_fill(16, 52, i * 12, 4); oled_flush_now(); sleep_ms(40); }
}

// ---- Callback HID ----
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    if (instance == 1) { cfg_on_raw(buffer, bufsize); return; }
    if (report_type != HID_REPORT_TYPE_OUTPUT) return;
    if (report_id == 0 && bufsize > 0) { report_id = buffer[0]; buffer++; bufsize--; }
    if (report_id == REPORT_ID_KEYBOARD && bufsize >= 1) {
        bool nl = buffer[0] & KEYBOARD_LED_NUMLOCK;
        if (nl != host_numlock) { host_numlock = nl; ui_dirty = true; }
    }
}

int main(void) {
    set_sys_clock_khz(150000, true);

    cfg_init();
    for (int r = 0; r < NROW; r++) {
        gpio_init(ROW0 + r);
        gpio_set_dir(ROW0 + r, GPIO_OUT);
        gpio_put(ROW0 + r, 0);
    }
    for (int c = 0; c < NCOL; c++) {
        gpio_init(COL0 + c);
        gpio_set_dir(COL0 + c, GPIO_IN);
        gpio_pull_down(COL0 + c);
    }

    oled_init(i2c0, OLED_SDA, OLED_SCL);   // GP12/GP13 = I2C0
    apply_oled();
    if (ex.flags & 2) splash();
    audio_init();
    pet_init();
    air_init();
    tusb_init();

    uint32_t last_scan = time_us_32();
    while (true) {
        tud_task();

        uint32_t t = time_us_32();
        if (t - last_scan >= 1000) { last_scan = t; scan(); }

        if (tap_active && now_ms() >= tap_until_ms) { tap_active = false; kb_dirty = true; }
        uint32_t nm = now_ms();
        if (macro_task(nm)) kb_dirty = true;
        td_task(nm);
        lt_task(nm);
        midi_task();
        player_task(nm);
        game_task(nm);
        pet_ambient(pet_ambient_allowed() && !pet_active() && !game_active() && !ss_active && !oled_asleep && !greet_active
                    && !alarm_on && !timer_run && !player_active() && (nm - act_ms) >= PET_IDLE_MS);
        pet_task(nm);
        { static bool ps_prev; if (pet_showing() != ps_prev) { ps_prev = pet_showing(); ui_dirty = true; } }
        { static bool pl_prev; if (player_active() != pl_prev) { pl_prev = player_active(); ui_dirty = true; } }
        { static bool gm_prev; if (game_active() != gm_prev) { gm_prev = game_active(); ui_dirty = true; } }
        if (pulse_on && nm >= pulse_until) { pulse_on = false; pulse = NOACT; kb_dirty = cons_dirty = true; }
        if (timer_run) {
            if ((int32_t)(nm - timer_end) >= 0) {
                timer_run = false; alarm_on = true; alarm_step = 0; alarm_t = nm - 300; alarm_start = nm; alarm_rel = 0;
                if (oled_asleep) { oled_power(true); oled_asleep = false; }
                ss_active = false; ui_dirty = true;
            } else if (nm / 1000 != last_sec) { last_sec = nm / 1000; ui_dirty = true; }
        }
        if (alarm_on) {
            static const uint16_t AL[4] = {880, 1047, 1319, 1047};
            if (nm - alarm_t >= 300) { alarm_t = nm; audio_tone(AL[alarm_step++ & 3]); alarm_rel = nm + 150; }
            if (alarm_rel && nm >= alarm_rel) { music_note(200, false); alarm_rel = 0; }
            if (nm - alarm_start > 60000) { alarm_on = false; music_note(200, false); ui_dirty = true; }
        }
        { int dx, dy; if (air_poll(nm, air_active(), &dx, &dy)) { pend_dx += dx; pend_dy += dy; } }
        if (pend >= 0 && nm - pend_t >= COMBO_MS) flush_pend();

        bool m = tud_mounted();
        if (m != last_mounted) { last_mounted = m; ui_dirty = true; }
        if (m && !greet_done && (ex.flags & 4) && ex.greet[0]) { greet_done = true; greet_active = true; greet_x = 128; }
        if (greet_active && !oled_busy()) {
            oled_clear();
            oled_text(greet_x, 26, ex.greet, 2);
            oled_flush();
            greet_x -= 4;
            if (greet_x < -12 * (int)strlen(ex.greet)) { greet_active = false; ui_dirty = true; }
        }

        if (tud_hid_ready()) {
            if (kb_dirty)        { send_kb();   kb_dirty = false; }
            else if (cons_dirty) { send_cons(); cons_dirty = false; }
            else if (mouse_dirty || pend_dx || pend_dy) send_mouse();
        }

        if (ui_dirty && !greet_active && !ss_active && !game_active() && !pet_showing()) { draw_ui(); ui_dirty = false; }
        oled_service();
        cfg_task();
        audio_task();
        if (cfg_take_changed()) apply_oled();
        if (ex.sleep_min && !oled_asleep && !ss_active && !game_active() && !pet_active() && now_ms() - act_ms > ex.sleep_min * 60000u) {
            if (ex.ss_kind == 0) { oled_power(false); oled_asleep = true; } else { ss_active = true; ss_init(); }
        }
        if (ss_active && !oled_busy()) ss_frame();
    }
}
