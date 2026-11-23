#ifndef MIDI_H_
#define MIDI_H_
#include <stdint.h>
#include <stdbool.h>
void midi_send(uint8_t idx, bool on);   // phím nhạc idx nhấn/nhả -> USB-MIDI
bool midi_connected(void);              // MIDI bật và máy tính đã nhận thiết bị
void midi_task(void);                   // đọc nốt MIDI từ máy tính, phát ra loa
#endif
