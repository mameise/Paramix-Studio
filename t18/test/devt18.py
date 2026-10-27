# T18 app (t18.bin, device program) in Unicorn with a simulated Paradise: hardware calls are caught,
# the screen goes into a picture, the SysTick moves every RT instructions (96 MHz Cortex-M3).
# python3 devt18.py <t18.elf> <paradise_image_with_t18> <ms> [KEYS="t:mask,..."] [SHOTS="t,t,..."] [OUT=dir]
# Prints per second: emulated ms of the guest (100 % = real time).
import sys, struct, subprocess, os
from unicorn import *
from unicorn.arm_const import *
elf, img, total_ms = sys.argv[1], sys.argv[2], int(sys.argv[3])
RT = int(os.environ.get('RT', '64000'))          # instructions per millisecond (96 MHz, ~1.5 cycles each)
out = os.environ.get('OUT', '.')
syms = {}
for l in subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
fl = bytearray(open(img, 'rb').read())
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
# as on the Paradise: PRAM 64 KB (the stub copies 64 KB from 0xA0000), RAM at 0x20000000 only 4 KB,
# flash window 0x10000000 = flash 0x11000.. (base 0x60011000)
APPOFF = int(os.environ.get('APPOFF', '0xA0000'), 16)   # test29: a program in the menu
u.mem_map(0, 0x10000); u.mem_write(0, bytes(fl[APPOFF:APPOFF + 0x10000]))
u.mem_map(0x18000000, 0x20000); u.mem_map(0x20000000, 0x2000 if os.environ.get('RAM8K') else 0x1000)
u.mem_map(0x10000000, 0x1000000)
# the flash window as measured on the device (1.6.6): 1 MB from the base register (0x4002F004), beyond that
# something else (here zeros); NOT updated on flash writes (like a cache that keeps old lines). WIN16=1: old model
def win_load(base):
    if os.environ.get('WIN16') == '1': u.mem_write(0x10000000, bytes(fl[0x11000:]) + bytes(0x11000)); return
    o = base - 0x60000000
    u.mem_write(0x10000000, bytes(fl[o:o + 0x100000]).ljust(0x100000, b'\xff') + bytes(0xF00000))
    WIN[0] = base
WIN = [0x60011000]
win_load(0x60011000)
CACHE_READS = [0]
UNC = [0]
BLK = {}
if os.environ.get('UNCOUNT'):
    def _unc(uc, acc, addr, size, val, ud):
        UNC[0] += 1; b = (addr - 0x60000000) >> 16; BLK[b] = BLK.get(b, 0) + 1
    u.hook_add(UC_HOOK_MEM_READ, _unc, begin=0x60000000, end=0x60FFFFFF)
PRAM_BAD = [0]
def pram_wr(uc, acc, addr, size, val, ud):
    pc = uc.reg_read(UC_ARM_REG_PC)
    if not (0x10000000 <= pc < 0x10100000 or 0x1800E600 <= pc < 0x18014400) or size not in (2, 4):
        PRAM_BAD[0] += 1
        if PRAM_BAD[0] < 5: print('PRAM written from pc', hex(pc), 'size', size, 'at', hex(addr))
# PRAMQUIRK=1: the PRAM as found on the device (1.6.2): a halfword write fills the other half of its word
# with the halfword written last (anywhere), not with what was there
QL = [0]
def pram_quirk(uc, acc, addr, size, val, ud):
    if size == 2:
        other = addr ^ 2
        uc.mem_write(other, struct.pack('<H', QL[0]))
        QL[0] = val & 0xFFFF
    elif size == 4: QL[0] = (val >> 16) & 0xFFFF
if os.environ.get('PRAMQUIRK') == '1': u.hook_add(UC_HOOK_MEM_WRITE, pram_quirk, begin=0, end=0xFFFF)
if os.environ.get('PRAMCHECK', '1') == '1': u.hook_add(UC_HOOK_MEM_WRITE, pram_wr, begin=0, end=0xFFFF)
# Unicorn keeps old translations of code buffer addresses written by the program: drop them on each write
JWLOG = [] if os.environ.get('JWLOG') else None
if 't18_pram_w16' in syms and os.environ.get('PWHOOK', '1') == '1':
    def _pw(uc, ad, sz, ud):
        a = uc.reg_read(UC_ARM_REG_R0) & ~1
        uc.ctl_remove_cache(a & ~0x3FF, (a & ~0x3FF) + 0x800); _pw.n += 1
        if JWLOG is not None: JWLOG.append((a, uc.reg_read(UC_ARM_REG_R1) & 0xFFFF))
    _pw.n = 0; u.hook_add(UC_HOOK_CODE, _pw, begin=syms['t18_pram_w16'] & ~1, end=syms['t18_pram_w16'] & ~1)

u.mem_map(0x60000000, 0x1000000); u.mem_write(0x60000000, bytes(fl))
u.mem_map(0x40000000, 0x100000)
u.mem_write(0x4000B014, b'\x60\x00\x00\x00')   # test6: UART1 line status: transmitter empty (T18_LINK sends hello frames)
UTX = []
def _utx(uc, acc, addr, size, val, ud): UTX.append(val & 0xFF)
u.hook_add(UC_HOOK_MEM_WRITE, _utx, begin=0x4000B000, end=0x4000B000)
def _xipw(uc, acc, addr, size, val, ud):
    if addr == 0x4002F004 and val != WIN[0]: win_load(val); uc.ctl_flush_tb()
u.hook_add(UC_HOOK_MEM_WRITE, _xipw, begin=0x4002F004, end=0x4002F007); u.mem_map(0x45000000, 0x1000); u.mem_map(0xE000E000, 0x1000); u.mem_map(0xE0001000, 0x1000)   # DWT (CYCCNT reads 0 here)
fb = [[0] * 128 for _ in range(128)]
rect = {'x0': 0, 'x1': 0, 'y': 0, 'x': 0}
KS = []
for p in os.environ.get('KEYS', '').split(','):
    if ':' in p: t, m = p.split(':'); KS.append((float(t), int(m)))
SH = [float(x) for x in os.environ.get('SHOTS', '').split(',') if x]
state = {'ms': 0, 'tone': [], 'flashw': 0, 'rows': 0}
def keys():
    m = 0
    for t, v in KS:
        if state['ms'] / 1000.0 >= t: m = v
    return m
def ret(uc, v=None):
    if v is not None: uc.reg_write(UC_ARM_REG_R0, v)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)
def h_begin(uc):
    x0, y0, x1, y1 = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
    rect.update(x0=x0, x1=x1, y=y0, x=x0); ret(uc)
def h_row(uc):
    p, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    d = struct.unpack('<%dH' % n, uc.mem_read(p, 2 * n))
    for v in d:
        if rect['y'] < 128 and rect['x'] < 128: fb[rect['y']][rect['x']] = v
        rect['x'] += 1
        if rect['x'] >= rect['x1']: rect['x'] = rect['x0']; rect['y'] += 1
    state['rows'] += 1; ret(uc)
def h_tone(uc): state['tone'].append((state['ms'], uc.reg_read(UC_ARM_REG_R0))); ret(uc)
def h_btn(uc): ret(uc, keys())
def h_upd(uc):
    ad, src = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    d = bytes(uc.mem_read(src, 0x1000)); a = ad & 0xFFF000
    if fl[a:a + 0x1000] == d: ret(uc, 0); return
    uc.mem_write(0x60000000 + a, d); fl[a:a + 0x1000] = d; state['flashw'] += 1; ret(uc, 1)
    if os.environ.get('FWLOG'): print('FW', state['ms'], hex(a))
def h_erase(uc):
    a = uc.reg_read(UC_ARM_REG_R0) & 0xFFF000
    uc.mem_write(0x60000000 + a, b'\xff' * 0x1000); fl[a:a + 0x1000] = b'\xff' * 0x1000; state['flashw'] += 1; ret(uc)
    if os.environ.get('FWLOG'): print('FE', state['ms'], hex(a))
def h_back(uc): state['back'] = 1; uc.emu_stop()
def h_nop(uc): ret(uc)
def h_fatal(uc): print('FATAL code', uc.reg_read(UC_ARM_REG_R0), 'info', hex(uc.reg_read(UC_ARM_REG_R1)), 'lr', hex(uc.reg_read(UC_ARM_REG_LR))); state['back'] = 1; uc.emu_stop()
PANLOG = os.environ.get('PANLOG'); PAN = {'n': 0, 'last': None}
def h_end(uc):
    # 1.6.13 test3: every picture the panel got (PANLOG=dir): what the user saw, frame by frame
    if PANLOG:
        d = struct.pack('<16384H', *[v for r in fb for v in r])
        if d != PAN['last']:
            PAN['last'] = d; open(os.path.join(PANLOG, 'p%05d_%07d.raw' % (PAN['n'], state['ms'])), 'wb').write(d); PAN['n'] += 1
    ret(uc)
H = {'hw_px_begin': h_begin, 'hw_px_row': h_row, 'hw_px_end': h_end, 'hw_tone': h_tone, 'hw_buttons': h_btn,
     'flash_update4k': h_upd, 'flash_erase4k': h_erase, 'hw_back_to_paradise': h_back, 'hw_init': h_nop,
     'hw_backlight': h_nop, 'hw_bl_pwm_off': h_nop, 'hw_wdt_feed': h_nop}
if not os.environ.get('FATALDRAW'): H['fatal'] = h_fatal
for n, f in H.items():
    if n not in syms: print('no symbol', n); continue
    a = syms[n] & ~1
    if n == 'hw_init':   # runs once from code that later becomes the translator's buffer: unhook after it
        HI = {}
        def h_init(uc, ad, sz, ud): ret(uc); uc.hook_del(HI['h'])
        HI['h'] = u.hook_add(UC_HOOK_CODE, h_init, begin=a, end=a)
        continue
    u.hook_add(UC_HOOK_CODE, (lambda f: lambda uc, ad, sz, ud: f(uc))(f), begin=a, end=a)
def bad(uc, access, addr, size, value, ud):
    print('unmapped', access, hex(addr), 'pc', hex(uc.reg_read(UC_ARM_REG_PC))); return False
u.hook_add(UC_HOOK_MEM_UNMAPPED, bad)
def shot(name):
    from PIL import Image
    im = Image.new('RGB', (128, 128))
    for y in range(128):
        for x in range(128):
            v = fb[y][x]; im.putpixel((x, y), (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3))
    im.save(os.path.join(out, name))
PROF = [float(x) * 1000 for x in os.environ.get('PROF', '').split(',') if x]
import bisect, collections
fs = []
for l in subprocess.run(['arm-none-eabi-nm', '-S', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 4 and p[2] in 'tTwW': fs.append((int(p[0], 16), int(p[1], 16), p[3]))
fs.sort(); fst = [x[0] for x in fs]; pc_cnt = collections.Counter(); prof_on = [False]
jc = syms.get('JC', 0); jcl = syms.get('__bss_end', 0)
def h_blk(uc, a, sz, ud):
    if not prof_on[0]: return
    if jc and jc <= a < jc + 0x8000: pc_cnt['<JIT code>'] += sz // 2; return
    i = bisect.bisect_right(fst, a) - 1
    pc_cnt[fs[i][2] if i >= 0 and a < fs[i][0] + fs[i][1] + 4 else '?%x' % a] += sz // 2
if PROF: u.hook_add(UC_HOOK_BLOCK, h_blk)
# SRAMFILL=seed: the SRAM full of leftovers as on the device (random bytes) before the start
if os.environ.get('SRAMFILL'):
    import random; random.seed(int(os.environ['SRAMFILL'])); u.mem_write(0x18000000, bytes(random.getrandbits(8) for _ in range(0x20000)))
# a Paradise clock in the SRAM, as the Paradise leaves it (PARA="2026-10-15 18:42:07", days/seconds base made up)
if os.environ.get('PARA'):
    import datetime
    dt = datetime.datetime.strptime(os.environ['PARA'], '%Y-%m-%d %H:%M:%S'); cnt = int(os.environ.get('RTC0', '5000'))
    days = 650; tod = dt.hour * 3600 + dt.minute * 60 + dt.second; sek = (tod - cnt) % 86400
    o = 0x1BA4
    u.mem_write(0x18000000 + o - 0x10, struct.pack('<II', days, sek))
    u.mem_write(0x18000000 + o, struct.pack('<6H', dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second))
EXEC = {}
if os.environ.get('EXECLOG'):
    def _ex(uc, a, sz, ud):
        if a not in EXEC: EXEC[a] = bytes(uc.mem_read(a, sz))
    u.hook_add(UC_HOOK_CODE, _ex, begin=syms['__jc_start'], end=0xFFFF)
# ALIGNCHK=1: LDRD/STRD/LDM/STM/PUSH/POP with an address that is not a multiple of 4 fault on the device
# (UNALIGNED, even with the trap off); Unicorn does them. Checked on every such instruction executed.
ALIGN_BAD = []
if os.environ.get('ALIGNCHK'):
    import capstone
    _cs = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS); _cs.detail = True
    _seen = {}
    def _al(uc, a, sz, ud):
        k = (a, bytes(uc.mem_read(a, sz)))
        ins = _seen.get(k)
        if ins is None:
            ins = next(_cs.disasm(k[1], a), None); _seen[k] = ins or False
        if not ins: return
        m = ins.mnemonic
        if not (m.startswith('ldrd') or m.startswith('strd') or m.startswith('ldm') or m.startswith('stm') or m.startswith('push') or m.startswith('pop')): return
        if m.startswith('push') or m.startswith('pop'): base = uc.reg_read(UC_ARM_REG_SP)
        else:
            op = [o for o in ins.operands if o.type == capstone.arm.ARM_OP_MEM]
            if op:
                base = uc.reg_read(ins.reg_name(op[0].mem.base) and getattr(__import__('unicorn.arm_const', fromlist=['x']), 'UC_ARM_REG_' + ins.reg_name(op[0].mem.base).upper().replace('SB', 'R9').replace('SL', 'R10').replace('FP', 'R11').replace('IP', 'R12'))) + op[0].mem.disp
            else:
                r = ins.operands[0].reg; nm = ins.reg_name(r).upper().replace('SB', 'R9').replace('SL', 'R10').replace('FP', 'R11').replace('IP', 'R12')
                base = uc.reg_read(getattr(__import__('unicorn.arm_const', fromlist=['x']), 'UC_ARM_REG_' + nm))
        if base & 3 and len(ALIGN_BAD) < 20:
            ALIGN_BAD.append((hex(a), ins.mnemonic, ins.op_str, hex(base))); print('UNALIGNED', hex(a), ins.mnemonic, ins.op_str, hex(base))
    u.hook_add(UC_HOOK_CODE, _al, begin=0, end=0xFFFF)
    u.hook_add(UC_HOOK_CODE, _al, begin=0x10000000, end=0x10100000)
sp, rst = struct.unpack('<II', bytes(u.mem_read(0, 8)))
u.reg_write(UC_ARM_REG_SP, sp)
pc = rst; gms = syms['g_ms']; rtc0 = int(os.environ.get('RTC0', '5000'))
st_sym = syms['g_t18st']
si = 0; last = (0, 0)
IRQ = os.environ.get('IRQ') == '1'
import random as _r; IRQRND = _r.Random(int(os.environ.get('IRQSEED', '1')))
IRQ_N = [0]
if IRQ: u.mem_map(0x30000000, 0x1000); u.mem_write(0x30000000, b'\xfe\xe7' * 8)   # return point of the handler (instead of EXC_RETURN)
def systick_on(): return struct.unpack('<I', bytes(u.mem_read(0xE000E010, 4)))[0] & 2
def irq_enter(pc):
    sp = u.reg_read(UC_ARM_REG_SP); xpsr = u.reg_read(UC_ARM_REG_XPSR)
    pad = sp & 4
    sp = (sp - 32 - pad) & 0xFFFFFFFF
    regs = [u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_LR)]
    u.mem_write(sp, struct.pack('<8I', *regs, pc & ~1, (xpsr | 0x01000000 | (0x200 if pad else 0)) & 0xFFFFFFFF))
    u.reg_write(UC_ARM_REG_SP, sp); u.reg_write(UC_ARM_REG_LR, 0x30000001)
    u.reg_write(UC_ARM_REG_PC, syms['SysTick_Handler'] | 1); IRQ_N[0] += 1
    if os.environ.get('IRQDBG') and IRQ_N[0] < 6: print('irq at', hex(pc), 'sp', hex(sp))
def irq_return():
    sp = u.reg_read(UC_ARM_REG_SP); f = struct.unpack('<8I', bytes(u.mem_read(sp, 32)))
    for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_LR), f[:6]): u.reg_write(r, v)
    sp += 32 + (4 if f[7] & 0x200 else 0)
    u.reg_write(UC_ARM_REG_SP, sp); u.reg_write(UC_ARM_REG_XPSR_NZCVQ, f[7] & 0xF8000000); u.reg_write(UC_ARM_REG_PC, f[6] | 1)
    if os.environ.get('IRQDBG') and IRQ_N[0] < 6: print('ret to', hex(f[6]), 'sp', hex(sp), 'gms', struct.unpack('<I', bytes(u.mem_read(gms, 4)))[0])
# 1.6.9 test26 CLK_ECO: the AHB prescaler at 0x45000010 slows the core: RT >> sh instructions per ms
state['sh'] = 0; state['shms'] = [0, 0, 0, 0]
def _ahb(uc, acc, a, sz, v, ud):
    state['sh'] = v & 7
    if os.environ.get('ECOLOG'): print('AHB', v & 7, 'at ms', state['ms'])
u.hook_add(UC_HOOK_MEM_WRITE, _ahb, begin=0x45000010, end=0x45000013)
# 1.6.13 test1 T18_PW (as devt7.py): system clock source (0x4500000C: 0 IHRC 12 MHz, 4 PLL; status in bits 6:4), PLL
# locked, RTC on with 1 s, deep sleep (PMU_CTRL bit 1, then WFI): the time runs on to the next RTC second or a key change
u.mem_write(0x4500000C, struct.pack('<I', 0x44)); u.mem_write(0x45000008, struct.pack('<I', 0x55)); u.mem_write(0x45000200, struct.pack('<I', 0x80))
state['ihrc'] = 0; state['pw'] = {'full': 0, 'ihrc': 0, 'sleep': 0}
def _clk(uc, acc, a, sz, v, ud):
    state['ihrc'] = (v & 7) == 0
    uc.mem_write(0x4500000C, struct.pack('<I', (v & 7) | ((v & 7) << 4)))
    if os.environ.get('ECOLOG'): print('CLKSRC', v & 7, 'at ms', state['ms'])
u.hook_add(UC_HOOK_MEM_WRITE, _clk, begin=0x4500000C, end=0x4500000F)
def _pmu(uc, acc, a, sz, v, ud):
    if v & 2 and not state.get('dsleep'):
        t = state['ms']
        while True:
            t += 1
            if t % 1000 == 0 or t >= total_ms or any(t / 1000.0 >= kt > state['ms'] / 1000.0 for kt, _ in KS): break
        state['dsleep'] = t
        if t % 1000 == 0 and 'g_pw_rtcw' in syms: uc.mem_write(syms['g_pw_rtcw'], b'\x01')   # the RTC interrupt ran
        if int(os.environ.get('RTCLAG', '0')):
            # 1.6.13 test3: the counter shows the new second only a few reads after the wake (32 kHz domain)
            state['rtc_new'] = rtc0 + t // 1000; state['rtclag'] = int(os.environ['RTCLAG'])
        else: uc.mem_write(0x45000304, struct.pack('<I', rtc0 + t // 1000))
u.hook_add(UC_HOOK_MEM_WRITE, _pmu, begin=0x45000300, end=0x45000303)
def _rtcrd(uc, acc, a, sz, v, ud):
    if 'rtc_new' in state:
        if state['rtclag'] > 0: state['rtclag'] -= 1
        else: uc.mem_write(0x45000304, struct.pack('<I', state.pop('rtc_new')))
u.hook_add(UC_HOOK_MEM_READ, _rtcrd, begin=0x45000304, end=0x45000307)
RTCLAGMS = int(os.environ.get('RTCLAGMS', '0'))
if RTCLAGMS:
    # 1.6.13 test3: the counter shows a new second only RTCLAGMS ms after it began (the RTC interrupt comes on time)
    def _rtcms(uc, acc, a, sz, v, ud):
        eff = state.get('dsleep') or state['ms']
        uc.mem_write(0x45000304, struct.pack('<I', rtc0 + max(eff - RTCLAGMS, 0) // 1000))
    u.hook_add(UC_HOOK_MEM_READ, _rtcms, begin=0x45000304, end=0x45000307)
while state['ms'] < total_ms and 'back' not in state:
    u.mem_write(0x45000304, struct.pack('<I', rtc0 + state['ms'] // 1000))
    try:
        if os.environ.get('FLUSHTB'): u.ctl_flush_tb()
        if IRQ:
            # the SysTick as a real exception: frame on the stack, handler, return through EXC_RETURN
            left = RT + IRQRND.randrange(-RT // 2, RT // 2)
            while left > 0:
                u.emu_start(pc | 1, 0x30000000, count=left)
                pc = u.reg_read(UC_ARM_REG_PC) & ~1
                if pc == 0x30000000: irq_return(); pc = u.reg_read(UC_ARM_REG_PC); left -= 100; continue
                break
            if u.reg_read(UC_ARM_REG_PRIMASK) & 1 or not systick_on(): state['pend'] = 1
            else: irq_enter(pc); pc = u.reg_read(UC_ARM_REG_PC)
        else:
            _sh = int(os.environ.get('IHRCSH', '3')) if state['ihrc'] else state['sh']
            u.emu_start(pc | 1, 0xFFFFFFF0, count=max(1, RT >> _sh)); state['shms'][min(_sh, 3)] += 1
            state['pw']['ihrc' if state['ihrc'] else 'full'] += 1
    except UcError as ex:
        print('error', ex, hex(u.reg_read(UC_ARM_REG_PC))); break
    pc = u.reg_read(UC_ARM_REG_PC)
    _ds = state.pop('dsleep', 0)
    if _ds:
        n = _ds - state['ms']; state['ms'] = _ds; state['pw']['sleep'] += n
        if os.environ.get('DBGSLEEP'): print('slept', n, 'to ms', state['ms'])
        if state['ms'] % 1000 == 0:
            st = struct.unpack('<8I', bytes(u.mem_read(st_sym, 32)))
            print('%4d s: emulated %4d ms (deep sleep)' % (state['ms'] // 1000, st[0] - last[0]), flush=True); last = st
        continue
    state['ms'] += 1
    if not IRQ:
        v = struct.unpack('<I', u.mem_read(gms, 4))[0]; u.mem_write(gms, struct.pack('<I', v + 1))
        if 'g_ms_raw' in syms: _r = syms['g_ms_raw']; u.mem_write(_r, struct.pack('<I', struct.unpack('<I', u.mem_read(_r, 4))[0] + 1))
    if os.environ.get('JSTAT') and state['ms'] % int(os.environ['JSTAT']) == 0 and state['ms'] <= 400:
        js = struct.unpack('<8I', bytes(u.mem_read(syms['t18_jit_stat'], 32))); jb = struct.unpack('<I', bytes(u.mem_read(syms['t18_jit_bytes'], 4)))[0]
        print('ms', state['ms'], 'blocks', js[0], 'flushes', js[3], 'bytes', jb, 'gpc', hex(struct.unpack('<I', bytes(u.mem_read(syms['g_tg'] + 60, 4)))[0]))
    if os.environ.get('GPC'):
        gpc = struct.unpack('<I', bytes(u.mem_read(syms['g_tg'] + 60, 4)))[0]
        lo, hi = [int(x, 16) for x in os.environ['GPC'].split('-')]
        if lo <= gpc < hi and state.get('gpcn', 0) < 20: state['gpcn'] = state.get('gpcn', 0) + 1; print('guest pc', hex(gpc), 'at ms', state['ms'], 'stage', struct.unpack('<I', bytes(u.mem_read(syms['g_stage'], 4)))[0])
    if si < len(SH) and state['ms'] >= SH[si] * 1000: shot('shot_%02d.png' % si); si += 1
    if PROF: prof_on[0] = PROF[0] <= state['ms'] < PROF[1]
    if os.environ.get('HISTRESET') and state['ms'] == int(os.environ['HISTRESET']): u.mem_write(0x20001000, bytes(256))   # test5: slow access histogram from here
    if state['ms'] % 1000 == 0:
        st = struct.unpack('<8I', bytes(u.mem_read(st_sym, 32)))
        if 'g_pres_n' in syms: pn = struct.unpack('<I', bytes(u.mem_read(syms['g_pres_n'], 4)))[0]; print('      pictures', pn - state.get('pn', 0)); state['pn'] = pn
        print('%4d s: emulated %4d ms, dropped %5d ms, flash writes %d, rows %d%s' % (state['ms'] // 1000, st[0] - last[0], st[1], state['flashw'], state['rows'], ', uncached reads %d' % UNC[0] if os.environ.get('UNCOUNT') else ''), flush=True); UNC[0] = 0
        last = st
st = struct.unpack('<8I', bytes(u.mem_read(st_sym, 32)))
print('emulated ms', st[0], 'dropped ms', st[1], 'flash', st[2], 'map', st[3], 'lost', st[5])
if '__stack_bottom' in syms and '__stack_top' in syms:
    lo, hi = syms['__stack_bottom'], syms['__stack_top']
    m = bytes(u.mem_read(lo + 4, hi - lo - 4)); k = next((i for i in range(0, len(m), 4) if m[i:i + 4] != b'\0\0\0\0'), len(m))
    print('stack used', hi - lo - 4 - k, 'of', hi - lo)
    if 'g_pool_n' in syms: print('hole pages used', struct.unpack('<I', bytes(u.mem_read(syms['g_pool_n'], 4)))[0])
if 'g_para' in syms:
    import datetime
    v = struct.unpack('<I', bytes(u.mem_read(syms['g_para'], 4)))[0]
    print('Paradise clock found:', datetime.datetime.utcfromtimestamp(v) if v else 'no')
print('interrupts', IRQ_N[0])
if 'g_ncached' in syms: print('blocks through the cache', struct.unpack('<I', bytes(u.mem_read(syms['g_ncached'], 4)))[0])
if os.environ.get('ALIGNCHK'): print('unaligned double/multiple accesses', len(ALIGN_BAD))
print('ms', state['ms'], 'tones', len(state['tone']), 'back' if 'back' in state else '', 'pc', hex(pc), 'bad PRAM writes', PRAM_BAD[0])
if PROF:
    tot = sum(pc_cnt.values())
    for k, v in pc_cnt.most_common(25): print('  %-24s %5.1f%%' % (k, 100.0 * v / tot))
print('ms per clock step (96, 48, 24, 12 MHz):', state['shms'])
print('power ms: full', state['pw']['full'], 'ihrc', state['pw']['ihrc'], 'deep sleep', state['pw']['sleep'])
open(os.path.join(out, 'flash_after.bin'), 'wb').write(fl)
if JWLOG is not None:
    open(os.environ['JWLOG'], 'w').write('\n'.join('%x %04x' % w for w in JWLOG))
if EXEC:
    import pickle; pickle.dump(EXEC, open(os.environ['EXECLOG'], 'wb'))
if os.environ.get('BLKOUT'): open(os.environ['BLKOUT'], 'w').write('\n'.join('%x %d' % kv for kv in sorted(BLK.items(), key=lambda kv: -kv[1])))
if os.environ.get('PEEK'):
    for _p in os.environ['PEEK'].split(','):
        _a, _n = _p.split(':'); print('PEEK', _a, bytes(u.mem_read(int(_a, 16), int(_n))).hex())
if os.environ.get('DUMPPRAM'): open(os.environ['DUMPPRAM'], 'wb').write(bytes(u.mem_read(0, 0x10000)))
shot('shot_end.png')
if os.environ.get('UARTLOG'): print('uart tx', len(UTX), 'bytes:', ' '.join('%02x' % b for b in UTX[:80]))
