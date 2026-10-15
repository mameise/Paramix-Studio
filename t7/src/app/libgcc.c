/* 64-bit division for the sound timers (no compiler runtime in this build).
 * AEABI: quotient in r0:r1, remainder in r2:r3. */
typedef unsigned long long u64; typedef long long s64;
__attribute__((used)) u64 t7_udiv64(u64 n, u64 d, u64 *rem)
{
  if (!d) { *rem = 0; return ~0ull; }
  u64 q = 0, r = 0;
  for (int i = 63; i >= 0; i--) { r = (r << 1) | ((n >> i) & 1u); if (r >= d) { r -= d; q |= 1ull << i; } }
  *rem = r; return q;
}
__attribute__((used)) s64 t7_sdiv64(s64 n, s64 d, s64 *rem)
{
  int neg = (n < 0) ^ (d < 0), nn = n < 0; u64 r;
  u64 q = t7_udiv64(n < 0 ? (u64)-n : (u64)n, d < 0 ? (u64)-d : (u64)d, &r);
  *rem = nn ? -(s64)r : (s64)r; return neg ? -(s64)q : (s64)q;
}
__attribute__((naked)) void __aeabi_uldivmod(void)
{
  __asm__ volatile("push {r4, lr}\n sub sp, #16\n add r4, sp, #8\n str r4, [sp]\n bl t7_udiv64\n ldrd r2, r3, [sp, #8]\n add sp, #16\n pop {r4, pc}\n");
}
__attribute__((naked)) void __aeabi_ldivmod(void)
{
  __asm__ volatile("push {r4, lr}\n sub sp, #16\n add r4, sp, #8\n str r4, [sp]\n bl t7_sdiv64\n ldrd r2, r3, [sp, #8]\n add sp, #16\n pop {r4, pc}\n");
}
