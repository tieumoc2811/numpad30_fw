// USB-MIDI: bàn phím gửi nốt khi ở lớp nhạc cụ; nốt từ máy tính gửi tới cũng phát ra loa.
#include <math.h>
#include "tusb.h"
#include "config.h"
#include "audio.h"
#include "midi.h"

static uint8_t sent[NROW * NCOL], sent_ch[NROW * NCOL];
// Trống theo General MIDI (kênh 10) cho id 1..12: kick, snare, hat đóng/mở, tom cao/giữa/thấp, crash, ride, clap, rim, cowbell
static const uint8_t GM[13] = {0, 36, 38, 42, 46, 50, 47, 45, 49, 51, 39, 37, 56};

bool midi_connected(void) { return ex.midi_on && tud_midi_mounted(); }

void midi_send(uint8_t idx, bool on) {
    if (idx >= NROW * NCOL || !midi_connected()) {
        if (!on && idx < NROW * NCOL) sent[idx] = 0;
        return;
    }
    uint8_t pkt[3];
    if (on) {
        uint16_t v = cfg_note(idx);
        if (!v) return;
        uint8_t ch = ex.midi_ch & 15, n;
        if (cfg_board() == 1) { n = v <= 12 ? GM[v] : 0; ch = 9; }
        else {
            int m = (int)lroundf(69.0f + 12.0f * log2f(v / 440.0f)) + ex.midi_tr;
            n = (uint8_t)(m < 0 ? 0 : m > 127 ? 127 : m);
        }
        if (!n) return;
        sent[idx] = n; sent_ch[idx] = ch;
        pkt[0] = 0x90 | ch; pkt[1] = n; pkt[2] = ex.midi_vel ? ex.midi_vel : 100;
        tud_midi_stream_write(0, pkt, 3);
    } else if (sent[idx]) {
        pkt[0] = 0x80 | sent_ch[idx]; pkt[1] = sent[idx]; pkt[2] = 0;
        tud_midi_stream_write(0, pkt, 3);
        sent[idx] = 0;
    }
}

void midi_task(void) {
    uint8_t p[4];
    while (tud_midi_available()) {
        if (!tud_midi_packet_read(p)) break;
        uint8_t st = p[1] & 0xF0, ch = p[1] & 15, note = p[2], vel = p[3];
        if (ch == 9) continue;                                   // bỏ kênh trống từ máy tính
        uint8_t key = (uint8_t)(128 + note % 72);                 // 128..199, không đụng phím thật
        if (st == 0x90 && vel > 0) audio_play_hz(midi_to_hz(note), key);
        else if (st == 0x80 || (st == 0x90 && vel == 0)) audio_release(key);
    }
}
