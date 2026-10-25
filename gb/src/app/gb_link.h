/* GBEmu link (GB_LINK, 1.6.11 test2): the Game Boy's serial port between two Paradise, as with a link cable.
 * The side that clocks (SC = 0x81) sends its byte ('M') and waits; the other side answers with its own byte ('R') as
 * soon as its game is ready to receive (SC = 0x80); until then it keeps the byte. Both clocking at once: each answers
 * 'B' (busy) and both get 0xFF, as with two clocks on a real cable. Without a partner (no hello for 2 s) everything
 * is as before: a clocking side gets 0xFF, a receiving side waits.
 * The transport and the clock come from the includer:
 *   GL_SEND(kind, byte)   send one frame to the other side
 *   GL_POLL()             take in what arrived (calls gl_got)
 *   GL_NOW()              milliseconds
 *   GL_PARTNER()          1 when the other side is there
 * Header only, one link. Author: mameise */
#ifndef GB_LINK_H
#define GB_LINK_H

#define GL_WAIT_MAX 400u     /* ms the clocking side waits for an answer, then 0xFF */
#define GL_PEND_MAX 600u     /* ms a received byte waits for the game to get ready, then 'B' */

typedef struct GlState {
  uint8_t waiting, got, reply;   /* clocking side: waiting for the answer, it came, its byte */
  uint8_t pend, pend_byte;       /* receiving side: a byte of the other side waits */
  uint32_t t0, pend_t;
  uint32_t n_tx, n_rx;           /* bytes (for the tests) */
} GlState;
#ifndef GL_S                     /* the PC test runs two: it points GL_S at the one running */
static GlState g_gl_one;
#define GL_S g_gl_one
#endif

/* a frame from the other side */
static void gl_got(uint8_t kind, uint8_t b)
{
  if (kind == 'M') {
    if (GL_S.waiting) { GL_SEND('B', 0xFF); return; }          /* both clocking */
    GL_S.pend = 1; GL_S.pend_byte = b; GL_S.pend_t = GL_NOW();
  } else if (kind == 'R' || kind == 'B') {
    if (GL_S.waiting) { GL_S.got = 1; GL_S.reply = kind == 'R' ? b : 0xFF; }
  }
}

/* Peanut-GB: a transfer starts */
static void gl_tx(struct gb_s *gb, const uint8_t tx)
{
  if (!GL_PARTNER()) return;
  if (!(gb->hram_io[IO_SC] & 1u)) {                            /* receiving side gets ready */
    GL_S.waiting = 0;                                          /* (a clocking try of this side is over) */
    GL_POLL();
    if (GL_S.pend) {                                           /* the other side clocked already: done at once */
      GL_SEND('R', tx); GL_S.pend = 0; GL_S.n_rx++;
      gb->hram_io[IO_SB] = GL_S.pend_byte;
      gb->hram_io[IO_SC] &= 0x01;
      gb->hram_io[IO_IF] |= SERIAL_INTR;
    }
    return;
  }
  if (GL_S.pend) { GL_SEND('B', 0xFF); GL_S.pend = 0; }        /* the other one clocked too */
  GL_S.waiting = 1; GL_S.got = 0; GL_S.t0 = GL_NOW();
  GL_SEND('M', tx); GL_S.n_tx++;
}
/* Peanut-GB: the transfer's 8 bits are through (called again and again while it returns GB_SERIAL_RX_WAIT) */
static enum gb_serial_rx_ret_e gl_rx(struct gb_s *gb, uint8_t *rx)
{
  GL_POLL();
  if (gb->hram_io[IO_SC] & 1u) {                               /* clocking side */
    if (!GL_S.waiting) return GB_SERIAL_RX_NO_CONNECTION;      /* no partner: 0xFF */
    if (GL_S.got) { GL_S.waiting = 0; *rx = GL_S.reply; GL_S.n_rx++; return GB_SERIAL_RX_SUCCESS; }
    if (GL_NOW() - GL_S.t0 > GL_WAIT_MAX) { GL_S.waiting = 0; *rx = 0xFF; return GB_SERIAL_RX_SUCCESS; }
    return GB_SERIAL_RX_WAIT;
  }
  /* receiving side: done when the other side's byte is there */
  if (GL_S.pend) {
    GL_SEND('R', gb->hram_io[IO_SB]); GL_S.pend = 0;
    *rx = GL_S.pend_byte; GL_S.n_rx++;
    return GB_SERIAL_RX_SUCCESS;
  }
  return GB_SERIAL_RX_NO_CONNECTION;                           /* keeps waiting, SB stays */
}
/* from the main loop: a byte nobody took for long is refused (the other side goes on with 0xFF) */
static void gl_tick(void)
{
  GL_POLL();
  if (GL_S.pend && GL_NOW() - GL_S.pend_t > GL_PEND_MAX) { GL_SEND('B', 0xFF); GL_S.pend = 0; }
}
#endif
