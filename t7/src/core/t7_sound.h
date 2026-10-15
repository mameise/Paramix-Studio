/* t7_sound.h - Originalton (Timer 0/1 + DAC), siehe t7_sound.c */
#ifndef T7_SOUND_H
#define T7_SOUND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t  g_snd_on;
extern int32_t  g_snd_next;
void     t7_snd_io_write(uint8_t reg, uint8_t value);
void     t7_snd_service(void);
void     t7_snd_flush(void);
void     t7_snd_set_mode(int on);          // 1 = Originalton (Timer + DAC), 0 = alt
void     t7_snd_config(uint32_t emu_hz, uint32_t sample_rate);
void     t7_snd_capture(int on);           // Tonstrom aufzeichnen
void     t7_snd_volume(int pct);           // 0..100
void     t7_snd_reset(void);               // Ring leeren
uint32_t t7_snd_avail(void);
uint32_t t7_snd_pull(int16_t *out, uint32_t n, uint32_t target);   // geregelt, target in Samples (0 = ungeregelt)
uint32_t t7_snd_underruns(void);
void     t7_snd_synth(int on);             // 1 = klarer Ton (Rechteck neu erzeugt), 0 = DAC roh
void     t7_snd_resync(void);
int      t7_snd_active(void);
uint32_t t7_snd_tone_hz(uint8_t *level);    // Rechteckton gerade (fuer Piezo), 0 = still
void     t7_snd_set_t1_hz(uint32_t hz);
// Piezo-Geraete: Ton-Ereignisse (Zeitpunkt in CPU-Zyklen, Frequenz in Hz, 0 = still)
void     t7_snd_tone_events(uint32_t interval_cycles);   // 0 = aus
int      t7_snd_tone_peek(uint64_t *cyc, uint32_t *hz);
void     t7_snd_tone_pop(void);
uint64_t t7_snd_cycles(void);
int      t7_snd_busy(void);
#ifdef __cplusplus
}
#endif
#endif
