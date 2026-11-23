// Phát bài lưu trong flash bằng bộ phát âm của bàn phím (không cần máy tính).
// Ô nhớ: 24 byte tiêu đề {'S','O','N','G', số nốt (u16), tên 18 byte}, sau đó mỗi nốt 4 byte
// {dt u16 = ms kể từ nốt trước, cao độ MIDI u8, độ dài u8 tính theo 20 ms}.
#include <string.h>
#include "pico/stdlib.h"
#include "config.h"
#include "audio.h"
#include "player.h"

#define MAGIC 0x474E4F53u
#define MAXEV ((SONG_SECTOR - 24) / 4)

typedef struct __attribute__((packed)) { uint32_t magic; uint16_t count; char name[18]; } shdr_t;
typedef struct __attribute__((packed)) { uint16_t dt; uint8_t pitch, dur; } sev_t;

static const uint8_t *slot_ptr(uint8_t s) { return (const uint8_t *)(XIP_BASE + SONG_BASE + (uint32_t)s * SONG_SECTOR); }

bool song_info(uint8_t s, uint16_t *cnt, char *name18) {
    if (s >= NSONG) return false;
    const shdr_t *h = (const shdr_t *)slot_ptr(s);
    if (h->magic != MAGIC || h->count == 0 || h->count > MAXEV) return false;
    *cnt = h->count;
    memcpy(name18, h->name, 18);
    name18[17] = 0;
    return true;
}

static struct { bool on; uint8_t slot; uint16_t idx, count; uint32_t t0, tabs; char name[18]; } pl;
static struct { uint8_t key; uint32_t due; } rel[16];
static uint8_t keyc;

static uint32_t scaled(uint32_t ms) {
    uint32_t t = ex.song_tempo < 25 ? 100 : ex.song_tempo;
    return ms * 100 / t;
}

static void begin(uint32_t now) {
    const sev_t *ev = (const sev_t *)(slot_ptr(pl.slot) + sizeof(shdr_t));
    pl.idx = 0; pl.t0 = now; pl.tabs = ev[0].dt;
    memset(rel, 0, sizeof rel);
}

void player_start(uint8_t slot) {
    uint16_t cnt;
    char nm[18];
    if (!song_info(slot, &cnt, nm)) return;
    player_stop();
    pl.slot = slot; pl.count = cnt;
    memcpy(pl.name, nm, sizeof pl.name);
    begin(to_ms_since_boot(get_absolute_time()));
    pl.on = true;
}

void player_stop(void) {
    if (!pl.on) return;
    pl.on = false;
    for (int i = 0; i < 16; i++) if (rel[i].key) { audio_release(rel[i].key); rel[i].key = 0; }
}

bool player_active(void) { return pl.on; }
const char *player_name(void) { return pl.name; }

void player_task(uint32_t now) {
    if (!pl.on) return;
    const sev_t *ev = (const sev_t *)(slot_ptr(pl.slot) + sizeof(shdr_t));
    while (pl.idx < pl.count && now - pl.t0 >= scaled(pl.tabs)) {
        sev_t e = ev[pl.idx];
        int f = -1;
        for (int i = 0; i < 16; i++) if (!rel[i].key) { f = i; break; }
        if (f < 0) { audio_release(rel[0].key); f = 0; }
        uint8_t key = (uint8_t)(100 + (keyc++ % 28));
        audio_play_hz(midi_to_hz(e.pitch), key);
        rel[f].key = key;
        rel[f].due = now + scaled(e.dur * 20u);
        pl.idx++;
        if (pl.idx < pl.count) pl.tabs += ev[pl.idx].dt;
    }
    bool any = false;
    for (int i = 0; i < 16; i++) {
        if (!rel[i].key) continue;
        if (now >= rel[i].due) { audio_release(rel[i].key); rel[i].key = 0; } else any = true;
    }
    if (pl.idx >= pl.count && !any) {
        if (ex.song_loop) begin(now); else pl.on = false;
    }
}
