/* Long-C reset policy. Pure state transitions shared by firmware and host tests.
 * UART activity disarms immediately; only an explicit successful host idle
 * boundary can re-arm after traffic. CANCEL/HELLO never clear a write latch. */
#ifndef FLASH_RESET_POLICY_H
#define FLASH_RESET_POLICY_H
typedef struct {
  u32 start, allowed, writing, release, phase, ui;
} ResetPolicy;
static void rp_activity(ResetPolicy *s){ s->allowed=0; s->phase=0; s->release=1; }
static void rp_init(ResetPolicy *s,u32 valid){ s->writing=0; s->ui=99; rp_activity(s); s->allowed=valid; }
static void rp_packet(ResetPolicy *s,u32 type){
  if(type==9u || type==10u || type==11u) s->writing=1;
  if(type==12u) s->writing=0;
}
static int rp_idle(ResetPolicy *s,u32 allow,u32 valid){
  rp_activity(s);
  if(!allow) return 1;
  if(s->writing || !valid) return 0;
  s->allowed=1; return 1;
}
static int rp_poll(ResetPolicy *s,u32 pressed,u32 now){
  if(!s->allowed || s->writing){ s->phase=0; s->release=1; return 0; }
  if(!pressed){
    if(s->phase==2u){ rp_activity(s); return 1; }
    s->release=0; s->phase=0; return 0;
  }
  if(s->release) return 0;
  if(!s->phase){ s->phase=1; s->start=now; }
  else if(s->phase==1u && now-s->start>=3u) s->phase=2;
  return 0;
}
static u32 rp_hint(const ResetPolicy *s){ return !s->allowed || s->writing ? 0u : s->phase==2u ? 2u : 1u; }
#endif
