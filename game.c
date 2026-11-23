#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "ssd1306.h"
#include "config.h"
#include "audio.h"
#include "game.h"

#define BW 10
#define BH 22            // 2 hàng ẩn phía trên
#define LOCK_MS 450
#define MUSIC_KEY 250

// ---- hình khối: toạ độ ô trong hộp, xoay bằng (x,y) -> (hộp-1-y, x) ----
static const int8_t BASE[7][4][2] = {
    {{0,1},{1,1},{2,1},{3,1}},   // I
    {{0,0},{1,0},{0,1},{1,1}},   // O
    {{1,0},{0,1},{1,1},{2,1}},   // T
    {{1,0},{2,0},{0,1},{1,1}},   // S
    {{0,0},{1,0},{1,1},{2,1}},   // Z
    {{0,0},{0,1},{1,1},{2,1}},   // J
    {{2,0},{0,1},{1,1},{2,1}},   // L
};
static const uint8_t BOX[7] = {4, 2, 3, 3, 3, 3, 3};
static int8_t SH[7][4][4][2];
static bool shapes_ready;

static void init_shapes(void) {
    for (int p = 0; p < 7; p++) {
        for (int k = 0; k < 4; k++) { SH[p][0][k][0] = BASE[p][k][0]; SH[p][0][k][1] = BASE[p][k][1]; }
        for (int r = 1; r < 4; r++)
            for (int k = 0; k < 4; k++) {
                SH[p][r][k][0] = (int8_t)(BOX[p] - 1 - SH[p][r-1][k][1]);
                SH[p][r][k][1] = SH[p][r-1][k][0];
            }
    }
    shapes_ready = true;
}

// ---- nhạc nền (giai điệu dân ca Nga Korobeiniki) và hiệu ứng ----
static const uint8_t MEL[][2] = {   // {nốt MIDI, độ dài tính theo nốt móc đơn}, 0 = nghỉ
    {76,2},{71,1},{72,1},{74,2},{72,1},{71,1},{69,2},{69,1},{72,1},{76,2},{74,1},{72,1},{71,3},{72,1},{74,2},{76,2},{72,2},{69,2},{69,2},{0,2},
    {74,3},{77,1},{81,2},{79,1},{77,1},{76,3},{72,1},{76,2},{74,1},{72,1},{71,2},{71,1},{72,1},{74,2},{76,2},{72,2},{69,2},{69,2},{0,2}};
#define NMEL ((int)(sizeof MEL / sizeof MEL[0]))
#define EIGHTH_MS 140

static struct { uint32_t at, off; uint16_t hz; bool used, started; } sq[12];

static uint32_t nowms(void) { return to_ms_since_boot(get_absolute_time()); }

static void snd(uint16_t hz, uint32_t delay, uint32_t dur) {
    if (!hz) return;
    uint32_t t = nowms();
    for (int i = 0; i < 12; i++) if (!sq[i].used) {
        sq[i].used = true; sq[i].started = false; sq[i].hz = hz; sq[i].at = t + delay; sq[i].off = t + delay + dur;
        return;
    }
}
static void snd_task(uint32_t now) {
    for (int i = 0; i < 12; i++) {
        if (!sq[i].used) continue;
        if (!sq[i].started && now >= sq[i].at) { audio_lead(sq[i].hz, (uint8_t)(230 + i)); sq[i].started = true; }
        else if (sq[i].started && now >= sq[i].off) { audio_release((uint8_t)(230 + i)); sq[i].used = false; }
    }
}
static void snd_all_off(void) {
    for (int i = 0; i < 12; i++) if (sq[i].used) { if (sq[i].started) audio_release((uint8_t)(230 + i)); sq[i].used = false; }
}

// ---- trạng thái ----
static struct {
    bool on, over, paused, clearing, hold_used, touch, kl, kr, kd, dirty, mus_note;
    uint8_t b[BH][BW];                 // 0 trống, còn lại = mảnh + 1
    uint8_t cur, rot, next, hold, bag[7], bag_i, resets, level;
    int8_t x, y;
    uint32_t score, lines, rng;
    uint32_t t_grav, t_lock, t_das, t_soft, t_clear, t_draw, t_mus;
    uint32_t clear_mask;
    int mus_i;
} g;

static uint32_t rnd(void) { g.rng ^= g.rng << 13; g.rng ^= g.rng >> 17; g.rng ^= g.rng << 5; return g.rng; }

static uint8_t take(void) {
    if (g.bag_i >= 7) {
        for (int i = 0; i < 7; i++) g.bag[i] = (uint8_t)i;
        for (int i = 6; i > 0; i--) { int j = rnd() % (i + 1); uint8_t t = g.bag[i]; g.bag[i] = g.bag[j]; g.bag[j] = t; }
        g.bag_i = 0;
    }
    return g.bag[g.bag_i++];
}

static bool fits(int p, int r, int x, int y) {
    for (int k = 0; k < 4; k++) {
        int cx = x + SH[p][r][k][0], cy = y + SH[p][r][k][1];
        if (cx < 0 || cx >= BW || cy >= BH) return false;
        if (cy >= 0 && g.b[cy][cx]) return false;
    }
    return true;
}

static uint32_t grav_ms(void) { return g.level >= 12 ? 50 : 800 - 62u * g.level; }

static void touched(uint32_t now) {
    if (g.touch && g.resets < 12) { g.t_lock = now + LOCK_MS; g.resets++; }
}

static bool try_move(int dx, int dy) {
    if (!fits(g.cur, g.rot, g.x + dx, g.y + dy)) return false;
    g.x = (int8_t)(g.x + dx); g.y = (int8_t)(g.y + dy); g.dirty = true;
    touched(nowms());
    return true;
}

static bool try_rotate(int dir) {
    static const int8_t kx[] = {0, -1, 1, -2, 2, 0}, ky[] = {0, 0, 0, 0, 0, -1};
    int nr = (g.rot + dir) & 3;
    for (int i = 0; i < 6; i++)
        if (fits(g.cur, nr, g.x + kx[i], g.y + ky[i])) {
            g.rot = (uint8_t)nr; g.x = (int8_t)(g.x + kx[i]); g.y = (int8_t)(g.y + ky[i]); g.dirty = true;
            touched(nowms()); snd(1319, 0, 25);
            return true;
        }
    return false;
}

static void spawn(uint32_t now) {
    g.cur = g.next; g.next = take(); g.rot = 0;
    g.x = g.cur == 1 ? 4 : 3; g.y = 1;
    g.hold_used = false; g.touch = false; g.resets = 0;
    g.t_grav = now + grav_ms();
    g.dirty = true;
    if (!fits(g.cur, g.rot, g.x, g.y)) {
        g.over = true;
        snd(392, 0, 140); snd(330, 160, 140); snd(262, 320, 140); snd(196, 480, 260);
        if (g.score > ex.hiscore) cfg_set_hiscore(g.score);
    }
}

static void finish_clear(uint32_t now) {
    int n = 0;
    for (int r = 0; r < BH; r++) if (g.clear_mask >> r & 1) n++;
    int w = BH - 1;
    for (int r = BH - 1; r >= 0; r--) {
        if (g.clear_mask >> r & 1) continue;
        if (w != r) memcpy(g.b[w], g.b[r], BW);
        w--;
    }
    for (; w >= 0; w--) memset(g.b[w], 0, BW);
    static const uint16_t PTS[5] = {0, 100, 300, 500, 800};
    g.score += (uint32_t)PTS[n] * (g.level + 1u);
    g.lines += (uint32_t)n;
    uint8_t lv = (uint8_t)(g.lines / 10 > 20 ? 20 : g.lines / 10);
    if (lv > g.level) { g.level = lv; snd(1568, 200, 90); snd(2093, 300, 140); }
    g.clearing = false; g.clear_mask = 0;
    spawn(now);
}

static void lock_piece(uint32_t now) {
    bool above = false;
    for (int k = 0; k < 4; k++) {
        int cx = g.x + SH[g.cur][g.rot][k][0], cy = g.y + SH[g.cur][g.rot][k][1];
        if (cy < 2) above = true;
        if (cy >= 0 && cy < BH) g.b[cy][cx] = (uint8_t)(g.cur + 1);
    }
    uint32_t mask = 0;
    for (int r = 0; r < BH; r++) {
        bool full = true;
        for (int c = 0; c < BW && full; c++) if (!g.b[r][c]) full = false;
        if (full) mask |= 1u << r;
    }
    g.dirty = true;
    if (mask) {
        g.clearing = true; g.clear_mask = mask; g.t_clear = now + 240;
        int n = 0; for (int r = 0; r < BH; r++) if (mask >> r & 1) n++;
        static const uint16_t F[4] = {1047, 1319, 1568, 2093};
        for (int i = 0; i < n; i++) snd(F[i], (uint32_t)i * 70, 90);
    } else if (above) {
        g.over = true;
        snd(392, 0, 140); snd(330, 160, 140); snd(262, 320, 140); snd(196, 480, 260);
        if (g.score > ex.hiscore) cfg_set_hiscore(g.score);
    } else {
        snd(g.kd ? 262 : 165, 0, 45);
        spawn(now);
    }
}

static void do_hold(uint32_t now) {
    if (g.hold_used) return;
    uint8_t keep = g.cur;
    if (g.hold < 7) { g.cur = g.hold; g.hold = keep; g.rot = 0; g.x = g.cur == 1 ? 4 : 3; g.y = 1;
        g.touch = false; g.resets = 0; g.t_grav = now + grav_ms(); }
    else { g.hold = keep; spawn(now); }
    g.hold_used = true; g.dirty = true;
    snd(880, 0, 30);
}

static void hard_drop(uint32_t now) {
    int d = 0;
    while (fits(g.cur, g.rot, g.x, g.y + 1)) { g.y++; d++; }
    g.score += 2u * (uint32_t)d;
    snd(196, 0, 60);
    lock_piece(now);
}

// ---- phím: idx = hàng*7 + cột ----
typedef enum { A_NONE, A_LEFT, A_RIGHT, A_DOWN, A_CW, A_CCW, A_HARD, A_HOLD, A_PAUSE } act_t;
static act_t map_key(uint8_t i) {
    switch (i) {
    case 28: case 17: return A_LEFT;       // ←  và 4
    case 30: case 19: return A_RIGHT;      // →  và 6
    case 29: case 18: return A_DOWN;       // ↓  và 5
    case 22: case 11: case 12: return A_CW;    // Esc, 8, 9
    case 10: case 7:  return A_CCW;        // 7, Ctrl+Y
    case 31: case 27: return A_HARD;       // phím 0, Enter
    case 33: case 14: return A_HOLD;       // phím ., Ctrl+S
    case 26: return A_PAUSE;               // Del
    default: return A_NONE;
    }
}

static void reset_game(void) {
    uint32_t seed = time_us_32() ^ nowms() * 2654435761u;
    memset(&g, 0, sizeof g);
    g.rng = seed ? seed : 1;
    g.bag_i = 7; g.hold = 7;
    g.next = take();
    g.on = true; g.dirty = true;
    spawn(nowms());
    g.t_mus = nowms() + 400;
}

void game_start(void) { if (!shapes_ready) init_shapes(); reset_game(); }

void game_stop(void) { g.on = false; snd_all_off(); audio_release(MUSIC_KEY); }
bool game_active(void) { return g.on; }

void game_key(uint8_t idx, bool down) {
    if (!g.on) return;
    act_t a = map_key(idx);
    if (a == A_NONE) return;
    uint32_t now = nowms();
    if (!down) {
        if (a == A_LEFT) g.kl = false; else if (a == A_RIGHT) g.kr = false; else if (a == A_DOWN) g.kd = false;
        return;
    }
    if (g.over) { if (a == A_HARD || a == A_CW || a == A_CCW) { game_start(); } return; }
    if (a == A_PAUSE) {
        g.paused = !g.paused; g.dirty = true;
        if (!g.paused) { g.t_grav = now + grav_ms(); g.t_mus = now + 200; }
        return;
    }
    if (g.paused) return;
    if (a == A_LEFT)  { g.kl = true; g.kr = false; g.t_das = now + 170; if (!g.clearing) try_move(-1, 0); }
    else if (a == A_RIGHT) { g.kr = true; g.kl = false; g.t_das = now + 170; if (!g.clearing) try_move(1, 0); }
    else if (a == A_DOWN)  { g.kd = true; g.t_soft = now + 60; if (!g.clearing && try_move(0, 1)) g.score++; }
    else if (g.clearing) return;
    else if (a == A_CW)  try_rotate(1);
    else if (a == A_CCW) try_rotate(-1);
    else if (a == A_HARD) hard_drop(now);
    else if (a == A_HOLD) do_hold(now);
}

// ---- vẽ ----
static void cell3(int x, int y, int style) {      // 0 đặc, 1 viền rỗng, 2 bóng
    if (style == 2) { oled_fill(x + 1, y + 1, 1, 1); return; }
    oled_fill(x, y, 3, 3);
    if (style == 1) oled_clr(x + 1, y + 1, 1, 1);
}

static void preview(int p, int x0, int y0, bool dim) {
    for (int k = 0; k < 4; k++) {
        int x = x0 + 4 * SH[p][0][k][0], y = y0 + 4 * SH[p][0][k][1];
        oled_fill(x, y, 3, 3);
        if (dim) oled_clr(x + 1, y + 1, 1, 1);
    }
}

static void draw(uint32_t now) {
    char buf[16];
    oled_clear();
    oled_fill(3, 1, 1, 62); oled_fill(34, 1, 1, 62); oled_fill(3, 1, 32, 1); oled_fill(3, 62, 32, 1);
    bool blink = (now / 70) & 1;
    for (int r = 2; r < BH; r++)
        for (int c = 0; c < BW; c++) {
            int sy = 2 + 3 * (r - 2), sx = 4 + 3 * c;
            if (g.clearing && (g.clear_mask >> r & 1)) { if (blink) cell3(sx, sy, 0); continue; }
            if (g.b[r][c]) cell3(sx, sy, 1);
        }
    if (!g.over && !g.clearing) {
        int gy = g.y;
        while (fits(g.cur, g.rot, g.x, gy + 1)) gy++;
        for (int k = 0; k < 4; k++) {
            int cx = g.x + SH[g.cur][g.rot][k][0], cy = gy + SH[g.cur][g.rot][k][1];
            if (cy >= 2) cell3(4 + 3 * cx, 2 + 3 * (cy - 2), 2);
        }
        for (int k = 0; k < 4; k++) {
            int cx = g.x + SH[g.cur][g.rot][k][0], cy = g.y + SH[g.cur][g.rot][k][1];
            if (cy >= 2) cell3(4 + 3 * cx, 2 + 3 * (cy - 2), 0);
        }
    }
    oled_text(44, 0, "NEXT", 1);  preview(g.next, 46, 10, false);
    oled_text(88, 0, "HOLD", 1);  if (g.hold < 7) preview(g.hold, 90, 10, g.hold_used);
    oled_text(44, 26, "SCORE", 1); snprintf(buf, sizeof buf, "%lu", (unsigned long)g.score); oled_text(44, 34, buf, 1);
    oled_text(88, 26, "LV", 1);    snprintf(buf, sizeof buf, "%u", g.level + 1u);             oled_text(88, 34, buf, 1);
    oled_text(44, 46, "BEST", 1);  snprintf(buf, sizeof buf, "%lu", (unsigned long)(g.score > ex.hiscore ? g.score : ex.hiscore)); oled_text(44, 54, buf, 1);
    oled_text(88, 46, "LINES", 1); snprintf(buf, sizeof buf, "%lu", (unsigned long)g.lines);  oled_text(88, 54, buf, 1);
    if (g.over || g.paused) {
        oled_clr(37, 20, 90, 26);
        oled_fill(37, 20, 90, 1); oled_fill(37, 45, 90, 1); oled_fill(37, 20, 1, 26); oled_fill(126, 20, 1, 26);
        if (g.over) { oled_text(55, 24, "GAME OVER", 1); oled_text(43, 35, "BAM 0 DE CHOI", 1); }
        else        { oled_text(58, 24, "TAM DUNG", 1);  oled_text(43, 35, "DEL: TIEP TUC", 1); }
    }
    oled_flush();
}

// ---- vòng lặp ----
void game_task(uint32_t now) {
    if (!g.on) return;
    snd_task(now);
    if (!g.paused && !g.over) {
        if (g.clearing) {
            g.dirty = true;
            if (now >= g.t_clear) finish_clear(now);
        } else {
            if ((g.kl || g.kr) && now >= g.t_das) { try_move(g.kl ? -1 : 1, 0); g.t_das = now + 50; }
            if (g.kd && now >= g.t_soft) { if (try_move(0, 1)) g.score++; g.t_soft = now + 45; g.t_grav = now + grav_ms(); }
            if (now >= g.t_grav) { g.t_grav = now + grav_ms(); try_move(0, 1); }
            if (!fits(g.cur, g.rot, g.x, g.y + 1)) {
                if (!g.touch) { g.touch = true; g.t_lock = now + LOCK_MS; g.resets = 0; }
                else if (now >= g.t_lock) lock_piece(now);
            } else g.touch = false;
        }
        if (!(ex.flags & 8) && now >= g.t_mus) {
            audio_release(MUSIC_KEY);
            uint8_t n = MEL[g.mus_i][0], d = MEL[g.mus_i][1];
            if (n) audio_lead(midi_to_hz(n), MUSIC_KEY);
            g.mus_note = n != 0;
            g.t_mus = now + d * EIGHTH_MS;
            g.mus_i = (g.mus_i + 1) % NMEL;
        } else if ((ex.flags & 8) && g.mus_note) { audio_release(MUSIC_KEY); g.mus_note = false; }
    } else if (g.mus_note) { audio_release(MUSIC_KEY); g.mus_note = false; }
    if (!oled_busy() && (g.dirty || now - g.t_draw >= 250)) { draw(now); g.dirty = false; g.t_draw = now; }
}
