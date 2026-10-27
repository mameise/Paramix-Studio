#ifndef ASK_H
#define ASK_H
int hw_ask_reset(int en);   /* 1 = yes (A), 0 = no (B or wheel); waits for the keys to be released */
#ifdef ASK_NUM
int hw_num_edit(const char *title, u32 *v, int digits, u32 max, int en);   /* test30: 1 = kept */
#endif
#ifdef OPT_MENU
u32 hw_opts_menu(u32 o, int en);
u32 hw_opts_menu_xn(u32 o, int en, const char *const *ex, int nex);   /* test30: chosen extra: bit 31, index in 28-30 */
u32 opt_light_pct(u32 o);
u32 opt_dim_pct(u32 o);   /* ASK_DIM: the dim level, at most the light */
u32 hw_opts_menu_x(u32 o, int en, const char *extra);   /* ASK_EXTRA (T18Emu): one more line, bit 31 = chosen */
int hw_list_menu(const char *title, const char *const *it, int n);
int hw_pic_ask(const volatile u8 *p, int en);     /* test47: a 64x64 picture, B ok (1), C back (0) */         /* 1.6.8 (test45): the light in percent from the flag bits */   /* 1.6.8: settings on the device; o, result: flag bits 16..22 */
#endif
#endif
