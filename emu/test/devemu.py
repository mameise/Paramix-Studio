# TamaEmu (emu.bin, device program) in Unicorn, like t7/test/devt7.py: hardware calls are caught, the screen is
# drawn into a picture, the SysTick advances every RT instructions (96 MHz Cortex-M3 ~ RT per ms). Measures the
# speed (emulated ms against real ms, dropped ms) and the power states (full clock, 12 MHz, deep sleep).
# python3 devemu.py <emu.elf> <paradise image with TamaEmu (mkimg.js)> <ms> [KEYS="t:mask,..."] [SHOTS="t,..."] [OUT=dir]
#   load: the code run per ms until the main loop waits (wfi), against RT instructions
#   LAGLOG=n: every n ms the emulated and dropped ms in that time
import sys, struct, subprocess, os
from unicorn import *
from unicorn.arm_const import *
elf, img, total_ms = sys.argv[1], sys.argv[2], int(sys.argv[3])
RT = int(os.environ.get('RT', '64000'))
out = os.environ.get('OUT', '.')
syms = {}
for l in subprocess.run(['llvm-nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
fl = bytearray(open(img, 'rb').read())
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
APPOFF = int(os.environ.get('APPOFF', '0xA0000'), 16)
u.mem_map(0, 0x10000); u.mem_write(0, bytes(fl[APPOFF:APPOFF + 0x10000]))
u.mem_map(0x18000000, 0x20000); u.mem_map(0x20000000, 0x1000)
u.mem_map(0x10000000, 0x100000)
u.mem_map(0x60000000, 0x1000000); u.mem_write(0x60000000, bytes(fl))
u.mem_map(0x40000000, 0x100000); u.mem_map(0x45000000, 0x1000); u.mem_map(0xE000E000, 0x1000); u.mem_map(0xE0001000, 0x1000)
u.mem_write(0x4000B014, struct.pack('<I', 0x60))   # UART1 LSR: sender empty, nothing received
WIN = [0]
def win_load(base):
    WIN[0] = base; o = base & 0xFFFFFF
    u.mem_write(0x10000000, bytes(fl[o:o + 0x100000]).ljust(0x100000, b'\xff'))
win_load(0x60011000)
def _xipw(uc, acc, addr, size, val, ud):
    if addr == 0x4002F004 and val != WIN[0]: win_load(val); state["pw_dirty"] = (0x10000000, 0x100000)
u.hook_add(UC_HOOK_MEM_WRITE, _xipw, begin=0x4002F004, end=0x4002F007)
fb = [[0] * 128 for _ in range(128)]
rect = {'x0': 0, 'x1': 0, 'y': 0, 'x': 0}
KS = []
for p in os.environ.get('KEYS', '').split(','):
    if ':' in p: t, m = p.split(':'); KS.append((float(t), int(m)))
SH = [float(x) for x in os.environ.get('SHOTS', '').split(',') if x]
state = {'ms': 0, 'tone': [], 'flashw': 0, 'bl': [], 'blpct': [], 'busy': 0, 'busyn': 0, 'ins': 0}
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
    state['rows'] = state.get('rows', 0) + 1; ret(uc)
def h_push(uc):
    p = uc.reg_read(UC_ARM_REG_R0); x0, y0, x1, y1 = uc.reg_read(UC_ARM_REG_R2), uc.reg_read(UC_ARM_REG_R3), *struct.unpack('<II', uc.mem_read(uc.reg_read(UC_ARM_REG_SP), 8))
    for y in range(y0, min(y1, 128)):
        d = struct.unpack('<%dH' % (x1 - x0), uc.mem_read(p + 2 * x0, 2 * (x1 - x0)))
        for i, v in enumerate(d):
            if x0 + i < 128: fb[y][x0 + i] = v
    ret(uc)
def h_tone(uc): state['tone'].append((state['ms'], uc.reg_read(UC_ARM_REG_R0))); ret(uc)
def h_btn(uc): ret(uc, keys())
def h_prog(uc):
    ad, src = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    d = bytes(uc.mem_read(src & ~3, 0x1000)); a = ad & 0xFFFFFF
    uc.mem_write(0x60000000 + a, d); fl[a:a + 0x1000] = d; state['flashw'] += 1
    o = a - (WIN[0] & 0xFFFFFF)
    if 0 <= o < 0x100000: uc.mem_write(0x10000000 + o, d)
    ret(uc)
def h_erase(uc):
    a = uc.reg_read(UC_ARM_REG_R0) & 0xFFFFFF
    uc.mem_write(0x60000000 + a, b'\xff' * 0x1000); fl[a:a + 0x1000] = b'\xff' * 0x1000
    o = a - (WIN[0] & 0xFFFFFF)
    if 0 <= o < 0x100000: uc.mem_write(0x10000000 + o, b'\xff' * 0x1000)
    ret(uc)
def h_back(uc): state['back'] = 1; uc.emu_stop()
def h_nop(uc): ret(uc)
def h_bl(uc): state['bl'].append((state['ms'], uc.reg_read(UC_ARM_REG_R0))); ret(uc)
def h_blset(uc): state['blpct'].append((state['ms'], uc.reg_read(UC_ARM_REG_R0))); ret(uc)
H = {'hw_px_begin': h_begin, 'hw_px_row': h_row, 'hw_px_end': h_nop, 'hw_push': h_push, 'hw_tone': h_tone,
     'hw_buttons': h_btn, 'flash_prog4k': h_prog, 'flash_erase4k': h_erase, 'hw_back_to_paradise': h_back,
     'hw_init': h_nop, 'hw_backlight': h_bl, 'hw_bl_pwm_off': h_nop, 'hw_wdt_feed': h_nop, 'hw_bl_set': h_blset,
     'hw_wake_step': h_nop}
for n, f in H.items():
    if n not in syms: continue
    a = syms[n] & ~1
    if a >= syms.get('__initcode_start', 0x10000):   # start code: its room becomes the translator's afterwards
        u.hook_add(UC_HOOK_CODE, (lambda f: lambda uc, ad, sz, ud: not state.get('jit') and f(uc))(f), begin=a, end=a)
    else: u.hook_add(UC_HOOK_CODE, (lambda f: lambda uc, ad, sz, ud: f(uc))(f), begin=a, end=a)
def bad(uc, access, addr, size, value, ud):
    print('unmapped', access, hex(addr), 'pc', hex(uc.reg_read(UC_ARM_REG_PC))); return False
u.hook_add(UC_HOOK_MEM_UNMAPPED, bad)
if os.environ.get('ROMHIST'):   # reads of the guest firmware per flash block: cached window and direct (uncached)
    _cfg = APPOFF + 0x140; _go = struct.unpack_from('<I', fl, _cfg + 8)[0]; _gl = struct.unpack_from('<I', fl, _cfg + 12)[0]
    RH = {}
    def _rh(uc, acc, a, sz, v, ud):
        if a >= 0x60000000: k = ((a & 0xFFFFFF) - _go) >> 16; RH[('u', k)] = RH.get(('u', k), 0) + 1
        else: k = ((WIN[0] & 0xFFFFFF) + (a - 0x10000000) - _go) >> 16; RH[('c', k)] = RH.get(('c', k), 0) + 1
    u.hook_add(UC_HOOK_MEM_READ, _rh, begin=0x60000000 + _go, end=0x60000000 + _go + _gl - 1)
    u.hook_add(UC_HOOK_MEM_READ, _rh, begin=0x10000000, end=0x100FFFFF)
    import atexit
    def _rhp():
        _map = bytes(fl[_cfg + 24:_cfg + 152]); inv = {m: b for b, m in enumerate(_map) if m != 0xFF}
        tot = {}
        for (w, k), n in RH.items(): tot[k] = tot.get(k, 0) + n
        print('romhist (flash slot: guest block, reads, uncached reads):')
        print(' '.join('%d:%02x:%d:%d' % (k, inv.get(k, 0xFF), n, RH.get(('u', k), 0)) for k, n in sorted(tot.items(), key=lambda kv: -kv[1])[:40]))
    atexit.register(_rhp)
if os.environ.get('INSTRACE'):   # INSTRACE=ms: every instruction from that ms on, the last 300 (pc, sp)
    import collections, atexit
    TI = collections.deque(maxlen=300); _it = int(os.environ['INSTRACE'])
    def _ih(uc, a, sz, ud):
        if state['ms'] >= _it: TI.append((a, uc.reg_read(UC_ARM_REG_SP), uc.reg_read(UC_ARM_REG_LR)))
    u.hook_add(UC_HOOK_CODE, _ih)
    atexit.register(lambda: print('\n'.join('I %x sp %x lr %x' % t for t in TI)))
if os.environ.get('PWLOG'):   # writes into the PRAM from elsewhere than the copy loop
    _jcc = syms.get('J_cpcode', 0) & ~1
    def _pwl(uc, acc, a, sz, v, ud):
        p = uc.reg_read(UC_ARM_REG_PC)
        if not (_jcc <= p < _jcc + 24): print('PRAM write %x = %x size %d by pc %x at ms %d' % (a, v, sz, p, state['ms']))
    u.hook_add(UC_HOOK_MEM_WRITE, _pwl, begin=0, end=0xFFFF)
if os.environ.get('BLKTRACE'):
    import collections
    TR = collections.deque(maxlen=int(os.environ['BLKTRACE']))
    u.hook_add(UC_HOOK_BLOCK, lambda uc, a, sz, ud: TR.append((a, sz)))
    import atexit; atexit.register(lambda: print('last blocks', ' '.join('%x/%d' % t for t in TR)))
    atexit.register(lambda: open(os.path.join(out, 'pram.bin'), 'wb').write(bytes(u.mem_read(0, 0x10000))))
    atexit.register(lambda: open(os.path.join(out, 'sram.bin'), 'wb').write(bytes(u.mem_read(0x18000000, 0x20000))))
    atexit.register(lambda: print('regs', ' '.join('%x' % u.reg_read(r) for r in [UC_ARM_REG_R0 + i for i in range(13)] + [UC_ARM_REG_SP, UC_ARM_REG_LR])))
# AHB prescaler (CLK_ECO) and the clock source (PW): RT >> sh instructions per ms
state['sh'] = 0; state['shms'] = [0, 0, 0, 0]
def _ahb(uc, acc, a, sz, v, ud): state['sh'] = v & 7
u.hook_add(UC_HOOK_MEM_WRITE, _ahb, begin=0x45000010, end=0x45000013)
u.mem_write(0x4500000C, struct.pack('<I', 0x44)); u.mem_write(0x45000008, struct.pack('<I', 0x55)); u.mem_write(0x45000200, struct.pack('<I', 0x80))
state['ihrc'] = 0; state['pw'] = {'full': 0, 'ihrc': 0, 'sleep': 0}
def _clk(uc, acc, a, sz, v, ud):
    state['ihrc'] = (v & 7) == 0; state['clkv'] = v & 7
u.hook_add(UC_HOOK_MEM_WRITE, _clk, begin=0x4500000C, end=0x4500000F)
def _clkrd(uc, acc, a, sz, v, ud):   # the status (bits 6:4) follows the source at once (the hook runs before the store)
    if 'clkv' in state: c = state['clkv']; uc.mem_write(0x4500000C, struct.pack('<I', c | (c << 4)))
u.hook_add(UC_HOOK_MEM_READ, _clkrd, begin=0x4500000C, end=0x4500000F)
rtc0 = int(os.environ.get('RTC0', '5000'))
def _pmu(uc, acc, a, sz, v, ud):
    if v & 2 and not state.get('dsleep'):
        t = state['ms']
        while True:
            t += 1
            if t % 1000 == 0 or t >= total_ms or any(t / 1000.0 >= kt > state['ms'] / 1000.0 for kt, _ in KS): break
        state['dsleep'] = t
        if t % 1000 == 0 and 'g_pw_rtcw' in syms: uc.mem_write(syms['g_pw_rtcw'], b'\x01')
        uc.mem_write(0x45000304, struct.pack('<I', rtc0 + t // 1000))
u.hook_add(UC_HOOK_MEM_WRITE, _pmu, begin=0x45000300, end=0x45000303)
# The load: the ms is run in NCH parts; the part in which the main loop waits (wfi) tells how busy it was.
NCH = int(os.environ.get('NCH', '8'))
state['pw_dirty'] = 0
# The translator copies new code into the PRAM with a small loop in the SRAM (J_cpcode: r0 target, r2 bytes). At
# its end (bx lr) the run stops and what Unicorn had translated from there is dropped.
if 'J_cpcode' in syms:
    _jc = syms['J_cpcode'] & ~1
    def _jc_in(uc, a, sz, ud): state['jcr'] = (uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R2)); state['jit'] = 1
    def _jc_out(uc, a, sz, ud):
        if state.pop('jc_go', 0): return              # resumed right here after the stop
        state['pw_dirty'] = state.get('jcr', (0, 0x10000)); state['jc_go'] = 1; uc.emu_stop()
    u.hook_add(UC_HOOK_CODE, _jc_in, begin=_jc, end=_jc)
    u.hook_add(UC_HOOK_CODE, _jc_out, begin=_jc + 20, end=_jc + 20)
for _v in os.environ.get('SETDATA', '').split(','):   # SETDATA=sym:value,...: an initialised variable changed in the image
    if ':' in _v:
        _n, _x = _v.split(':'); _d = syms[_n] - syms['__data_start'] + syms['__data_load']; u.mem_write(_d, struct.pack('<I', int(_x, 0)))
if os.environ.get('NOJIT'):   # the interpreter only: jit_enabled (data, initial value in the image) = 0
    _d = syms['jit_enabled'] - syms['__data_start'] + syms['__data_load']; u.mem_write(_d, b'\0\0\0\0')
# the main loop waits (wfi): the rest of the ms is idle
_code = bytes(fl[APPOFF:APPOFF + 0x10000]); _end = syms.get('__initcode_start', 0x10000)
def _wfi(uc, a, sz, ud): state['waited'] = 1; uc.emu_stop()
_wfa = [i for i in range(0, _end, 2) if _code[i:i + 2] == b'\x30\xbf']
if not os.environ.get('NOWFI'):
    for _a in _wfa: u.hook_add(UC_HOOK_CODE, _wfi, begin=_a, end=_a)
def shot(name):
    from PIL import Image
    im = Image.new('RGB', (128, 128))
    for y in range(128):
        for x in range(128):
            v = fb[y][x]; im.putpixel((x, y), (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3))
    im.save(os.path.join(out, name))
def st(i): return struct.unpack('<I', u.mem_read(0x18000000 + 4 * i, 4))[0]
sp, rst = struct.unpack('<II', bytes(u.mem_read(0, 8)))
u.reg_write(UC_ARM_REG_SP, sp)
pc = rst; gms = syms['g_ms']
si = 0; LL = int(os.environ.get('LAGLOG', '0')); llp = None
CL = int(os.environ.get('CLOCKLOG', '0')); IOOFF = int(os.environ.get('IOOFF', '0xd98c'), 16)   # Emu.ioram (ARM layout)
PROF = {} if os.environ.get('PROF') else None; PROFFROM = int(os.environ.get('PROFFROM', '0'))   # pc samples (8 per ms) by function
while state['ms'] < total_ms and 'back' not in state:
    u.mem_write(0x45000304, struct.pack('<I', rtc0 + state['ms'] // 1000))
    state['waited'] = 0
    _sh = 3 if state['ihrc'] else state['sh']; n = max(1, RT >> _sh); ch = max(1, n // NCH)
    err = 0; k = 0
    while k < NCH and not state['waited'] and 'back' not in state and not state.get('dsleep'):
        if state['pw_dirty']:
            _a, _n = state["pw_dirty"]; (u.ctl_flush_tb() if os.environ.get("FULLFLUSH") else u.ctl_remove_cache(_a & ~1, _a + _n + 2)); state['pw_dirty'] = 0; state['jcopies'] = state.get('jcopies', 0) + 1
        try: u.emu_start(pc | 1, 0xFFFFFFF0, count=ch)
        except UcError as ex:
            print('error', ex, hex(u.reg_read(UC_ARM_REG_PC))); err = 1; break
        pc = u.reg_read(UC_ARM_REG_PC)
        if PROF is not None and not state['waited'] and (not PROFFROM or state['ms'] >= PROFFROM): PROF[pc] = PROF.get(pc, 0) + 1
        if not state['pw_dirty']: k += 1
    if err: break
    state['shms'][min(_sh, 3)] += 1; state['pw']['ihrc' if state['ihrc'] else 'full'] += 1
    state['busy'] += k; state['busyn'] += 1
    if state['waited']: pc = (pc + 2) & ~1   # past the wfi: the tick comes
    _ds = state.pop('dsleep', 0)
    if _ds:
        n = _ds - state['ms']; state['ms'] = _ds; state['pw']['sleep'] += n
        u.mem_write(0x45000304, struct.pack('<I', rtc0 + state['ms'] // 1000))
        continue
    state['ms'] += 1
    v = struct.unpack('<I', u.mem_read(gms, 4))[0]; u.mem_write(gms, struct.pack('<I', v + 1))
    km = keys(); u.mem_write(0x40018000, struct.pack('<I', 0xFFFFFFFF & ~(((km & 1) << 9) | ((km & 2) << 10) | ((km & 4) << 8) | ((km & 8) << 5))))
    if si < len(SH) and state['ms'] >= SH[si] * 1000: shot('shot_%02d.png' % si); si += 1
    if CL and state['ms'] % CL == 0:
        _io = bytes(u.mem_read(syms['g_emu'] + IOOFF + 0x1910, 0x10)); b = lambda x: (x >> 4) * 10 + (x & 15)
        print('clock', state['ms'], 'guest %02d:%02d:%02d' % (b(_io[8]), b(_io[4]), b(_io[0])), flush=True)
    if LL and state['ms'] % LL == 0:
        cur = (st(9), st(10)); p0 = llp or cur
        print('lag', state['ms'], cur[0] - p0[0], cur[1] - p0[1], 'pw', dict(state['pw']), flush=True); llp = cur
print('emulated ms', st(9), 'dropped ms', st(10), 'busy loop ms', st(12), 'guest pc %x' % st(6), 'mclk', st(4))
print('ms', state['ms'], 'tones', len(state['tone']), 'flash writes', state['flashw'], 'back' if 'back' in state else '', 'pc', hex(pc))
print('ms per clock step (96, 48, 24, 12 MHz):', state['shms'])
print('power ms: full', state['pw']['full'], 'ihrc', state['pw']['ihrc'], 'deep sleep', state['pw']['sleep'])
print('backlight', state['bl'][-6:], 'light %', state['blpct'][-4:])
print('translator copies', state.get('jcopies', 0), ' '.join('%s %d' % (n, struct.unpack('<I', u.mem_read(syms[n], 4))[0]) for n in ('jit_st_flush', 'jit_st_blocks', 'jit_st_idle', 'st_n_evict') if n in syms)); print('load %.1f %% of the CPU (awake ms, in steps of %d %%)' % (100.0 * state['busy'] / max(1, state['busyn'] * NCH), 100 // NCH))
if PROF:
    fs = sorted(((v, k) for k, v in syms.items() if v < 0x20000000), key=lambda t: t[0]); import bisect
    fa = [a for a, _ in fs]; agg = {}
    for a, n in PROF.items():
        i = bisect.bisect_right(fa, a & ~1) - 1; nm = fs[i][1] if i >= 0 else '?'
        if a >= syms.get('__initcode_start', 0x10000) and a < 0x10000: nm = 'JIT code'
        if a >= 0x18000000: nm = 'SRAM code'
        agg[nm] = agg.get(nm, 0) + n
    tot = sum(agg.values())
    if os.environ.get('PROFPC'): print('top pcs', ' '.join('%x:%d' % kv for kv in sorted(PROF.items(), key=lambda kv: -kv[1])[:12]))
    print('profile (%d samples):' % tot, ', '.join('%s %.1f%%' % (k, 100.0 * v / tot) for k, v in sorted(agg.items(), key=lambda kv: -kv[1])[:16]))
open(os.path.join(out, 'flash_after.bin'), 'wb').write(fl)
shot('shot_end.png')
