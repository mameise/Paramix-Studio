/*
 * t7_sound.c - Originalton der V7 (Timer + DAC)
 *
 * Aus dem ROM rekonstruiert (Einzelheiten in docs/TON_FINDINGS.md):
 *   - Timer 0: Vorladewert $3032/$3033, zaehlt aufwaerts bis $FFFF, dann IRQ 0
 *     und neu laden. Takt 4 MHz, wenn das obere Nibble von $3031 = 2 ist.
 *     IRQ 0 ist der Ausgabetakt: Rechteck (Ton) oder Samples (5000 Hz).
 *   - Timer 1: Vorladewert $3034/$3035, unteres Nibble von $3031 = 2.
 *     Takt 32768 Hz, mit $3030 Bit 0 = 1 das Sechzehnfache (524288 Hz, ab v1.9).
 *     Ueberlauf -> IRQ 12 = Takt des Melodie-Sequenzers ($D230), im
 *     Normalbetrieb ($FC00) also 256 Hz.
 *   - $3060 = 8-Bit-DAC, vorzeichenbehaftet (0 = Ruhe). Der ROM rampt den
 *     Wert bei $F605 selbst auf 0, damit es nicht knackt.
 *   - $3065 Bit 1 = Verstaerker an.
 *
 * Belege: Die Notentabelle $F851/$F89A beginnt bei 65 Hz (C2), die
 * Sample-Rate aus $F84F ergibt bei 4 MHz genau 5000 Hz, der Schlaftest $C78E
 * stellt Timer 1 bei $3030=0 auf $8000 (bei 32768 Hz genau 1 s) und eine
 * Melodie im ROM laeuft damit exakt mit 120 BPM.
 *
 * Aus dem DAC-Verlauf entsteht direkt ein fertiger 16-Bit-Tonstrom
 * (Kastenfilter pro Ausgabe-Sample, sanfter Tiefpass, Gleichanteil-Filter),
 * den die Plattform (Android AudioTrack / ESP32 I2S) nur noch abholt.
 *
 * Alles haengt am Schalter g_snd_on. Ist er aus, laufen weder Timer noch
 * IRQ 0/12 - exakt das alte Verhalten.
 *
 * Aufwand: pro Instruktion nur ein Addieren und ein Vergleich. Timer und
 * Tonstrom werden ereignisgesteuert nachgezogen.
 */
#include "t7_internal.h"
#include "t7_sound.h"

uint8_t  g_snd_on = 0;
int32_t  g_snd_next = 0x7FFFFFFF;        // Zyklen bis zum naechsten Timer-Ereignis (zaehlt pro Instruktion runter)
uint32_t g_snd_irq12 = 0, g_snd_irq0 = 0;

// Zeitbasis: jetzt = s_base + (s_span - g_snd_next). So kostet die heisse
// Schleife pro Instruktion nur ein Subtrahieren und einen Vergleich.
static uint64_t s_base = 0;              // CPU-Zyklen bei der letzten Planung
static int32_t  s_span = 0x7FFFFFFF;     // damals geplanter Abstand

uint32_t t7_snd_tone_hz(uint8_t *level);
static inline uint64_t snd_now(void) { return s_base + (uint64_t)(int64_t)(s_span - g_snd_next); }

// Takt Timer 0. Die Notentabelle ergibt bei 4 MHz ein C2 (65 Hz) als tiefsten
// Ton, bei 8 MHz ein C3. Am Geraet klang 4 MHz eine Oktave zu tief (v1.7,
// Bernds Hoertest), daher 8 MHz. Die Sample-Rate aus $F84F ist dann 10 kHz.
#ifndef T7_T0_HZ
#define T7_T0_HZ 8000000u
#endif
#define T0_HZ T7_T0_HZ

// ---- Ton-Ereignisse fuer Piezo-Geraete (ESP32) ----
// Alle s_smp_per Zyklen wird der gerade gespielte Rechteckton abgefragt; aendert
// er sich, landet (Zeitpunkt in CPU-Zyklen, Frequenz) in einem kleinen Ring.
// Die Plattform spielt die Ereignisse zeitversetzt in Echtzeit ab.
#define TEV_N 64u
static uint32_t s_smp_per = 0;           // 0 = aus
static uint64_t s_smp_due = 0;
static uint32_t s_tev_last = 0;
static uint32_t s_tev_cand = 0;          // neuer Wert, muss TEV_STABLE Abfragen lang stehen (keine Zuckungen)
#define TEV_STABLE 4u                    // ein neuer Ton muss 4 ms stehen (Zucken beim Notenwechsel weg)
#define TEV_STABLE_OFF 2u                // Stille nur 2 ms (die kurze Pause zwischen gleichen Noten bleibt)
static uint8_t  s_tev_seen = 0;
static uint8_t  s_ev_on = 0;             // Ereignisse fuer den Piezo (ESP32) gewuenscht

// ---- Klarer Ton (Android): Rechteck aus Tonhoehe + Huellkurve neu erzeugen ----
// Die Firmware kippt den DAC per Software im IRQ 0. Andere Interrupts (Sequenzer
// 256 Hz, Bild) verschieben diese Flanken um bis zu ~100 Zyklen. Aus dem DAC
// abgetastet klingt das rauschig mit leisem Knacksen darunter. Im Klar-Modus
// wird deshalb nur gelesen, WAS die Firmware spielt (Tonhoehe aus Timer 0,
// Lautstaerke aus der Huellkurve $93), und daraus ein sauberes, bandbegrenztes
// Rechteck erzeugt.
#ifndef T7_SND_SYN_N
#define T7_SND_SYN_N 512u            // Zustaende im 1-ms-Raster (512 ms Vorlauf); ESP32 braucht das nicht
#endif
#define SYN_N T7_SND_SYN_N
typedef struct { uint64_t c; uint32_t hz; uint8_t lv; } syn_t;
static syn_t    s_syn[SYN_N];
static uint32_t s_syn_w = 0, s_syn_r = 0;
static uint8_t  s_syn_on = 0;
static float    s_ph = 0.0f;             // Phase 0..1
static float    s_amp = 0.0f;            // geglaettete Lautstaerke
static uint32_t s_ohz = 0;               // zuletzt gespielte Tonhoehe (fuers Ausklingen)
static uint32_t s_rate = 48000;
static uint64_t s_tev_c[TEV_N];
static uint32_t s_tev_hz[TEV_N];
static volatile uint32_t s_tev_w = 0, s_tev_r = 0;
// Timer 1 bei $3030 Bit 0 = 1. v1.7/v1.8: 262144 (Sequenzer 256 Hz), am Geraet
// zu langsam (Bernd, Android und ESP32). Ab v1.9 524288 -> Sequenzer 512 Hz.
#ifndef T7_T1_FAST_HZ
#define T7_T1_FAST_HZ 524288u
#endif
static uint32_t s_t1_fast = T7_T1_FAST_HZ;
static uint32_t s_emu_hz  = 4000000u;    // CPU-Zyklen pro Echtzeit-Sekunde

static uint8_t  s_run[2];
static uint64_t s_per[2];                // Periode in CPU-Zyklen (16.16)
static uint64_t s_due[2];                // naechster Ueberlauf, absolut (16.16)

// ---- Tonstrom ----
static uint8_t  s_cap = 0;
static uint64_t s_cps_fp = 0;            // CPU-Zyklen pro Ausgabe-Sample (16.16)
static uint64_t s_rend = 0;              // bis hierhin gerendert (16.16)
static uint64_t s_pos = 0;               // Anteil im laufenden Sample (16.16)
static int64_t  s_acc = 0;
static int32_t  s_dac = 0;               // aktueller DAC-Wert (-128..127), 0 wenn Verstaerker aus
static int32_t  s_vol = 60;              // 0..100
static int32_t  s_hp_x = 0, s_hp_y = 0;  // Gleichanteil-Filter
static int32_t  s_lp = 0;                // sanfter Tiefpass (kleiner Lautsprecher)

#ifndef T7_SND_RING
#define T7_SND_RING 65536u            // Zweierpotenz, 1,36 s bei 48 kHz
#endif
#define RING T7_SND_RING
static int16_t  s_ring[RING];
static volatile uint32_t s_w = 0, s_r = 0;
static uint32_t s_rfrac = 0;             // Nachkomma-Leseposition beim Abholen (16.16)
static int32_t  s_last = 0;              // zuletzt ausgegebener Wert
static uint32_t s_under = 0;             // Zaehler Unterdeckungen (Diagnose)

static uint32_t timer_hz(int t)
{
  if (t == 0) return T0_HZ;
  return (g_cpureg[0x30] & 0x01) ? s_t1_fast : 32768u;
}

static void reschedule(void)
{
  uint64_t cnow = snd_now();
  uint64_t now = cnow << 16;
  uint64_t best = (uint64_t)0x7FFFFFFF << 16;
  for (int t = 0; t < 2; t++) {
    if (!s_run[t]) continue;
    uint64_t d = s_due[t] > now ? s_due[t] - now : 0;
    if (d < best) best = d;
  }
  if (s_smp_per) {
    uint64_t sd = s_smp_due << 16;
    uint64_t d = sd > now ? sd - now : 0;
    if (d < best) best = d;
  }
  int64_t c = (int64_t)((best + 0xFFFF) >> 16);
  if (c < 1) c = 1;
  if (c > 0x3FFFFFFF) c = 0x3FFFFFFF;
  s_base = cnow;
  s_span = (int32_t)c;
  g_snd_next = (int32_t)c;
}

static void recalc_timer(int t, int restart)
{
  uint8_t ctl = g_cpureg[0x31];
  uint8_t src = t == 0 ? (ctl >> 4) : (ctl & 0x0F);
  uint32_t pre = t == 0 ? (g_cpureg[0x32] | (g_cpureg[0x33] << 8))
                        : (g_cpureg[0x34] | (g_cpureg[0x35] << 8));
  uint32_t n = 0x10000u - pre;
  if (n < 16) n = 16;                    // Schutz: kein IRQ-Sturm
  uint8_t run = (src == 2);
  s_per[t] = (((uint64_t)n * s_emu_hz) << 16) / timer_hz(t);
  if (run && (!s_run[t] || restart)) s_due[t] = (snd_now() << 16) + s_per[t];
  s_run[t] = run;
}

static inline void ring_put(int32_t v)
{
  s_lp += (v - s_lp) >> 1;               // ca. 5 kHz bei 48 kHz Abtastrate
  v = s_lp;
  int32_t y = v - s_hp_x + ((s_hp_y * 254) >> 8);   // Koppelkondensator
  s_hp_x = v; s_hp_y = y;
  if (y > 32767) y = 32767;
  if (y < -32768) y = -32768;
  if (s_w - s_r >= RING - 1) return;     // voll -> Neues verwerfen (nur der Leser bewegt s_r)
  s_ring[s_w & (RING - 1)] = (int16_t)y;
  s_w++;
}

// Tonstrom bis "jetzt" rendern (der DAC-Wert war seit s_rend konstant).
static void render_now(void)
{
  if (s_syn_on) return;                  // Klar-Modus rendert aus den Zustaenden
  uint64_t now = snd_now() << 16;
  if (!s_cap || !s_cps_fp) { s_rend = now; return; }
  if (now <= s_rend) return;
  uint64_t span = now - s_rend;
  s_rend = now;
  if (span > s_cps_fp * 48000u) {        // riesige Luecke (Aufholen) -> nicht nachrendern
    s_pos = 0; s_acc = 0; return;
  }
  while (s_pos + span >= s_cps_fp) {
    uint64_t take = s_cps_fp - s_pos;
    s_acc += (int64_t)s_dac * (int64_t)take;
    int32_t avg = (int32_t)(s_acc / (int64_t)(s_cps_fp >> 8));   // DAC * 256
    ring_put(avg * s_vol / 100);
    span -= take; s_pos = 0; s_acc = 0;
  }
  s_acc += (int64_t)s_dac * (int64_t)span;
  s_pos += span;
}

void t7_snd_io_write(uint8_t reg, uint8_t value)
{
  (void)value;
  switch (reg) {
    case 0x30: recalc_timer(1, 1); break;
    case 0x31: recalc_timer(0, 0); recalc_timer(1, 0); break;
    case 0x32: recalc_timer(0, 0); break;
    case 0x33: recalc_timer(0, 1); break;
    case 0x34: recalc_timer(1, 0); break;
    case 0x35: recalc_timer(1, 1); break;
    case 0x60: case 0x65:
      render_now();
      s_dac = (g_cpureg[0x65] & 0x02) ? (int32_t)(int8_t)g_cpureg[0x60] : 0;
      return;
    default: return;
  }
  reschedule();
}

// Wird aufgerufen, wenn g_snd_next abgelaufen ist.
void T7_HOT t7_snd_service(void)
{
  uint64_t now = snd_now() << 16;
  if (s_run[0]) while (s_due[0] <= now) { s_due[0] += s_per[0]; t7_fire_irq(0);  g_snd_irq0++; }
  if (s_run[1]) while (s_due[1] <= now) { s_due[1] += s_per[1]; t7_fire_irq(12); g_snd_irq12++; }
  if (s_smp_per) {
    uint64_t c = now >> 16;
    if (c >= s_smp_due) {
      s_smp_due += s_smp_per;
      if (s_smp_due <= c) s_smp_due = c + s_smp_per;       // weit zurueck -> neu aufsetzen
      uint8_t lv;
      uint32_t hz = t7_snd_tone_hz(&lv);
      if (lv < 4) hz = 0;                                   // Huellkurve fast aus = still
      if (hz != s_tev_cand) { s_tev_cand = hz; s_tev_seen = 1; }
      else if (s_tev_seen < TEV_STABLE) s_tev_seen++;
      uint32_t need = hz ? TEV_STABLE : TEV_STABLE_OFF;
      int changed = (s_tev_seen >= need && hz != s_tev_last);
      if (changed) {
        s_tev_last = hz;
        if (s_ev_on && (s_tev_w - s_tev_r) < TEV_N) {
          s_tev_c[s_tev_w % TEV_N] = c - (uint64_t)s_smp_per * (need - 1);   // Beginn lag frueher
          s_tev_hz[s_tev_w % TEV_N] = hz;
          s_tev_w++;
        }
      }
      if (s_syn_on && s_cap) {
        if (s_syn_w - s_syn_r >= SYN_N) s_syn_r = s_syn_w - SYN_N + 1;   // Renderer haengt -> Aeltestes weg
        syn_t *e = &s_syn[s_syn_w % SYN_N];
        e->c = c; e->hz = s_tev_last; e->lv = lv;
        s_syn_w++;
        if (changed) {                                      // bestaetigte Aenderung rueckdatieren
          for (uint32_t k = 1; k < need; k++) {
            uint32_t idx = s_syn_w - 1 - k;
            if ((int32_t)(idx - s_syn_r) < 0) break;        // schon abgespielt
            s_syn[idx % SYN_N].hz = s_tev_last;
          }
        }
      }
    }
  }
  reschedule();
}

#ifndef T7_NO_SYNTH
static inline float blep(float t, float dt)
{
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

// Klar-Modus: Samples bis (jetzt - Vorlauf) aus den 1-ms-Zustaenden erzeugen.
static void render_synth(void)
{
  if (!s_cap || !s_cps_fp || !s_smp_per) return;
  uint64_t lag = (uint64_t)s_smp_per * (TEV_STABLE + 1);
  uint64_t nowc = snd_now();
  if (nowc <= lag) return;
  uint64_t target = (nowc - lag) << 16;
  if (target <= s_rend) return;
  if (target - s_rend > s_cps_fp * 48000u) {             // riesige Luecke -> nicht nachrendern
    s_rend = target;
    while (s_syn_w - s_syn_r > 1 && (s_syn[(s_syn_r + 1) % SYN_N].c << 16) <= s_rend) s_syn_r++;
    return;
  }
  const float kAmp = 0.03f;                               // ~0,7 ms Glaettung (kein Klick, kein Reissverschluss)
  while (s_rend + s_cps_fp <= target) {
    s_rend += s_cps_fp;
    while (s_syn_w - s_syn_r > 1 && (s_syn[(s_syn_r + 1) % SYN_N].c << 16) <= s_rend) s_syn_r++;
    uint32_t hz = 0; uint8_t lv = 0;
    if (s_syn_w != s_syn_r) { syn_t *e = &s_syn[s_syn_r % SYN_N]; hz = e->hz; lv = e->lv; }
    float want = hz ? (float)lv * 256.0f : 0.0f;
    if (hz) {
      if (s_ohz == 0 && s_amp < 1.0f) s_ph = 0.0f;       // neuer Ton aus der Stille: sauber bei Phase 0
      s_ohz = hz;
    }
    s_amp += (want - s_amp) * kAmp;
    int32_t out = 0;
    if (s_ohz && s_amp > 0.5f) {
      float dt = (float)s_ohz / (float)s_rate;
      if (dt > 0.45f) dt = 0.45f;
      float t2 = s_ph + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
      float v = (s_ph < 0.5f ? 1.0f : -1.0f) + blep(s_ph, dt) - blep(t2, dt);
      out = (int32_t)(v * s_amp);
      s_ph += dt; if (s_ph >= 1.0f) s_ph -= 1.0f;
    } else if (!hz) {
      s_ohz = 0;
    }
    out = out * s_vol / 100;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    if (s_w - s_r >= RING - 1) continue;
    s_ring[s_w & (RING - 1)] = (int16_t)out;
    s_w++;
  }
}

#endif

void t7_snd_flush(void)
{
  if (!g_snd_on) return;
#ifndef T7_NO_SYNTH
  if (s_syn_on) render_synth(); else
#endif
  render_now();
}

static void smp_update(void)
{
  uint32_t per = s_emu_hz / 1000u;                       // 1-ms-Raster
  uint32_t want = (s_ev_on || s_syn_on) ? per : 0;
  if (want != s_smp_per) {
    s_smp_per = want;
    s_smp_due = snd_now() + want;
  }
}

// 1 = Klar-Modus (Rechteck neu erzeugt), 0 = DAC abtasten (roh, wie v1.7)
void t7_snd_synth(int on)
{
  s_syn_on = on ? 1 : 0;
  s_syn_r = s_syn_w;
  s_rend = snd_now() << 16;
#ifndef T7_NO_SYNTH
  s_amp = 0.0f; s_ohz = 0; s_ph = 0.0f;
#endif
  smp_update();
  reschedule();
}

void t7_snd_set_mode(int on)
{
  g_snd_on = on ? 1 : 0;
  s_run[0] = s_run[1] = 0;
  s_dac = 0;
  s_rend = snd_now() << 16;
  if (g_snd_on) { recalc_timer(0, 1); recalc_timer(1, 1); }
  reschedule();
}

void t7_snd_config(uint32_t emu_hz, uint32_t sample_rate)
{
  if (emu_hz < 100000) emu_hz = 100000;
  if (sample_rate < 8000) sample_rate = 8000;
  s_emu_hz = emu_hz;
  s_cps_fp = ((uint64_t)emu_hz << 16) / sample_rate;
  s_rate = sample_rate;
  s_pos = 0; s_acc = 0; s_rend = snd_now() << 16;
  s_smp_per = 0; smp_update();
  if (g_snd_on) { recalc_timer(0, 1); recalc_timer(1, 1); }
  reschedule();
}

void t7_snd_capture(int on)
{
  if (on && !s_cap) {
    s_rend = snd_now() << 16; s_pos = 0; s_acc = 0;
    s_syn_r = s_syn_w;
#ifndef T7_NO_SYNTH
    s_amp = 0.0f; s_ohz = 0;
#endif
  }
  s_cap = on ? 1 : 0;
}
void t7_snd_volume(int pct) { if (pct < 0) pct = 0; if (pct > 100) pct = 100; s_vol = pct; }
void t7_snd_reset(void)     { s_r = s_w; s_hp_x = s_hp_y = 0; s_lp = 0; s_rfrac = 0; s_last = 0; }
uint32_t t7_snd_avail(void) { return s_w - s_r; }

/*
 * Tonstrom abholen, geregelt (wie bei TamaParadise, dort 0 Knackstellen):
 * Der Emulator liefert in Schueben (66 ms), die Ausgabe holt in kleinen
 * Bloecken. Statt bei leerem Vorrat mit Stille aufzufuellen (eine Stufe mitten
 * in der Rechteckflanke = Knacks), wird die Lesegeschwindigkeit um ein paar
 * Prozent gedehnt oder gestaucht, bis der Vorrat wieder bei `target` liegt.
 * Reicht es trotzdem nicht, klingt der letzte Wert weich aus, statt auf Null
 * zu springen. target = 0: ungeregelt (Stille auffuellen).
 * Rueckgabe: Anzahl echt gelesener Ausgabe-Samples.
 */

uint32_t t7_snd_pull(int16_t *out, uint32_t n, uint32_t target)
{
  uint32_t have = s_w - s_r;
  uint32_t step = 65536;
  if (target) {
    // Zu viel Vorrat kommt vor allem von Widget-Tipps (0,7 s Ton auf einmal); danach
    // pausiert die Emulation genauso lange, der Vorrat baut sich also selbst ab.
    // Deshalb nur ganz sanft schneller (1,02) und erst ab 1 s hart schneiden.
    uint32_t hard = RING * 3 / 4;
    if (hard > 48000u) hard = 48000u;
    if (have > hard) { s_r = s_w - target; have = target; s_rfrac = 0; }
    if      (have < target / 2)     step = 60293;   // 0,92
    else if (have < target * 3 / 4) step = 62915;   // 0,96
    else if (have > target * 3)     step = 66847;   // 1,02
  }
  uint32_t real = 0;
  int under = 0;
  for (uint32_t i = 0; i < n; i++) {
    have = s_w - s_r;
    if (have >= 2) {
      int32_t a = s_ring[s_r & (RING - 1)];
      int32_t b = s_ring[(s_r + 1) & (RING - 1)];
      int32_t v = a + (int32_t)(((int64_t)(b - a) * s_rfrac) >> 16);
      out[i] = (int16_t)v; s_last = v;
      s_rfrac += step;
      s_r += s_rfrac >> 16;
      s_rfrac &= 0xFFFF;
      real++;
    } else {
      s_last = (s_last * 255) / 256;               // weich ausklingen (~5 ms)
      out[i] = (int16_t)s_last;
      under = 1;
    }
  }
  if (under) s_under++;
  return real;
}
uint32_t t7_snd_underruns(void) { return s_under; }

// Nach dem Laden eines Spielstands: Timerzustand aus den Registern ableiten.
void t7_snd_resync(void)
{
  s_run[0] = s_run[1] = 0;
  s_dac = 0;
  if (g_snd_on) { recalc_timer(0, 1); recalc_timer(1, 1); }
  reschedule();
}

// 1 = gerade Tonausgabe aktiv (Timer 0 laeuft, IRQ 0 frei, Verstaerker an)
int t7_snd_active(void)
{
  return g_snd_on && s_run[0] && (g_cpureg[0x70] & 0x80) && (g_cpureg[0x65] & 0x02);
}

// Nur fuer Versuche: schneller Takt von Timer 1 (Standard T7_T1_FAST_HZ).
void t7_snd_set_t1_hz(uint32_t hz)
{
  if (hz < 1000) hz = 1000;
  s_t1_fast = hz;
  if (g_snd_on) { recalc_timer(1, 1); reschedule(); }
}

/*
 * Fuer Geraete ohne DAC (ESP32 mit Piezo): welcher Rechteckton spielt die
 * Firmware gerade? Nachgebildet aus dem IRQ-0-Handler $D1B9: das Rechteck
 * wird nur ausgegeben, wenn $92 Bit 0 gesetzt ist und entweder $B3 Bit 0
 * oder keines der Bits $94 in $97 (Sample-/Rampenbetrieb). Die Tonhoehe ist
 * der halbe IRQ-0-Takt, die Lautstaerke steckt in $93 (Huellkurve).
 * Rueckgabe 0 = still. *level = Betrag der Huellkurve (0..127).
 */
uint32_t t7_snd_tone_hz(uint8_t *level)
{
  if (level) *level = 0;
  if (!g_snd_on || !s_run[0]) return 0;
  if (!(g_cpureg[0x70] & 0x80) || !(g_cpureg[0x65] & 0x02)) return 0;
  if (!(g_wram[0x92] & 0x01)) return 0;
  if (!(g_wram[0xB3] & 0x01) && (g_wram[0x97] & 0x94)) return 0;
  int a = (int8_t)g_wram[0x93];
  if (a == -128) a = 0;
  if (a < 0) a = -a;
  if (level) *level = (uint8_t)a;
  if (a == 0) return 0;
  uint32_t cnt = 0x10000u - (g_cpureg[0x32] | (g_cpureg[0x33] << 8));
  if (cnt < 16) return 0;
  return (T0_HZ / 2u + cnt / 2u) / cnt;
}

// ---- Ton-Ereignisse (Piezo) ----
void t7_snd_tone_events(uint32_t interval_cycles)
{
  s_ev_on = interval_cycles ? 1 : 0;
  s_smp_per = 0; smp_update();
  if (s_ev_on && interval_cycles != s_smp_per) { s_smp_per = interval_cycles; s_smp_due = snd_now() + interval_cycles; }
  s_tev_last = 0; s_tev_cand = 0; s_tev_seen = 0;
  reschedule();
}
int t7_snd_tone_peek(uint64_t *cyc, uint32_t *hz)
{
  if (s_tev_r == s_tev_w) return 0;
  if (cyc) *cyc = s_tev_c[s_tev_r % TEV_N];
  if (hz)  *hz  = s_tev_hz[s_tev_r % TEV_N];
  return 1;
}
void t7_snd_tone_pop(void) { if (s_tev_r != s_tev_w) s_tev_r++; }
uint64_t t7_snd_cycles(void) { return snd_now(); }

// 1 = die Firmware spielt gerade eine Melodie oder einen Ton (nicht schlafen legen)
int t7_snd_busy(void)
{
  if (!g_snd_on) return 0;
  if (g_wram[0xA3] & 0x80) return 1;                      // Sequenzer laeuft ($FC4C setzt $A3=$80)
  return s_tev_last != 0 || s_tev_r != s_tev_w;
}
