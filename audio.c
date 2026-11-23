// Bộ phát âm: PIO I2S + DMA vòng, trộn tối đa 8 giọng ở 22,05 kHz, đơn âm (L = R).
// Bảng 0 piano (sóng vuông + răng cưa), bảng 1 trống tổng hợp, bảng 2 guitar (Karplus-Strong).
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "i2s.pio.h"
#include "audio.h"
#include "config.h"
#include "midi.h"

#define SR       22050
#define NV       8
#define RING     1024          // số từ 32 bit, = 4096 byte
#define AHEAD    768           // luôn giữ đệm sẵn ~35 ms
#define PIN_BCLK 16            // LRCK = GP17
#define PIN_DIN  18

typedef struct {
    uint8_t kind;              // 0 tắt, 1 piano, 2 guitar, 3 trống
    uint8_t key, st;
    bool held, sq, hp;
    float ph, inc, env, edec, nenv, ndec, fp, fpd, f1, tg, ng, prev;
    float c, gain, s1, s2;      // guitar: all-pass chỉnh cao độ lẻ, độ ngân, trạng thái all-pass
    uint16_t pos, len;
    uint32_t age;
} voice_t;

typedef struct { float f0, f1, pd, td, nd, tg, ng; uint8_t sq, hp; } dp_t;
static const dp_t DP[14] = {
    {0},
    {150, 45, .03f, .25f, 0,    .9f, 0,    0, 0},   // 1 kick
    {200, 150, .02f, .10f, .15f, .4f, .5f, 0, 0},   // 2 snare
    {0, 0, 0, 0, .04f, 0, .5f,  0, 1},              // 3 hat đóng
    {0, 0, 0, 0, .30f, 0, .45f, 0, 1},              // 4 hat mở
    {260, 140, .05f, .25f, 0, .8f, 0, 0, 0},        // 5 tom cao
    {190, 100, .05f, .30f, 0, .8f, 0, 0, 0},        // 6 tom giữa
    {130, 60, .06f, .35f, 0, .9f, 0, 0, 0},         // 7 tom thấp
    {0, 0, 0, 0, 1.0f, 0, .5f, 0, 1},               // 8 crash
    {3100, 3100, .01f, .4f, .6f, .06f, .3f, 1, 1},  // 9 ride
    {0, 0, 0, 0, .12f, 0, .6f, 0, 0},               // 10 clap
    {900, 800, .01f, .04f, 0, .3f, 0, 1, 0},        // 11 rim
    {560, 560, .01f, .25f, 0, .15f, 0, 1, 0},       // 12 cowbell
    {2400, 1300, .004f, .022f, .014f, .55f, .55f, 0, 1},  // 13 tiếng gõ phím (tách ngắn)
};

static voice_t vox[NV];
static float ks[NV][480];
static float sinlut[256];
static uint32_t abuf[RING] __attribute__((aligned(4096)));
static uint32_t wr;
static int dma_ch;
static uint32_t rs = 2463534242u;

static inline float rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (int32_t)rs * (1.0f / 2147483648.0f); }
static float dec(float s) { return s > 0 ? expf(-6.9f / (s * SR)) : 0.f; }

static int alloc_voice(void) {
    int best = 0;
    for (int i = 0; i < NV; i++) {
        if (!vox[i].kind) return i;
        if (vox[i].age > vox[best].age) best = i;
    }
    return best;
}

static void voice_start(uint8_t idx, uint16_t val, uint8_t board) {
    int i = alloc_voice();
    voice_t *v = &vox[i];
    memset(v, 0, sizeof *v);
    v->key = idx; v->held = true;
    switch (board) {
    case 1: {
        if (val > 13) return;
        const dp_t *p = &DP[val];
        v->kind = 3; v->f1 = p->f1; v->fp = p->f0 - p->f1; v->fpd = dec(p->pd);
        v->env = 1; v->edec = dec(p->td); v->nenv = 1; v->ndec = dec(p->nd);
        v->tg = p->tg; v->ng = p->ng; v->sq = p->sq; v->hp = p->hp;
        break; }
    case 2: {
        // Karplus-Strong có chỉnh cao độ lẻ (all-pass), kích thích lọc thấp + vị trí gảy dây,
        // độ ngân phụ thuộc cao độ (dây trầm ngân lâu hơn dây cao).
        static float tmp[480];
        float D = (float)SR / val + 0.5f;          // bộ lọc vòng làm vòng ngắn đi 0,5 mẫu (đã kiểm bằng số đo)
        int len = (int)D;
        float d = D - len;
        if (d < 0.5f) { len--; d += 1; }           // giữ d trong [0,5; 1,5) để all-pass ổn định
        if (len < 8) { len = 8; d = 1; }
        if (len > 478) { len = 478; d = 1; }
        float w0 = 6.2831853f * val / SR;
        float e = 0;
        for (int k = 0; k < len; k++) { e += 0.55f * (rnd() - e); tmp[k] = e; }
        int pp = (int)(len * 0.17f);               // gảy gần cầu đàn
        if (pp < 1) pp = 1;
        float vel = 0.85f + 0.15f * (rnd() * 0.5f + 0.5f);
        for (int k = 0; k < len; k++) ks[i][k] = 0.9f * vel * (tmp[k] - tmp[(k + pp) % len]);
        float T = 8.0f - 0.009f * val;             // thời gian ngân T60 (giây)
        if (T < 3.5f) T = 3.5f;
        if (T > 6.5f) T = 6.5f;
        v->kind = 2; v->len = (uint16_t)len;
        v->c = sinf((1 - d) * w0 / 2) / sinf((1 + d) * w0 / 2);   // trễ đúng d mẫu tại cao độ cần chơi
        v->gain = expf(-6.9f / (T * val));
        v->nenv = 0.12f;                           // tiếng "tách" của móng gảy
        break; }
    default:
        v->kind = 1; v->inc = val / (float)SR;
        break;
    }
}

uint16_t midi_to_hz(int m) { return (uint16_t)(440.0f * powf(2.0f, (m - 69) / 12.0f) + 0.5f); }

void audio_release(uint8_t key) {
    for (int i = 0; i < NV; i++)
        if (vox[i].kind && vox[i].kind != 3 && vox[i].key == key) vox[i].held = false;
}

void audio_lead(uint16_t hz, uint8_t key) { if (hz) voice_start(key, hz, 0); }

void audio_play_hz(uint16_t hz, uint8_t key) { if (hz) voice_start(key, hz, cfg_board() == 2 ? 2 : 0); }

void music_note(uint8_t idx, bool on) {
    midi_send(idx, on);
    if (!on) { audio_release(idx); return; }
    if (midi_connected() && !ex.midi_local) return;     // chỉ gửi MIDI, không phát loa
    uint16_t val = cfg_note(idx);
    if (val) voice_start(idx, val, cfg_board());
}

static inline float sample_voice(voice_t *v, int vi) {
    float x = 0;
    v->age++;
    switch (v->kind) {
    case 1:
        v->ph += v->inc; if (v->ph >= 1) v->ph -= 1;
        if (!v->held) { v->env *= 0.9965f; if (v->env < 0.002f) { v->kind = 0; return 0; } }
        else if (v->st == 0) { v->env += 0.012f; if (v->env >= 1) { v->env = 1; v->st = 1; } }
        else if (v->env > 0.5f) v->env *= 0.99992f;
        x = v->env * ((v->ph < .5f ? .4f : -.4f) + .3f * (2 * v->ph - 1));
        break;
    case 2: {
        float *l = ks[vi];
        int i = v->pos, j = i + 1;
        if (j >= v->len) j = 0;
        x = l[i] + v->nenv * rnd();
        v->nenv *= 0.985f;
        float f = 0.5f * (l[i] + l[j]);                    // lọc thấp: âm cao tắt nhanh hơn
        float ap = v->c * (f - v->s2) + v->s1;             // all-pass: cao độ chính xác
        v->s1 = f; v->s2 = ap;
        l[i] = (v->held ? v->gain : 0.97f) * ap;
        v->pos = (uint16_t)j;
        if (v->age > SR * 4) v->kind = 0;
        break; }
    case 3: {
        bool alive = false;
        if (v->tg > 0 && v->env > 0.001f) {
            v->fp *= v->fpd;
            v->ph += (v->f1 + v->fp) / SR; if (v->ph >= 1) v->ph -= 1;
            x += v->tg * v->env * (v->sq ? (v->ph < .5f ? 1.f : -1.f) : sinlut[(int)(v->ph * 256) & 255]);
            v->env *= v->edec; alive = true;
        }
        if (v->ng > 0 && v->nenv > 0.001f) {
            float n = rnd();
            if (v->hp) { float h = n - v->prev; v->prev = n; n = h * 0.5f; }
            x += v->ng * v->nenv * n;
            v->nenv *= v->ndec; alive = true;
        }
        if (!alive) v->kind = 0;
        break; }
    }
    return x;
}

// Cộng hưởng thùng đàn acoustic: ba đỉnh ~100 Hz (lỗ thùng), ~215 Hz (mặt đàn), ~520 Hz
typedef struct { float b0, b2, a1, a2, x1, x2, y1, y2; } bq_t;
static bq_t bqs[3];
static const float BF[3] = {100, 215, 520}, BQ[3] = {4, 5, 3}, BG[3] = {0.7f, 0.5f, 0.25f};

static inline float bq_run(bq_t *q, float x) {
    float y = q->b0 * x + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}

static uint32_t next_frame(void) {
    float s = 0, gs = 0;
    for (int i = 0; i < NV; i++) {
        uint8_t k = vox[i].kind;
        if (!k) continue;
        float x = sample_voice(&vox[i], i);
        if (k == 2) gs += x; else s += x;
    }
    s += gs;
    for (int b = 0; b < 3; b++) s += BG[b] * bq_run(&bqs[b], gs);
    s *= (ex.volume > 100 ? 100 : ex.volume) * 0.006f;
    if (s > 1) s = 1; else if (s < -1) s = -1;
    uint16_t u = (uint16_t)(int16_t)(s * 30000);
    return ((uint32_t)u << 16) | u;
}

void audio_init(void) {
    for (int i = 0; i < 256; i++) sinlut[i] = sinf(i * 6.2831853f / 256);
    for (int b = 0; b < 3; b++) {
        float w = 6.2831853f * BF[b] / SR, al = sinf(w) / (2 * BQ[b]), a0 = 1 + al;
        bqs[b].b0 = al / a0; bqs[b].b2 = -al / a0;
        bqs[b].a1 = -2 * cosf(w) / a0; bqs[b].a2 = (1 - al) / a0;
    }
    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint off = pio_add_program(pio, &audio_i2s_program);
    audio_i2s_program_init(pio, sm, off, PIN_DIN, PIN_BCLK, (float)clock_get_hz(clk_sys) / (SR * 64.0f));

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    channel_config_set_ring(&c, false, 12);          // đọc vòng quanh 4096 byte
#if PICO_RP2350
    uint32_t cnt = dma_encode_transfer_count_with_self_trigger(RING);
#else
    uint32_t cnt = 0xFFFFFFFFu;
#endif
    dma_channel_configure(dma_ch, &c, &pio->txf[sm], abuf, cnt, true);
    pio_sm_set_enabled(pio, sm, true);
}

void audio_task(void) {
    uint32_t rd = ((dma_hw->ch[dma_ch].read_addr - (uint32_t)abuf) >> 2) & (RING - 1);
    while (((wr - rd) & (RING - 1)) < AHEAD) {
        abuf[wr] = next_frame();
        wr = (wr + 1) & (RING - 1);
    }
}

void audio_click(void) { voice_start(201, 13, 1); }   // tiếng "tách" khi gõ phím

void audio_tone(uint16_t hz) {
    voice_t *v = &vox[alloc_voice()];
    memset(v, 0, sizeof *v);
    v->kind = 1; v->key = 200; v->held = true; v->inc = hz / (float)SR;
}
