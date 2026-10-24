# SMSEmu (sms.bin) in Unicorn, as devt7.py: hardware calls caught, the screen into a picture.
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
u.mem_map(0x60000000, 0x1000000); u.mem_write(0x60000000, bytes(fl))
u.mem_map(0x40000000, 0x100000); u.mem_map(0x45000000, 0x1000); u.mem_map(0xE000E000, 0x1000); u.mem_map(0xE0001000, 0x1000)   # DWT (CYCCNT reads 0 here)
u.mem_write(0x4000B014, struct.pack('<I', 0x60))   # 1.6.9 T7_LINK: UART1 LSR, sender empty (nothing received)
cfg = APPOFF + 0x140
rom_off = struct.unpack_from('<I', fl, cfg + 8)[0]
u.mem_write(0x10000000, bytes(fl[rom_off:rom_off + 0x100000]))
WH = []   # WHEEL='t:steps,...' (+ one way, - the other): the encoder phases at GPIO2
for p in os.environ.get('WHEEL', '').split(','):
    if ':' in p: t, n = p.split(':'); WH.append([float(t), int(n)])
state_enc = {'ph': 0, 'q': []}
GRAY = [0, 1, 3, 2]
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
US_PX = float(os.environ.get('US_PX', '2.0'))
state['spi'] = 0.0; state['px'] = 0
def h_row(uc, *a):
    p, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    state['spi'] += n * US_PX / 1000.0; state['px'] += n
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
H = {'hw_px_begin': h_begin, 'hw_px_row': h_row, 'hw_px_end': h_nop, 'hw_tone': h_tone, 'hw_buttons': h_btn,
     'flash_prog4k': h_prog, 'flash_erase4k': h_erase, 'hw_back_to_paradise': h_back, 'hw_init': h_nop,
     'hw_backlight': h_nop, 'hw_bl_pwm_off': h_nop, 'hw_wdt_feed': h_nop}
for n, f in H.items():
    a = syms[n] & ~1
    u.hook_add(UC_HOOK_CODE, (lambda f: lambda uc, ad, sz, ud: f(uc))(f), begin=a, end=a)
import builtins
if hasattr(builtins, '__gbprof'): u.hook_add(UC_HOOK_BLOCK, builtins.__gbprof[0])
if os.environ.get('CACHESIM'):   # test38: the I-cache of the flash window (16 KB, 32-byte lines, WAYS ways, LRU): misses per frame
    _ways = int(os.environ.get('WAYS', '4')); _sets = 16384 // 32 // _ways
    _cs = [[] for _ in range(_sets)]; state['miss'] = 0; state['acc'] = 0
    def _cr(uc, acc, a, sz, v, ud):
        state['acc'] += 1
        ln = (a - 0x10000000) >> 5; s = _cs[ln % _sets]
        if ln in s: s.remove(ln); s.append(ln); return
        state['miss'] += 1; s.append(ln)
        if len(s) > _ways: s.pop(0)
    u.hook_add(UC_HOOK_MEM_READ, _cr, begin=0x10000000, end=0x100FFFFF)
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
    try:
        u.emu_start(pc | 1, 0xFFFFFFF0, count=max(1, RT >> state['sh'])); state['shms'][min(state['sh'], 3)] += 1
    except UcError as ex:
        print('error', ex, hex(u.reg_read(UC_ARM_REG_PC))); break
    pc = u.reg_read(UC_ARM_REG_PC)
    state['ms'] += 1
    v = struct.unpack('<I', u.mem_read(gms, 4))[0]; u.mem_write(gms, struct.pack('<I', v + 1))
    while state['spi'] >= 1.0:
        state['spi'] -= 1.0; state['ms'] += 1; v += 1; u.mem_write(gms, struct.pack('<I', v + 1))
    # 1.6.9 test13 T7_KEYQ: the GPIO as the keys say (pressed = 0) and the tick's press counter
    km = keys(); u.mem_write(0x40018000, struct.pack('<I', 0xFFFFFFFF & ~(((km & 1) << 9) | ((km & 2) << 10) | ((km & 4) << 8) | ((km & 8) << 5))))
    if 'g_kin' in syms:
        e = km & ~state.get('kprev', 0) & 7; state['kprev'] = km
        u.mem_write(syms['g_kprev'], bytes([km & 7]))
        for i in range(3):
            if e & (1 << i): a = syms['g_kin'] + i; u.mem_write(a, bytes([(u.mem_read(a, 1)[0] + 1) & 0xFF]))
    for w in WH:
        if w[1] and state['ms'] / 1000.0 >= w[0]:
            d = 1 if w[1] > 0 else -1; state_enc['ph'] = (state_enc['ph'] + d) % 4; w[0] += 0.004 if (state_enc['ph'] % 4) else 0.02
            if state_enc['ph'] == 0: w[1] -= d
    u.mem_write(0x4001A000, struct.pack('<I', GRAY[state_enc['ph']]))
    for pk in os.environ.get('POKE', '').split(','):   # test: 'ms:symbol+off:byte' written once
        if pk.count(':') == 2 and int(pk.split(':')[0]) == state['ms']:
            _t, _a, _v = pk.split(':'); _n, _o = (_a.split('+') + ['0'])[:2]; u.mem_write(syms[_n] + int(_o, 0), bytes([int(_v, 0)]))
    if os.environ.get('DROPLOG') and 'g_v3st' in syms:
        _d = struct.unpack('<I', u.mem_read(syms['g_v3st'] + 4, 4))[0]
        if _d != state.get('drop', 0): print('drop', _d - state.get('drop', 0), 'at ms', state['ms'], 'sh', state['sh']); state['drop'] = _d
    if si < len(SH) and state['ms'] >= SH[si] * 1000: shot('shot_%02d.png' % si); si += 1
    if 'miss' in state and state['ms'] % 1000 == 0:
        _f = struct.unpack('<I', u.mem_read(syms['g_smsst'], 4))[0]; _df = max(1, _f - state.get('f0', 0))
        print('s %2d frames %3d misses/frame %6.0f reads/frame %7.0f' % (state['ms'] // 1000, _df, state['miss'] / _df, state['acc'] / _df))
        state['f0'] = _f; state['miss'] = 0; state['acc'] = 0
st = struct.unpack('<4I', bytes(u.mem_read(syms['g_smsst'], 16)))
print('pixels sent', state['px']); print('frames run', st[0], 'drawn', st[1], 'dropped', st[2], 'last frame ms', st[3])
(os.environ.get('TONES') and print('tones:', state['tone']));print('ms', state['ms'], 'tones', len(state['tone']), 'flash writes', state['flashw'], 'back' if 'back' in state else '', 'pc', hex(pc))
print('ms per clock step (96, 48, 24, 12 MHz):', state['shms'])
open(os.path.join(out, 'flash_after.bin'), 'wb').write(fl)
shot('shot_end.png')
for _d in os.environ.get('DUMP', '').split(','):   # test: 'symbol+off:len' printed at the end
    if ':' in _d:
        _a, _l = _d.split(':'); _n, _o = (_a.split('+') + ['0'])[:2]
        print('DUMP', _d, bytes(u.mem_read(syms[_n] + int(_o, 0), int(_l, 0))).hex())
