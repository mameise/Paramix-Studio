/* LCD controller of the colour Tamagotchi, based on tamaemu lcd.c. Tracks the
 * changed area so only that part is sent to the Paradise panel. */
#include "tc.h"

static void finish_cmd(Emu *e)
{
    Lcd *l = &e->lcd;
    switch (l->cmd) {
    case 0x2A: if (l->argc >= 4) { l->xs = (l->args[0] << 8) | l->args[1];
                                   l->xe = (l->args[2] << 8) | l->args[3];
                                   l->cx = l->xs; } break;
    case 0x2B: if (l->argc >= 4) { l->ys = (l->args[0] << 8) | l->args[1];
                                   l->ye = (l->args[2] << 8) | l->args[3];
                                   l->cy = l->ys; } break;
    default: break;
    }
}

#ifdef LCD_ROWS
/* v0.8.19: additionally the changed range per row (outside of Emu, so the save format stays the same).
 * lcd_rall: redraw the whole frame (commands like invert, sleep). */
uint8_t lcd_rx0[GRAM_H], lcd_rx1[GRAM_H], lcd_rall = 1;
#endif
static void mark(Lcd *l, int x, int y)
{
    if (x < l->dx0) l->dx0 = x;
    if (x > l->dx1) l->dx1 = x;
    if (y < l->dy0) l->dy0 = y;
    if (y > l->dy1) l->dy1 = y;
#ifdef LCD_ROWS
    if ((unsigned)y < GRAM_H) { if (x < lcd_rx0[y]) lcd_rx0[y] = (uint8_t)x; if (x > lcd_rx1[y]) lcd_rx1[y] = (uint8_t)x; }
#endif
}
static void mark_all(Lcd *l)
{
    mark(l, 0, 0); mark(l, 127, 127);
#ifdef LCD_ROWS
    lcd_rall = 1;
#endif
}

void lcd_cmd(Emu *e, uint8_t c)
{
    Lcd *l = &e->lcd;
#ifdef LCDCMD_DEBUG
    if(c==1||c==0x10||c==0x11||c==0x28||c==0x29) printf("LCDCMD %02x at %.3f\n", c, (double)e->cycles/e->dev.osc3_hz);
#endif
    finish_cmd(e);
    l->cmd = c; l->argc = 0; l->pix_n = 0;
    switch (c) {
    case 0x01: l->madctl = 0; l->sleep_out = false; l->disp_on = false; l->inverted = false;
               l->xs = 0; l->xe = 131; l->ys = 0; l->ye = 161; break;
    case 0x10: l->sleep_out = false; mark_all(l); break;
    case 0x11: l->sleep_out = true;  mark_all(l); break;
    case 0x20: l->inverted = false; mark_all(l); break;
    case 0x21: l->inverted = true;  mark_all(l); break;
    case 0x28: l->disp_on = false;  mark_all(l); break;
    case 0x29: l->disp_on = true;   mark_all(l); break;
    case 0x2C: l->cx = l->xs; l->cy = l->ys; break;
    default: break;
    }
}

void lcd_data(Emu *e, uint8_t d)
{
    Lcd *l = &e->lcd;
    if (l->cmd == 0x2C) {
        int bpp = ((l->colmod & 0x0F) == 0x05) ? 2 : 3;
        l->pix_buf[l->pix_n++] = d;
        if (l->pix_n >= bpp) {
            uint16_t p;
            if (bpp == 2) p = (uint16_t)((l->pix_buf[0] << 8) | l->pix_buf[1]);
            else {
                uint32_t r6 = l->pix_buf[0] >> 2, g6 = l->pix_buf[1] >> 2, b6 = l->pix_buf[2] >> 2;
                p = (uint16_t)(((r6 >> 1) << 11) | (g6 << 5) | (b6 >> 1));
            }
            l->pix_n = 0;
            int x = l->cx, y = l->cy;
            if (x >= 0 && x < GRAM_W && y >= 0 && y < GRAM_H) {
                if (l->gram[y][x] != p) { l->gram[y][x] = p; mark(l, x, y); }
            }
            if (++l->cx > l->xe) { l->cx = l->xs; if (++l->cy > l->ye) l->cy = l->ys; }
        }
        return;
    }
    if (l->argc < 16) l->args[l->argc] = d;
    l->argc++;
    if (l->cmd == 0x36 && l->argc == 1) { l->madctl = d; mark_all(l); }
    if (l->cmd == 0x3A && l->argc == 1) l->colmod = d;
}
