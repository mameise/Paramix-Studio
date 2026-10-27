# V3 app (v3.bin) in Unicorn, as devt7.py: hardware calls caught, the screen into a picture.
# into a picture, the SysTick is advanced every RT instructions (96 MHz Cortex-M3 ~ RT per ms).
# python3 devv3.py <v3.elf> <image with v3.bin at APPOFF> <ms> [KEYS="t:mask,..."] [SHOTS="t,t,..."] [OUT=dir]
import sys, struct, subprocess, os
from unicorn import *
from unicorn.arm_const import *
elf, img, total_ms = sys.argv[1], sys.argv[2], int(sys.argv[3])
RT = int(os.environ.get('RT', '64000'))          # instructions per millisecond (96 MHz, ~1.5 cycles each)
out = os.environ.get('OUT', '.')
syms = {}
for l in subprocess.run(['llvm-nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
fl = bytearray(open(img, 'rb').read())
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
APPOFF = int(os.environ.get('APPOFF', '0xA0000'), 16)   # program image (1.6.0: more programs elsewhere)
u.mem_map(0, 0x10000); u.mem_write(0, bytes(fl[APPOFF:APPOFF + 0x10000]))
u.mem_map(0x18000000, 0x20000); u.mem_map(0x20000000, 0x1000)
u.mem_map(0x10000000, 0x100000)
u.mem_map(0x60000000, 0x1000000)
if len(fl) < 0x1000000: fl += b'\xff' * (0x1000000 - len(fl))
# FLASHGATE=1: as on the device after the reset, the window shows nothing useful until hw_init
if os.environ.get('FLASHGATE'): u.mem_write(0x60000000, b'\x40' * 0x1000000)
else: u.mem_write(0x60000000, bytes(fl))
u.mem_map(0x40000000, 0x100000); u.mem_map(0x45000000, 0x1000); u.mem_map(0xE000E000, 0x1000); u.mem_map(0xE0001000, 0x1000)   # DWT (CYCCNT reads 0 here)
u.mem_write(0x4000B014, struct.pack('<I', 0x60))   # 1.6.9 T7_LINK: UART1 LSR, sender empty (nothing received)
cfg = APPOFF + 0x140
rom_off = struct.unpack_from('<I', fl, cfg + 8)[0]
u.mem_write(0x10000000, bytes(fl[rom_off:rom_off + 0x100000]))
fb = [[0] * 128 for _ in range(128)]
rect = {'x0': 0, 'x1': 0, 'y': 0, 'x': 0}
KS = []
for p in os.environ.get('KEYS', '').split(','):
    if ':' in p: t, m = p.split(':'); KS.append((float(t), int(m)))
SH = [float(x) for x in os.environ.get('SHOTS', '').split(',') if x]
state = {'ms': 0, 'tone': [], 'left': 0, 'flashw': 0}
def keys():
    m = 0
    for t, v in KS:
        if state['ms'] / 1000.0 >= t: m = v
    return m
def ret(uc, v=None):
    if v is not None: uc.reg_write(UC_ARM_REG_R0, v)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)
def h_begin(uc, *a):
    x0, y0, x1, y1 = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
    rect.update(x0=x0, x1=x1, y=y0, x=x0); ret(uc)
def h_row(uc, *a):
    p, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    d = struct.unpack('<%dH' % n, uc.mem_read(p, 2 * n))
    for v in d:
        if rect['y'] < 128 and rect['x'] < 128: fb[rect['y']][rect['x']] = v
        rect['x'] += 1
        if rect['x'] >= rect['x1']: rect['x'] = rect['x0']; rect['y'] += 1
    ret(uc)
def h_tone(uc, *a): state['tone'].append((state['ms'], uc.reg_read(UC_ARM_REG_R0))); ret(uc)
def h_btn(uc, *a): ret(uc, keys())
def h_prog(uc, *a):
    ad, src = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    d = bytes(uc.mem_read(src, 0x1000)); a = ad & 0xFFFFFF
    uc.mem_write(0x60000000 + a, d); fl[a:a + 0x1000] = d; state['flashw'] += 1; ret(uc)
def h_erase(uc, *a):
    a = uc.reg_read(UC_ARM_REG_R0) & 0xFFFFFF
    uc.mem_write(0x60000000 + a, b'\xff' * 0x1000); fl[a:a + 0x1000] = b'\xff' * 0x1000; ret(uc)
def h_back(uc, *a): state['back'] = 1; uc.emu_stop()
def h_nop(uc, *a): ret(uc)
def h_init(uc, *a):
    if os.environ.get('FLASHGATE') and not state.get('gate'): uc.mem_write(0x60000000, bytes(fl)); state['gate'] = 1
    ret(uc)
H = {'hw_px_begin': h_begin, 'hw_px_row': h_row, 'hw_px_end': h_nop, 'hw_tone': h_tone, 'hw_buttons': h_btn,
     'flash_prog4k': h_prog, 'flash_erase4k': h_erase, 'hw_back_to_paradise': h_back, 'hw_init': h_init,
     'hw_backlight': h_nop, 'hw_bl_pwm_off': h_nop, 'hw_wdt_feed': h_nop}
for n, f in H.items():
    a = syms[n] & ~1
    u.hook_add(UC_HOOK_CODE, (lambda f: lambda uc, ad, sz, ud: f(uc))(f), begin=a, end=a)
def bad(uc, access, addr, size, value, ud):
    print('unmapped', access, hex(addr), 'pc', hex(uc.reg_read(UC_ARM_REG_PC))); return False
u.hook_add(UC_HOOK_MEM_UNMAPPED, bad)
# 1.6.9 test22 CLK_ECO: the AHB prescaler at 0x45000010 slows the core: RT >> sh instructions per ms
state['sh'] = 0; state['shms'] = [0, 0, 0, 0]
def _ahb(uc, acc, a, sz, v, ud):
    state['sh'] = v & 7
    if os.environ.get('ECOLOG'): print('AHB', v & 7, 'at ms', state['ms'])
u.hook_add(UC_HOOK_MEM_WRITE, _ahb, begin=0x45000010, end=0x45000013)
if os.environ.get('MEMW'):
    _mw = int(os.environ['MEMW'], 16)
    u.hook_add(UC_HOOK_MEM_WRITE, lambda uc, acc, a, sz, v, ud: print('W %08x = %08x at ms %d' % (a, v, state['ms'])), begin=_mw, end=_mw + 0x3F)
# 1.6.13 test12 (V3_PW, as devt7/devemu): clock source (0x4500000C), deep sleep (PMU_CTRL bit 1, then WFI): the time
# runs on to the next RTC second or a key change; the load: the ms in NCH parts, the part with the main loop's wfi
u.mem_write(0x45000008, struct.pack('<I', 0x55)); u.mem_write(0x45000200, struct.pack('<I', 0x80)); u.mem_write(0x4500000C, struct.pack('<I', 0x44))
state['ihrc'] = 0; state['pw'] = {'full': 0, 'ihrc': 0, 'sleep': 0}; state['busy'] = 0; state['busyn'] = 0
def _clk(uc, acc, a, sz, v, ud): state['ihrc'] = (v & 7) == 0; state['clkv'] = v & 7
u.hook_add(UC_HOOK_MEM_WRITE, _clk, begin=0x4500000C, end=0x4500000F)
def _clkrd(uc, acc, a, sz, v, ud):
    if 'clkv' in state: c = state['clkv']; uc.mem_write(0x4500000C, struct.pack('<I', c | (c << 4)))
u.hook_add(UC_HOOK_MEM_READ, _clkrd, begin=0x4500000C, end=0x4500000F)
def _pmu(uc, acc, a, sz, v, ud):
    if v & 2 and not state.get('dsleep'):
        t = state['ms']
        while True:
            t += 1
            if t % 1000 == 0 or t >= total_ms or any(t / 1000.0 >= kt > state['ms'] / 1000.0 for kt, _ in KS): break
        state['dsleep'] = t
        if t % 1000 == 0 and 'g_pw_rtcw' in syms: uc.mem_write(syms['g_pw_rtcw'], b'\x01')
        uc.mem_write(0x45000304, struct.pack('<I', int(os.environ.get('RTC0', '5000')) + t // 1000))
u.hook_add(UC_HOOK_MEM_WRITE, _pmu, begin=0x45000300, end=0x45000303)
NCH = int(os.environ.get('NCH', '8'))
_code = bytes(fl[APPOFF:APPOFF + 0x10000]); _end = syms.get('__prog_end', 0x10000)
def _wfi(uc, a, sz, ud): state['waited'] = 1; uc.emu_stop()
for _a in [i for i in range(0, _end, 2) if _code[i:i + 2] == b'\x30\xbf']: u.hook_add(UC_HOOK_CODE, _wfi, begin=_a, end=_a)
PROF = {} if os.environ.get('PROF') else None
def shot(name):
    from PIL import Image
    im = Image.new('RGB', (128, 128))
    for y in range(128):
        for x in range(128):
            v = fb[y][x]; im.putpixel((x, y), (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3))
    im.save(os.path.join(out, name))
if os.environ.get('PMB'):   # test: the Paradise's time in the mailbox (seconds since 1970, local)
    _t = int(os.environ['PMB']); _c = int(os.environ.get('RTC0', '5000'))
    u.mem_write(0x20000FC0, struct.pack('<IIII', 0x4B4C4350, _t, _c, (~_t) & 0xFFFFFFFF))
sp, rst = struct.unpack('<II', bytes(u.mem_read(0, 8)))
u.reg_write(UC_ARM_REG_SP, sp)
pc = rst; gms = syms['g_ms']; rtc0 = int(os.environ.get('RTC0', '5000'))
emu_ms_sym = None
si = 0
while state['ms'] < total_ms and 'back' not in state:
    u.mem_write(0x45000304, struct.pack('<I', rtc0 + state['ms'] // 1000))
    state['waited'] = 0; _sh = 3 if state['ihrc'] else state['sh']; _n = max(1, RT >> _sh); _ch = max(1, _n // NCH); _k = 0; _err = 0
    while _k < NCH and not state['waited'] and 'back' not in state and not state.get('dsleep'):
        try: u.emu_start(pc | 1, 0xFFFFFFF0, count=_ch)
        except UcError as ex:
            print('error', ex, hex(u.reg_read(UC_ARM_REG_PC))); _err = 1; break
        pc = u.reg_read(UC_ARM_REG_PC); _k += 1
        if PROF is not None and not state['waited']: PROF[pc] = PROF.get(pc, 0) + 1
    if _err: break
    state['shms'][min(_sh, 3)] += 1; state['pw']['ihrc' if state['ihrc'] else 'full'] += 1; state['busy'] += _k; state['busyn'] += 1
    if state['waited']: pc = (pc + 2) & ~1
    _ds = state.pop('dsleep', 0)
    if _ds:
        state['pw']['sleep'] += _ds - state['ms']; state['ms'] = _ds
        u.mem_write(0x45000304, struct.pack('<I', rtc0 + state['ms'] // 1000)); continue
    state['ms'] += 1
    v = struct.unpack('<I', u.mem_read(gms, 4))[0]; u.mem_write(gms, struct.pack('<I', v + 1))
    # 1.6.9 test13 T7_KEYQ: the GPIO as the keys say (pressed = 0) and the tick's press counter
    km = keys(); u.mem_write(0x40018000, struct.pack('<I', 0xFFFFFFFF & ~(((km & 1) << 9) | ((km & 2) << 10) | ((km & 4) << 8) | ((km & 8) << 5))))
    if 'g_kin' in syms:
        e = km & ~state.get('kprev', 0) & 7; state['kprev'] = km
        u.mem_write(syms['g_kprev'], bytes([km & 7]))
        for i in range(3):
            if e & (1 << i): a = syms['g_kin'] + i; u.mem_write(a, bytes([(u.mem_read(a, 1)[0] + 1) & 0xFF]))
    for pk in os.environ.get('POKE', '').split(','):   # test: 'ms:symbol+off:byte' written once
        if pk.count(':') == 2 and int(pk.split(':')[0]) == state['ms']:
            _t, _a, _v = pk.split(':'); _n, _o = (_a.split('+') + ['0'])[:2]; u.mem_write(syms[_n] + int(_o, 0), bytes([int(_v, 0)]))
    if os.environ.get('DROPLOG') and 'g_v3st' in syms:
        _d = struct.unpack('<I', u.mem_read(syms['g_v3st'] + 4, 4))[0]
        if _d != state.get('drop', 0): print('drop', _d - state.get('drop', 0), 'at ms', state['ms'], 'sh', state['sh']); state['drop'] = _d
    if si < len(SH) and state['ms'] >= SH[si] * 1000: shot('shot_%02d.png' % si); si += 1
st = struct.unpack('<2I', bytes(u.mem_read(syms['g_v3st'], 8))) if 'g_v3st' in syms else (0, 0)   # V1Emu has no counters
print('emulated ms', st[0], 'dropped ms', st[1])
(os.environ.get('TONES') and print('tones:', state['tone']));print('ms', state['ms'], 'tones', len(state['tone']), 'flash writes', state['flashw'], 'back' if 'back' in state else '', 'pc', hex(pc))
print('ms per clock step (96, 48, 24, 12 MHz):', state['shms'])
print('power ms: full', state['pw']['full'], 'ihrc', state['pw']['ihrc'], 'deep sleep', state['pw']['sleep'])
print('load %.1f %% of the CPU (awake ms)' % (100.0 * state['busy'] / max(1, state['busyn'] * NCH)))
if PROF:
    import bisect
    fs = sorted(((v, k) for k, v in syms.items() if v < 0x20000000), key=lambda t: t[0]); fa = [a for a, _ in fs]; agg = {}
    for a, n in PROF.items():
        i = bisect.bisect_right(fa, a & ~1) - 1; nm = fs[i][1] if i >= 0 else '?'
        if a >= 0x18000000: nm = 'SRAM code'
        agg[nm] = agg.get(nm, 0) + n
    tot = sum(agg.values()); print('profile:', ', '.join('%s %.1f%%' % (k, 100.0 * v / tot) for k, v in sorted(agg.items(), key=lambda kv: -kv[1])[:16]))
open(os.path.join(out, 'flash_after.bin'), 'wb').write(fl)
shot('shot_end.png')
for _d in os.environ.get('DUMP', '').split(','):   # test: 'symbol+off:len' printed at the end
    if ':' in _d:
        _a, _l = _d.split(':'); _n, _o = (_a.split('+') + ['0'])[:2]
        print('DUMP', _d, bytes(u.mem_read(syms[_n] + int(_o, 0), int(_l, 0))).hex())
