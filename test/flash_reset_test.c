#include <assert.h>
#include <stdio.h>
#include <stdint.h>
typedef uint32_t u32;
#include "../flash/src/reset_policy.h"
int main(void){
  ResetPolicy s;
  rp_init(&s,1); assert(rp_hint(&s)==1);
  assert(!rp_poll(&s,1,10)); assert(!rp_poll(&s,1,100)); /* held on entry cannot reset */
  assert(!rp_poll(&s,0,100)); assert(!rp_poll(&s,1,100));
  assert(!rp_poll(&s,1,102) && s.phase==1);
  assert(!rp_poll(&s,1,103) && rp_hint(&s)==2); assert(rp_poll(&s,0,103));
  assert(!rp_poll(&s,0,200)); /* one reset only */
  assert(rp_idle(&s,1,1)); rp_poll(&s,0,0); rp_poll(&s,1,10); rp_poll(&s,1,13);
  rp_activity(&s); assert(!rp_poll(&s,0,14) && !s.allowed); /* incoming UART wins over pending release */
  assert(rp_idle(&s,1,1)); assert(!rp_poll(&s,1,200)); assert(!rp_poll(&s,1,205)); /* fresh release required */
  for(u32 type=9;type<=11;type++){
    rp_init(&s,1); rp_activity(&s); rp_packet(&s,type);
    assert(!rp_idle(&s,1,1)); rp_packet(&s,3); rp_packet(&s,1);
    assert(!rp_idle(&s,1,1)); /* cancel, hello, reconnect-style idle cannot unlock unfinished writes */
    assert(rp_idle(&s,0,1)); assert(s.writing && !s.allowed);
    rp_packet(&s,12); assert(!s.allowed); assert(rp_idle(&s,1,1));
  }
  rp_init(&s,0); assert(!rp_idle(&s,1,0)); assert(!rp_poll(&s,1,20));
  rp_init(&s,1); rp_poll(&s,0,0); rp_poll(&s,1,0xFFFFFFFEu);
  rp_poll(&s,1,1); assert(s.phase==2); assert(rp_poll(&s,0,1)); /* counter wrap */
  rp_init(&s,1); rp_poll(&s,0,0); rp_poll(&s,1,2); rp_poll(&s,0,4);
  rp_poll(&s,1,4); assert(!rp_poll(&s,1,6) && s.phase==1); /* short/repeated presses */
  puts("PASS: long-C timing, release gating, activity cancellation, write latch, explicit idle and wrap");
}
