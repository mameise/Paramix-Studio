#ifndef ASK_H
#define ASK_H
int hw_ask_reset(int en);   /* 1 = yes (A), 0 = no (B or wheel); waits for the keys to be released */
#ifdef OPT_MENU
u32 hw_opts_menu(u32 o, int en);
u32 opt_light_pct(u32 o);         /* 1.6.8 (test45): the light in percent from the flag bits */   /* 1.6.8: settings on the device; o, result: flag bits 16..22 */
#endif
#endif
