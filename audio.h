#ifndef AUDIO_H_
#define AUDIO_H_
void audio_init(void);
void audio_task(void);
uint16_t midi_to_hz(int midi);
void audio_play_hz(uint16_t hz, uint8_t key);   // giọng đơn theo bảng đang dùng (guitar gảy, còn lại piano)
void audio_release(uint8_t key);
void audio_lead(uint16_t hz, uint8_t key);      // giọng piano đơn, giữ tới khi audio_release
void audio_tone(uint16_t hz);   // giọng đơn dùng cho tiếng báo (key 200, nhả bằng music_note(200,false))   // gọi liên tục trong vòng lặp chính
void audio_click(void);   // tiếng "tách" ngắn khi gõ phím
#endif
