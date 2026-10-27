/* exitkey.h - test24 (switch EXIT_KEY): how to go back to the Paradise or the menu, chosen in the Studio (tab Menu) for
 * every program. The Studio writes "EX" << 16 | mode into the last word of the program's vector table (0x13C, IRQ 63,
 * never used); without it the program's own way stays. Modes: 0 wheel held (as before), 1 wheel and A held, 2 wheel
 * and C held, 3 wheel held 5 s. Header only. Author: mameise */
#ifndef EXITKEY_H
#define EXITKEY_H
#include "hw.h"
#ifdef EXIT_KEY
/* the wheel held for held ms (def: the program's own time): 1 = go back */
__attribute__((noinline, unused, minsize)) static int exit_now(u32 held, u32 def)
{
  u32 v = *(volatile const u32 *)0x13Cu, m = (v >> 16) == 0x5845u ? (v & 3u) : 0u;
  if (m == 1u || m == 2u) return held > 300u && (hw_buttons() & (m == 1u ? BTN_A : BTN_C)) != 0u;
  return held > (m == 3u ? 5000u : def);
}
#define EXIT_NOW(held, def) exit_now((held), (def))
#else
#define EXIT_NOW(held, def) ((held) > (def))
#endif
#endif
