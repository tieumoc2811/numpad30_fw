// Tamagotchi mini trên OLED 128x64 — xem pet.h để biết cách hoạt động.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "ssd1306.h"
#include "audio.h"
#include "player.h"
#include "pet.h"

// ================= THÔNG SỐ CÂN BẰNG (chỉnh ở đây) =================
#define TICK_MS       30000u              // 1 nhịp mô phỏng = 30 giây BẬT MÁY
#define EGG_TICKS     20u                 // trứng nở sau ~10 phút
#define BABY_TICKS    (EGG_TICKS + 240u)  // sơ sinh -> nhóc sau ~2 giờ
#define CHILD_TICKS   (BABY_TICKS + 1440u)// nhóc -> trưởng thành sau ~12 giờ
#define FOOD_DECAY    8                   // cứ 8 nhịp (4 phút) mất 1 điểm no
#define JOY_DECAY     10
#define ENERGY_DECAY  12
#define CLEAN_DECAY   9
#define POOP_MIN      60                  // 30..60 phút thì ị một lần
#define POOP_RANGE    60
#define STARVE_DIE    480u                // đói lả (no = 0) liên tục 4 giờ thì chết
#define SICK_DIE      480u                // ốm không chữa 4 giờ thì chết
#define ALERT_TICKS   40u                 // tối đa 20 phút kêu nhắc một lần
#define SAVE_TICKS    60u                 // định kỳ lưu mỗi 30 phút
#define AUTO_EXIT_MS  120000u             // chế độ tương tác tự thoát sau 2 phút không bấm
#define FRAME_MS      125u                // 8 khung hình / giây

// ================= flash =================
#define PET_MAGIC  0x50455431u
#define NPETSEC    4
#define PET_BASE   (SONG_BASE - SONG_SECTOR * NPETSEC)   // ngay dưới các ô bài MIDI

enum { ST_EGG, ST_BABY, ST_CHILD, ST_ADULT, ST_DEAD };
enum { F_SICK = 1, F_SLEEP = 2, F_MUTE = 4, F_NOAMB = 8 };

typedef struct {
    uint32_t magic, seq;
    uint32_t age;                       // số nhịp đã sống
    uint16_t sick_t, starve_t;
    uint8_t  food, joy, energy, clean;  // 0..100
    uint8_t  stage, poop, flags, wins;
    uint32_t csum;
} rec_t;
_Static_assert(sizeof(rec_t) <= FLASH_PAGE_SIZE, "rec_t too big");

static rec_t P;
static uint8_t cur_sec;
static uint8_t wbuf[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
static uint32_t w_off;

static uint32_t csum(const rec_t *r) {
    const uint32_t *w = (const uint32_t *)r;
    uint32_t s = 0x5A17C0DEu;
    for (unsigned i = 0; i < sizeof *r / 4 - 1; i++) s = ((s << 5) | (s >> 27)) ^ w[i];
    return s;
}

static void save_job(void *p) {
    (void)p;
    flash_range_erase(w_off, FLASH_SECTOR_SIZE);
    flash_range_program(w_off, wbuf, FLASH_PAGE_SIZE);
}

// ================= trạng thái chạy =================
typedef enum { M_SCENE, M_INFO, M_GAME } pmode_t;   // (không đặt tên mode_t: trùng newlib)
enum { A_NONE, A_EAT, A_CLEAN, A_HEAL, A_HAPPY };

static uint32_t rs = 2463534242u;
static uint32_t t_tick, tick_n, last_alert;
static uint16_t poop_t, poop_goal;
static uint8_t  snacks, menu, typed;
static bool     active, amb, save_req;
static pmode_t  mode;
static uint32_t last_key, last_typed, last_frame, next_save_ok;
static char     msg[24];
static uint32_t msg_until;
static uint8_t  act_kind;
static uint32_t act_t0, act_until;
static int      px = 20, pd = 1, pause_f;
static uint32_t frame_n;
static int      mg_round, mg_wins, mg_dir;
static bool     mg_reveal, mg_ok;
static uint32_t mg_t;

static inline uint32_t nowms(void) { return to_ms_since_boot(get_absolute_time()); }
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static void clampi(uint8_t *v, int d) { int x = (int)*v + d; *v = (uint8_t)(x < 0 ? 0 : x > 100 ? 100 : x); }
static void say(const char *s, uint32_t ms) {
    strncpy(msg, s, sizeof msg - 1); msg[sizeof msg - 1] = 0; msg_until = nowms() + ms;
}
static void anim(uint8_t kind, uint32_t ms) { act_kind = kind; act_t0 = nowms(); act_until = act_t0 + ms; }

// ================= âm thanh (1 giọng, bỏ qua nếu tắt tiếng) =================
#define PET_KEY 220
typedef struct { uint16_t hz, ms; } nt_t;
static const nt_t S_OK[]     = {{1319,60},{1760,90},{0,0}};
static const nt_t S_NO[]     = {{220,120},{165,170},{0,0}};
static const nt_t S_EAT[]    = {{523,70},{0,40},{523,70},{0,40},{659,70},{0,40},{784,120},{0,0}};
static const nt_t S_WIN[]    = {{784,90},{988,90},{1175,90},{1568,200},{0,0}};
static const nt_t S_LOSE[]   = {{392,120},{330,120},{262,260},{0,0}};
static const nt_t S_EVOLVE[] = {{523,100},{659,100},{784,100},{1047,100},{784,100},{1047,100},{1319,320},{0,0}};
static const nt_t S_DIE[]    = {{440,250},{392,250},{349,250},{262,650},{0,0}};
static const nt_t S_ALERT[]  = {{1568,70},{0,60},{1568,70},{0,0}};

static struct { const nt_t *seq; uint8_t i; uint32_t at; bool on; } sf;

static void sfx(const nt_t *s) {
    if (sf.on) { audio_release(PET_KEY); sf.on = false; }
    sf.seq = s; sf.i = 0; sf.at = nowms();
}
static void sfx_task(uint32_t now) {
    if (!sf.seq || (int32_t)(now - sf.at) < 0) return;
    if (sf.on) { audio_release(PET_KEY); sf.on = false; }
    nt_t n = sf.seq[sf.i];
    if (!n.ms) { sf.seq = 0; return; }
    sf.i++;
    if (n.hz && !(P.flags & F_MUTE)) { audio_lead(n.hz, PET_KEY); sf.on = true; }
    sf.at = now + n.ms;
}

// ================= hình (ASCII, '#' = điểm sáng) — sửa thoải mái =================
static const char *const SP_EGG[16] = {
    "................",
    ".....######.....",
    "...##......##...",
    "..#..........#..",
    "..#...#..#...#..",
    ".#............#.",
    ".#....##......#.",
    ".#............#.",
    ".#.....#...#..#.",
    ".#............#.",
    "..#....##....#..",
    "..#..........#..",
    "...##......##...",
    ".....######.....",
    "................",
    "................"};
static const char *const SP_BABY_A[16] = {
    "................",
    "................",
    ".....######.....",
    "...##########...",
    "..############..",
    ".##############.",
    ".###..####..###.",
    ".###..####..###.",
    ".##############.",
    ".##############.",
    ".######..######.",
    ".##############.",
    "..############..",
    "...##########...",
    ".....######.....",
    "................"};
static const char *const SP_BABY_B[16] = {
    "................",
    "................",
    ".....######.....",
    "...##########...",
    "..############..",
    ".##############.",
    ".###..####..###.",
    ".##############.",
    ".##############.",
    ".####......####.",
    ".#####....#####.",
    ".##############.",
    "..############..",
    "...##########...",
    ".....######.....",
    "................"};
static const char *const SP_CHILD_A[16] = {
    "..##........##..",
    "..###......###..",
    "..############..",
    ".##############.",
    ".##..######..##.",
    ".##..######..##.",
    ".##############.",
    ".##############.",
    ".###.######.###.",
    ".##############.",
    "..############..",
    "..############..",
    "..##.######.##..",
    "..##........##..",
    "..###......###..",
    "................"};
static const char *const SP_CHILD_B[16] = {
    "..##........##..",
    "..###......###..",
    "..############..",
    ".##..######..##.",
    ".##############.",
    ".##############.",
    ".##############.",
    ".##############.",
    ".###.#....#.###.",
    ".##############.",
    "..############..",
    "..############..",
    "..##.######.##..",
    "..##........##..",
    "..###......###..",
    "................"};
static const char *const SP_ADULT_A[16] = {
    "..##........##..",
    "..###......###..",
    ".##############.",
    ".##..######..##.",
    ".##..######..##.",
    ".##############.",
    "###.########.###",
    "###.########.###",
    "###..######..###",
    ".##..######..##.",
    "..############..",
    "..############..",
    "..##.######.##..",
    "..##........##..",
    ".###........###.",
    "................"};
static const char *const SP_ADULT_B[16] = {
    "..##........##..",
    "..###......###..",
    ".##############.",
    ".##..######..##.",
    ".##############.",
    ".##############.",
    "###.########.###",
    "###.#......#.###",
    "###..######..###",
    ".##..######..##.",
    "..############..",
    "..############..",
    "..##.######.##..",
    "..##........##..",
    ".###........###.",
    "................"};
static const char *const SP_GHOST[16] = {
    "................",
    ".....######.....",
    "...##########...",
    "..############..",
    "..##..####..##..",
    "..##..####..##..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..##.######.##..",
    "..#...####...#..",
    "................",
    "................",
    "................"};
static const char *const *const SPR[5][2] = {
    {SP_EGG, SP_EGG}, {SP_BABY_A, SP_BABY_B}, {SP_CHILD_A, SP_CHILD_B},
    {SP_ADULT_A, SP_ADULT_B}, {SP_GHOST, SP_GHOST}};

enum { I_FEED, I_SNACK, I_PLAY, I_CLEAN, I_HEAL, I_LIGHT, I_INFO, I_SETUP, I_POOP, I_HEART };
static const char *const IC[10][8] = {
    {"..#..#..", ".#..#...", "........", "########", "#......#", ".#....#.", "..####..", "........"},  // cơm
    {"..####..", ".######.", "##.##.##", "######.#", "#.######", "##.##.##", ".######.", "..####.."},  // bánh
    {"..####..", ".#.##.#.", "#..##..#", "########", "########", "#..##..#", ".#.##.#.", "..####.."},  // bóng
    {"...#....", "..###...", ".#####..", ".#####..", "#######.", "#######.", ".#####..", "..###..."},  // giọt nước
    {"..####..", "..####..", "########", "########", "########", "########", "..####..", "..####.."},  // thập
    {"..####..", ".#....#.", "#......#", "#......#", ".#....#.", "..####..", "..####..", "...##..."},  // bóng đèn
    {"........", "......#.", "..#...#.", "..#.#.#.", "#.#.#.#.", "#.#.#.#.", "#.#.#.#.", "########"},  // biểu đồ
    {"....#...", "...##.#.", "####.#.#", "####.#.#", "####.#.#", "...##.#.", "....#...", "........"},  // loa
    {"...#....", "..##....", "...###..", "..#####.", ".#######", "########", "########", ".######."},  // phân
    {".##..##.", "########", "########", "########", ".######.", "..####..", "...##...", "........"}}; // tim

static const char *const STN[5] = {"TRUNG", "SO SINH", "NHOC", "TRUONG THANH", "DA MAT"};
static const char *const LBL[8] = {"AN COM", "AN VAT", "CHOI GAME", "TAM RUA", "UONG THUOC", "DEN / NGU", "TRANG THAI", "CAI DAT"};
static const char *const SETUP_TXT[4] = {"AM THANH + MAN CHO", "IM LANG + MAN CHO", "AM THANH, TAT MAN CHO", "IM LANG, TAT MAN CHO"};

// ================= mô phỏng =================
static void new_egg(void) {
    uint8_t keep = P.flags & (F_MUTE | F_NOAMB), wins = P.wins;
    uint32_t seq = P.seq;
    memset(&P, 0, sizeof P);
    P.seq = seq; P.flags = keep; P.wins = wins;
    P.stage = ST_EGG;
    P.food = P.joy = P.energy = P.clean = 80;
    poop_t = 0; snacks = 0; tick_n = 0;
    save_req = true;
}

static void evolve(uint8_t st) {
    P.stage = st;
    sfx(S_EVOLVE);
    anim(A_HAPPY, 3000);
    say(st == ST_BABY ? "TRUNG NO ROI!" : "TIEN HOA!", 3000);
    clampi(&P.joy, 20);
    save_req = true;
}

static void die(void) {
    P.stage = ST_DEAD; P.flags &= (uint8_t)~(F_SICK | F_SLEEP); P.poop = 0;
    sfx(S_DIE);
    save_req = true;
}

static const char *alert_msg(void) {
    if (P.stage == ST_DEAD) return "NHAN OK: TRUNG MOI";
    if (P.stage == ST_EGG) return NULL;
    if (P.flags & F_SLEEP) return "ZZZ...";
    if (P.flags & F_SICK) return "OM ROI! CAN THUOC";
    if (P.poop) return "CAN DON PHAN!";
    if (P.food < 25) return "DOI BUNG!";
    if (P.joy < 25) return "BUON QUA, CHOI DI";
    if (P.energy < 20) return "BUON NGU ROI";
    return NULL;
}

static void step(void) {
    P.age++; tick_n++;
    if (P.stage == ST_DEAD) return;
    if (P.stage == ST_EGG) { if (P.age >= EGG_TICKS) evolve(ST_BABY); return; }

    bool slp = P.flags & F_SLEEP;
    if (tick_n % FOOD_DECAY == 0 && (!slp || ((tick_n / FOOD_DECAY) & 1))) clampi(&P.food, -1);
    if (tick_n % JOY_DECAY == 0 && !slp) clampi(&P.joy, (P.flags & F_SICK) ? -2 : -1);
    if (tick_n % CLEAN_DECAY == 0) clampi(&P.clean, -(1 + P.poop));
    if (slp) {
        clampi(&P.energy, 3);
        if (P.energy >= 100) { P.flags &= (uint8_t)~F_SLEEP; say("DA THUC DAY!", 2500); }
    } else if (tick_n % ENERGY_DECAY == 0) {
        clampi(&P.energy, -1);
        if (P.energy <= 5) P.flags |= F_SLEEP;       // kiệt sức: tự đi ngủ
    }
    if (!slp && P.food > 0 && ++poop_t >= poop_goal) {
        poop_t = 0; poop_goal = (uint16_t)(POOP_MIN + rnd() % POOP_RANGE);
        if (P.poop < 3) P.poop++;
    }
    if (snacks && tick_n % 20 == 0) snacks--;

    if (!(P.flags & F_SICK) && tick_n % 20 == 0) {   // 10 phút xét một lần
        int risk = (P.clean < 20 ? 2 : 0) + (P.poop >= 3 ? 2 : 0) + (P.food < 10 ? 1 : 0) + (P.joy < 10 ? 1 : 0);
        if (risk && (int)(rnd() % 32) < risk) { P.flags |= F_SICK; say("BI OM ROI!", 3000); }
    }
    if (P.flags & F_SICK) { if (++P.sick_t > SICK_DIE) { die(); return; } } else P.sick_t = 0;
    if (P.food == 0)      { if (++P.starve_t > STARVE_DIE) { die(); return; } } else P.starve_t = 0;

    if (P.stage == ST_BABY && P.age >= BABY_TICKS) evolve(ST_CHILD);
    else if (P.stage == ST_CHILD && P.age >= CHILD_TICKS) evolve(ST_ADULT);

    if (tick_n % SAVE_TICKS == 0) save_req = true;
    if ((active || amb) && !(P.flags & F_SLEEP) && alert_msg() && tick_n - last_alert >= ALERT_TICKS) {
        last_alert = tick_n; sfx(S_ALERT);
    }
}

// ================= hành động =================
static void deny(const char *s) { say(s, 1600); sfx(S_NO); }

static void do_action(int i) {
    if (P.stage == ST_DEAD) return;
    if (P.stage == ST_EGG) { deny("TRUNG CHUA NO"); return; }
    bool slp = P.flags & F_SLEEP;
    switch (i) {
    case 0:   // ăn cơm
        if (slp) { deny("DANG NGU MA!"); break; }
        if (P.food >= 95) { deny("NO ROI, KHONG AN"); break; }
        clampi(&P.food, 30); clampi(&P.joy, 2);
        anim(A_EAT, 1800); sfx(S_EAT); save_req = true;
        break;
    case 1:   // ăn vặt: vui hơn nhưng ăn nhiều sẽ đau bụng
        if (slp) { deny("DANG NGU MA!"); break; }
        clampi(&P.food, 8); clampi(&P.joy, 15);
        anim(A_EAT, 1800); sfx(S_EAT);
        if (++snacks >= 5 && !(P.flags & F_SICK)) { P.flags |= F_SICK; say("DAU BUNG ROI!", 2500); }
        else say("NGON QUA!", 1500);
        save_req = true;
        break;
    case 2:   // chơi đoán hướng
        if (slp) { deny("DANG NGU MA!"); break; }
        if (P.flags & F_SICK) { deny("DANG OM, KHONG CHOI"); break; }
        if (P.energy < 15) { deny("MET QUA, DE NGU DA"); break; }
        mode = M_GAME; mg_round = 0; mg_wins = 0; mg_reveal = false; mg_dir = (int)(rnd() & 1);
        break;
    case 3:   // tắm / dọn
        if (slp) { deny("DANG NGU MA!"); break; }
        if (!P.poop && P.clean >= 90) { deny("DA SACH ROI"); break; }
        P.poop = 0; P.clean = 100;
        anim(A_CLEAN, 1800); sfx(S_OK); save_req = true;
        break;
    case 4:   // thuốc
        if (!(P.flags & F_SICK)) { deny("KHONG OM, KHONG UONG"); break; }
        P.flags &= (uint8_t)~F_SICK; P.sick_t = 0; snacks = 0; clampi(&P.joy, -5);
        anim(A_HEAL, 1800); sfx(S_OK); say("KHOI BENH ROI!", 2000); save_req = true;
        break;
    case 5:   // đèn: ngủ / thức
        if (slp) {
            P.flags &= (uint8_t)~F_SLEEP;
            if (P.energy < 60) { clampi(&P.joy, -5); say("CHUA NGU DU!", 1800); } else say("DA THUC DAY", 1500);
        } else { P.flags |= F_SLEEP; say("CHUC NGU NGON", 1500); }
        sfx(S_OK); save_req = true;
        break;
    case 6: mode = M_INFO; break;
    case 7: {
        int s = (((P.flags & F_MUTE) ? 1 : 0) | ((P.flags & F_NOAMB) ? 2 : 0));
        s = (s + 1) & 3;
        P.flags = (uint8_t)((P.flags & ~(F_MUTE | F_NOAMB)) | ((s & 1) ? F_MUTE : 0) | ((s & 2) ? F_NOAMB : 0));
        say(SETUP_TXT[s], 2500); sfx(S_OK); save_req = true;
        break; }
    }
}

static void mg_guess(int g) {
    if (mg_reveal) return;
    mg_ok = (g == mg_dir);
    if (mg_ok) mg_wins++;
    mg_reveal = true; mg_t = nowms() + 1000;
    sfx(mg_ok ? S_OK : S_NO);
}

static void mg_finish(void) {
    bool win = mg_wins >= 3;
    mode = M_SCENE;
    clampi(&P.joy, win ? 25 : 8); clampi(&P.energy, -12); clampi(&P.food, -4);
    if (win && P.wins < 255) P.wins++;
    char b[40];
    snprintf(b, sizeof b, "THANG %d/5 %s", mg_wins, win ? "VUI QUA!" : "HUHU");
    say(b, 2500);
    if (win) anim(A_HAPPY, 2500);
    sfx(win ? S_WIN : S_LOSE);
    save_req = true;
}

// Phím: idx = hàng*7 + cột
#define K_ESC   22
#define K_DEL   26
#define K_ENTER 27
#define K_LEFT  28
#define K_DOWN  29
#define K_RIGHT 30
#define K_ZERO1 31
#define K_ZERO2 32

static int shortcut(uint8_t idx) {   // cụm số 7 8 9 / 4 5 6 / 1 2 = 8 mục menu
    switch (idx) {
    case 10: return 0; case 11: return 1; case 12: return 2;
    case 17: return 3; case 18: return 4; case 19: return 5;
    case 24: return 6; case 25: return 7;
    default: return -1;
    }
}

void pet_key(uint8_t idx, bool down) {
    if (!active || !down) return;
    uint32_t now = nowms();
    last_key = now; last_frame = 0; rs ^= now * 2654435761u;
    bool ok = idx == K_ENTER || idx == K_DOWN || idx == K_ZERO1 || idx == K_ZERO2;
    bool esc = idx == K_ESC || idx == K_DEL;

    if (P.stage == ST_DEAD) {
        if (ok) { new_egg(); sfx(S_OK); } else if (esc) pet_stop();
        return;
    }
    if (mode == M_INFO) { if (ok || esc || idx == K_LEFT || idx == K_RIGHT) mode = M_SCENE; return; }
    if (mode == M_GAME) {
        if (idx == K_LEFT) mg_guess(0);
        else if (idx == K_RIGHT) mg_guess(1);
        else if (esc) { mode = M_SCENE; say("DA HUY", 1000); }
        return;
    }
    if (esc) { pet_stop(); return; }
    if (idx == K_LEFT)  { menu = (uint8_t)((menu + 7) & 7); return; }
    if (idx == K_RIGHT) { menu = (uint8_t)((menu + 1) & 7); return; }
    int it = shortcut(idx);
    if (it >= 0) { menu = (uint8_t)it; ok = true; }
    if (ok && !(act_kind && now < act_until)) do_action(menu);
}

void pet_typed(void) {
    last_typed = nowms();
    if (++typed >= 150) {            // gõ phím nhiều thì thú vui lên một chút
        typed = 0;
        if (P.stage >= ST_BABY && P.stage != ST_DEAD && !(P.flags & F_SLEEP)) clampi(&P.joy, 1);
    }
}

// ================= vẽ =================
static void blit(const char *const *s, int w, int h, int x, int y, int sc, bool flip) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            if (s[r][flip ? w - 1 - c : c] == '#') oled_fill(x + c * sc, y + r * sc, sc, sc);
}
static void spr(const char *const *s, int x, int y, bool flip) { blit(s, 16, 16, x, y, 2, flip); }
static void icon(int i, int x, int y, int sc) { blit(IC[i], 8, 8, x, y, sc, false); }

static void bar(int x, int y, int w, int h, int v) {
    oled_fill(x, y, w, 1); oled_fill(x, y + h - 1, w, 1); oled_fill(x, y, 1, h); oled_fill(x + w - 1, y, 1, h);
    int f = (w - 4) * v / 100;
    if (f > 0) oled_fill(x + 2, y + 2, f, h - 4);
}

static void age_text(char *b, size_t n) {   // b >= 24 byte
    snprintf(b, n, "%s %uH%02u", STN[P.stage], (unsigned)(P.age / 120), (unsigned)((P.age % 120) / 2));
}

static void anim_step(void) {
    frame_n++;
    if (P.stage == ST_EGG || P.stage == ST_DEAD || (P.flags & (F_SICK | F_SLEEP))) return;
    if (pause_f > 0) { pause_f--; return; }
    px += pd;
    if (px <= 2)  { px = 2;  pd = 1; }
    if (px >= 46) { px = 46; pd = -1; }
    if (rnd() % 40 == 0) pd = -pd;
    if (rnd() % 30 == 0) pause_f = 8 + (int)(rnd() % 16);
}

static const char *const *cur_sprite(uint32_t now, bool moving) {
    int fr = moving ? (int)((frame_n >> 2) & 1) : (int)((frame_n >> 3) & 1);
    if (P.flags & F_SLEEP) fr = 1;
    if (act_kind == A_EAT && now < act_until) fr = (int)(((now - act_t0) / 150) & 1);
    return SPR[P.stage][fr];
}

static void draw_scene(uint32_t now, bool menu_on) {
    oled_clear();
    if (menu_on && P.stage != ST_DEAD) {
        for (int i = 0; i < 8; i++) {
            int x = i * 16 + 4;
            icon(i, x, 1, 1);
            if (i == menu) { oled_fill(x - 3, 0, 14, 1); oled_fill(x - 3, 10, 14, 1); oled_fill(x - 3, 0, 1, 11); oled_fill(x + 10, 0, 1, 11); }
        }
    }
    // thanh trạng thái bên phải: no / vui / sức / sạch
    static const uint8_t BI[4] = {I_FEED, I_HEART, I_LIGHT, I_CLEAN};
    uint8_t v[4] = {P.food, P.joy, P.energy, P.clean};
    for (int i = 0; i < 4; i++) { int y = 13 + i * 10; icon(BI[i], 98, y, 1); bar(108, y, 20, 8, v[i]); }
    oled_fill(0, 53, 96, 1);

    bool still = P.stage == ST_EGG || P.stage == ST_DEAD || (P.flags & (F_SICK | F_SLEEP));
    int x = px, y = 21;
    if (P.stage == ST_DEAD) {
        spr(SP_GHOST, 24, 19 + (int)((now / 500) & 1), false);
        oled_text(58, 24, "RIP", 2);
    } else {
        if (P.stage == ST_EGG) { int ph = (int)((now / 300) & 3); x = 36 + (ph == 1 ? -2 : ph == 3 ? 2 : 0); }
        else if (P.flags & F_SICK) x = px + (int)(frame_n & 1) * 2 - 1;
        else if (!still) y -= (int)((frame_n >> 1) & 1);
        spr(cur_sprite(now, !still), x, y, pd < 0);
        if (P.flags & F_SLEEP) {
            oled_text(x + 34, 22 - (int)((now / 600) & 1) * 3, "Z", 1);
            oled_text(x + 42, 12, "z", 1);
        } else if ((P.flags & F_SICK) && ((now / 400) & 1)) icon(I_HEAL, x + 34, 18, 1);
        else if (P.stage > ST_EGG && alert_msg() && ((now / 400) & 1)) oled_text(x + 36, 18, "!", 1);
    }
    for (int i = 0; i < P.poop; i++) icon(I_POOP, 60 + i * 11, 45, 1);

    if (act_kind && now < act_until) {      // hiệu ứng hành động
        uint32_t t = now - act_t0;
        switch (act_kind) {
        case A_EAT: { int ix = x + 40 - (int)(t * 20 / 1800); blit(IC[I_FEED], 8, 8, ix, 28, 2, false); break; }
        case A_CLEAN: for (int k = 0; k < 4; k++) icon(I_CLEAN, x + k * 9, 14 + (int)((t / 90 + k * 5) % 34), 1); break;
        case A_HEAL:  for (int k = 0; k < 3; k++) icon(I_HEAL, x + 4 + k * 11, 44 - (int)((t / 100 + k * 7) % 26), 1); break;
        case A_HAPPY: for (int k = 0; k < 3; k++) icon(I_HEART, x + 2 + k * 12, 20 - (int)((t / 120 + k * 5) % 12), 1); break;
        }
    }

    char tmp[40]; const char *txt;
    if (now < msg_until) txt = msg;
    else if (menu_on && P.stage != ST_DEAD) txt = LBL[menu];
    else { txt = alert_msg(); if (!txt) { age_text(tmp, sizeof tmp); txt = tmp; } }
    oled_text(0, 56, txt, 1);
    oled_flush();
}

static void draw_info(void) {
    oled_clear();
    char b[40];
    age_text(b, sizeof b); oled_text(0, 0, b, 1);
    if (P.flags & F_SICK) oled_text(104, 0, "OM", 1);
    else if (P.flags & F_SLEEP) oled_text(104, 0, "NGU", 1);
    static const char *const L[4] = {"NO BUNG", "VUI VE", "NANG LUONG", "SACH SE"};
    uint8_t v[4] = {P.food, P.joy, P.energy, P.clean};
    for (int i = 0; i < 4; i++) { oled_text(0, 12 + i * 10, L[i], 1); bar(66, 11 + i * 10, 62, 9, v[i]); }
    snprintf(b, sizeof b, "THANG: %u   OK=VE", (unsigned)P.wins);
    oled_text(0, 55, b, 1);
    oled_flush();
}

static void draw_game(uint32_t now) {
    oled_clear();
    char b[40];
    snprintf(b, sizeof b, "VONG %d/5   DUNG %d", mg_round + 1, mg_wins);
    oled_text(0, 0, b, 1);
    int fr = (int)((now / 300) & 1);
    const char *const *s = SPR[P.stage][fr];
    if (!mg_reveal) {
        oled_text(15, 12, "NO QUAY HUONG NAO?", 1);
        spr(s, 48, 24, false);
        oled_text(0, 56, "< TRAI         PHAI >", 1);
    } else {
        oled_text(mg_ok ? 34 : 40, 12, mg_ok ? "DUNG!" : "SAI!", 2);
        spr(s, mg_dir ? 86 : 10, 30, mg_dir == 0);   // thú quay mặt về phía nó chọn
        oled_text(mg_dir ? 4 : 104, 40, mg_dir ? ">" : "<", 2);
    }
    oled_flush();
}

// ================= vòng lặp / điều khiển =================
static void do_save(void) {
    P.magic = PET_MAGIC; P.seq++;
    P.csum = csum(&P);
    memset(wbuf, 0xFF, sizeof wbuf);
    memcpy(wbuf, &P, sizeof P);
    cur_sec = (uint8_t)((cur_sec + 1) % NPETSEC);
    w_off = PET_BASE + (uint32_t)cur_sec * SONG_SECTOR;
    flash_safe_execute(save_job, NULL, 200);
}

void pet_task(uint32_t now) {
    sfx_task(now);
    while ((uint32_t)(now - t_tick) >= TICK_MS) { t_tick += TICK_MS; step(); }
    if (act_kind && (int32_t)(now - act_until) >= 0) act_kind = A_NONE;
    if (active && now - last_key > AUTO_EXIT_MS) pet_stop();
    if (mode == M_GAME && mg_reveal && (int32_t)(now - mg_t) >= 0) {
        mg_reveal = false; mg_round++;
        if (mg_round >= 5) mg_finish(); else mg_dir = (int)(rnd() & 1);
    }
    // Ghi flash chỉ khi không đang gõ (xoá sector làm USB chững vài chục ms) và cách lần trước >= 60 s
    if (save_req && now >= next_save_ok && now - last_typed > 3000 && now - last_key > 1500) {
        do_save(); save_req = false; next_save_ok = now + 60000;
    }
    if ((active || amb) && !oled_busy() && now - last_frame >= FRAME_MS) {
        last_frame = now;
        anim_step();
        if (active && mode == M_INFO) draw_info();
        else if (active && mode == M_GAME) draw_game(now);
        else draw_scene(now, active);
    }
}

void pet_init(void) {
    rs ^= time_us_32() | 1u;
    int best = -1; uint32_t bseq = 0;
    for (int i = 0; i < NPETSEC; i++) {
        const rec_t *r = (const rec_t *)(XIP_BASE + PET_BASE + (uint32_t)i * SONG_SECTOR);
        if (r->magic == PET_MAGIC && r->csum == csum(r) && r->stage <= ST_DEAD && (best < 0 || (int32_t)(r->seq - bseq) > 0)) {
            best = i; bseq = r->seq;
        }
    }
    if (best >= 0) {
        P = *(const rec_t *)(XIP_BASE + PET_BASE + (uint32_t)best * SONG_SECTOR);
        cur_sec = (uint8_t)best;
    } else {
        cur_sec = NPETSEC - 1;
        memset(&P, 0, sizeof P);
        new_egg();
    }
    poop_goal = (uint16_t)(POOP_MIN + rnd() % POOP_RANGE);
    t_tick = nowms();
    next_save_ok = t_tick + 60000;
}

void pet_start(void) { active = true; mode = M_SCENE; menu = 0; last_key = nowms(); last_frame = 0; }
void pet_stop(void)  { active = false; mode = M_SCENE; }
bool pet_active(void) { return active; }
bool pet_ambient_allowed(void) { return !(P.flags & F_NOAMB); }
void pet_ambient(bool on) {
    if (on && !amb) { last_frame = 0; mode = M_SCENE; }
    amb = on;
}
bool pet_showing(void) { return active || amb; }
