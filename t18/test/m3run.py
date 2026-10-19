import zlib
import os
# Drive the T18 core built for Cortex-M3 (m3main.c) in Unicorn with a script like host/ref.c.
# python3 m3run.py <elf> <fw.bin> <script> [K]   prints per command host instructions; hash lines like ref
import sys, collections, struct, subprocess
from unicorn import *
from unicorn.arm_const import *
elf, fw, script = sys.argv[1], sys.argv[2], sys.argv[3]
K = int(sys.argv[4]) if len(sys.argv) > 4 else 200000
syms = {}
for l in subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
# as on the Paradise: PRAM 64 KB, flash window, SRAM 128 KB, RAM at 0x20000000 only 4 KB
u.mem_map(0, int(os.environ.get('PRAMSZ', '0x10000'), 16)); u.mem_map(0x10000000, 0x100000); u.mem_map(0x18000000, 0x20000); u.mem_map(0x20000000, int(os.environ.get('RAM20', '0x1000'), 16))
u.mem_map(0x60000000, 0x1000000); u.mem_write(0x60000000, open(fw, 'rb').read()[:0x800000].ljust(0x800000, b'\xff'))
subprocess.run(['arm-none-eabi-objcopy', '-O', 'binary', '-j', '.text', '-j', '.data', elf, elf + '.bin'], check=True)
subprocess.run(['arm-none-eabi-objcopy', '-O', 'binary', '-j', '.xip', elf, elf + '.xip'], check=True)
img = open(elf + '.bin', 'rb').read()
u.mem_write(0, img)
u.mem_write(0x100DF000, open(elf + '.xip', 'rb').read())
UNC = [0]
LASTC = [(0, 0, 0, 0)]
if os.environ.get('UNCOUNT', '1') == '1':
    UNB = collections.Counter() if os.environ.get('UNBLK') else None
    UNPC = collections.Counter() if os.environ.get('UNCPC') else None
    def _unc(uc, acc, addr, size, val, ud):
        UNC[0] += 1
        if UNB is not None: UNB[(addr >> 16) & 0xFF] += 1
        if UNPC is not None: UNPC[(uc.reg_read(UC_ARM_REG_PC), size)] += 1
    u.hook_add(UC_HOOK_MEM_READ, _unc, begin=0x60000000, end=0x60FFFFFF)
PRAM_BAD = [0]
def pram_wr(uc, acc, addr, size, val, ud):
    pc = uc.reg_read(UC_ARM_REG_PC)
    if not (0x10000000 <= pc < 0x10100000 or 0x1800E600 <= pc < 0x18014400) or size not in (2, 4):
        PRAM_BAD[0] += 1
        if PRAM_BAD[0] < 5: print('PRAM written from pc', hex(pc), 'size', size, 'at', hex(addr))
if os.environ.get('PRAMCHECK', '1') == '1': u.hook_add(UC_HOOK_MEM_WRITE, pram_wr, begin=0, end=0xFFFF)
# Unicorn keeps old translations of code buffer addresses written by the program: drop them on each write
if 't18_pram_w16' in syms:
    def _pw(uc, ad, sz, ud):
        a = uc.reg_read(UC_ARM_REG_R0) & ~1
        uc.ctl_remove_cache(a & ~0x3FF, (a & ~0x3FF) + 0x800)
    u.hook_add(UC_HOOK_CODE, _pw, begin=syms['t18_pram_w16'] & ~1, end=syms['t18_pram_w16'] & ~1)

def bad(uc, acc, addr, size, val, ud):
    print('bad access', hex(addr), 'pc', hex(uc.reg_read(UC_ARM_REG_PC)), 'lr', hex(uc.reg_read(UC_ARM_REG_LR)), 'sp', hex(uc.reg_read(UC_ARM_REG_SP))); return False
u.hook_add(UC_HOOK_MEM_INVALID, bad)
MB = syms['g_mb']
sp, entry = struct.unpack_from('<II', img, 0)
u.reg_write(UC_ARM_REG_SP, sp); pc = entry | 1
total = 0; seq = 0
def rd(i): return struct.unpack('<I', u.mem_read(MB + 4 * i, 4))[0]
def wr(i, v): u.mem_write(MB + 4 * i, struct.pack('<I', v & 0xFFFFFFFF))
import collections, os
TR = collections.deque(maxlen=40)
if os.environ.get('DEBUG'):
    u.hook_add(UC_HOOK_BLOCK, lambda uc, a, sz, ud: TR.append(a))
def crash(e):
    import capstone
    print('CRASH', e)
    for r, nm in [(UC_ARM_REG_R0,'r0'),(UC_ARM_REG_R1,'r1'),(UC_ARM_REG_R2,'r2'),(UC_ARM_REG_R3,'r3'),(UC_ARM_REG_R4,'r4'),(UC_ARM_REG_R5,'r5'),(UC_ARM_REG_R6,'r6'),(UC_ARM_REG_R7,'r7'),(UC_ARM_REG_R8,'r8'),(UC_ARM_REG_R9,'r9'),(UC_ARM_REG_R10,'r10'),(UC_ARM_REG_R11,'r11'),(UC_ARM_REG_R12,'r12'),(UC_ARM_REG_SP,'sp'),(UC_ARM_REG_LR,'lr'),(UC_ARM_REG_PC,'pc')]:
        print(nm, hex(u.reg_read(r)), end='  ')
    print()
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
    jc = syms.get('JC', syms.get('__jc_start', 0))
    for a in list(TR)[-12:]:
        print('block', hex(a), '(JC+%x)' % (a - jc) if jc <= a < jc + 0x10000 else '')
    last = [a for a in TR if jc <= a < jc + 0x10000]
    if len(last) > 1 and os.environ.get('DEBUG2'):
        a = last[-2] & ~1
        for i in md.disasm(bytes(u.mem_read(a, 400)), a): print('  p', hex(i.address), i.mnemonic, i.op_str)
        if 't18_hdbg' in syms: print('hdbg', ['%x' % x for x in struct.unpack('<8I', u.mem_read(syms['t18_hdbg'], 32))])
        jf = syms.get('j_fill')
        if jf: print('j_fill', ['%x' % x for x in struct.unpack('<2I', u.mem_read(jf, 8))], 'J', '%x' % struct.unpack('<I', u.mem_read(syms['J'], 4))[0], 'half', struct.unpack('<I', u.mem_read(syms['j_half'], 4))[0] if 'j_half' in syms else '?')
    if last:
        a = last[-1] & ~1
        code = bytes(u.mem_read(a, 160))
        for i in md.disasm(code, a): print('   ', hex(i.address), i.mnemonic, i.op_str)
    gpc = struct.unpack('<16I', u.mem_read(syms['g_t'], 64))
    print('guest regs', ' '.join('%08x' % x for x in gpc))
    sys.exit(1)
def vfail(uc, intno, ud):
    if 'g_vf' not in syms: return
    v = struct.unpack('<80I', uc.mem_read(syms['g_vf'], 320))
    if v[0] != 0xBAD: return
    import capstone
    key = v[1]
    print('VERIFY FAIL block key %08x' % key)
    for nm, o in (('before', 2), ('jit   ', 18), ('interp', 34)): print(nm, ' '.join('%08x' % x for x in v[o:o + 16]))
    print('cpsr', ' '.join('%08x' % x for x in v[50:53]), 'cycles jit %d interp %d start %d' % (v[53], v[54], v[55]))
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if key & 1 else capstone.CS_MODE_ARM)
    a = key & ~1
    src = bytes(uc.mem_read(0x18000000 + (a - 0xF8000000), 200)) if a >= 0xF8000000 else bytes(uc.mem_read(0x60000000 + (a - 0x20000000), 200))
    for i in list(md.disasm(src, a))[:40]: print('   ', hex(i.address), i.mnemonic, i.op_str)
    sys.exit(2)
u.hook_add(UC_HOOK_INTR, vfail)
FNCNT = collections.Counter()
if os.environ.get('FNCOUNT'):
    for nm in os.environ['FNCOUNT'].split(','):
        def _fc(uc, a, sz, ud, nm=nm): FNCNT[nm] += 1
        u.hook_add(UC_HOOK_CODE, _fc, begin=syms[nm] & ~1, end=syms[nm] & ~1)
KSEQ = [] if os.environ.get('KSEQ') else None
if KSEQ is not None:
    def _ks(uc, a, sz, ud):
        if len(KSEQ) < 3000000: KSEQ.append(uc.reg_read(UC_ARM_REG_R11))
    u.hook_add(UC_HOOK_CODE, _ks, begin=syms['j_ind_key'] & ~1, end=syms['j_ind_key'] & ~1)
CHKR = collections.Counter() if os.environ.get('CHKREG') else None
CHKK = collections.Counter()
if CHKR is not None:
    def _cr(uc, a, sz, nm):
        r11 = uc.reg_read(UC_ARM_REG_R11); lr = uc.reg_read(UC_ARM_REG_LR)
        info = struct.unpack('<H', bytes(uc.mem_read((lr & ~1), 2)))[0]
        o = (r11 - 0xF8000000) & 0xFFFFFFFF
        reg = 'sram' if o < 0x20000 and not (0xE600 <= o < 0x14400) else 'hole' if o < 0x20000 else 'flash' if 0x20000000 <= r11 < 0x20800000 else 'io%x' % (r11 >> 20)
        CHKR[(nm, reg, 'L' if info & 0x80 else 'S')] += 1
        if os.environ.get('CHKKEY'): k = struct.unpack('<HH', bytes(uc.mem_read((lr & ~1) + 4, 4))); CHKK[k[0] | k[1] << 16] += 1
    for nm in ('j_chk0','j_chk1','j_chk2','j_chkf0','j_chkf1','j_chkf2'):
        u.hook_add(UC_HOOK_CODE, _cr, nm, begin=syms[nm] & ~1, end=syms[nm] & ~1)
JSH = collections.Counter() if os.environ.get('JSLOWH') else None
if JSH is not None:
    def _js(uc, a, sz, ud):
        r11 = uc.reg_read(UC_ARM_REG_R11); lr = uc.reg_read(UC_ARM_REG_LR)
        info = struct.unpack('<I', bytes(uc.mem_read((lr & ~1) - 0 + 0, 4)))[0] if False else 0
        JSH[(r11 & ~0xFF)] += 1
    u.hook_add(UC_HOOK_CODE, _js, begin=syms['j_slow'] & ~1, end=syms['j_slow'] & ~1)
if os.environ.get('JTRACE'):
    _tf = open(os.environ['JTRACE'], 'w'); _tend = int(os.environ.get('JTEND', '400000'))
    def _jt(uc, a, sz, ud):
        c = uc.reg_read(UC_ARM_REG_R0)
        v = struct.unpack('<17I', uc.mem_read(c, 68)); cy = struct.unpack('<Q', uc.mem_read(c + 184, 8))[0]
        if int(os.environ.get('JTBEG','0')) <= cy <= _tend: _tf.write('%d %s %x %x\n' % (cy, ' '.join('%x' % x for x in v[:16]), v[16] & 0xF000003F, zlib.crc32(bytes(uc.mem_read(0x18000000, 0xE600)))))
    a = syms['t18_jit_try'] & ~1
    u.hook_add(UC_HOOK_CODE, _jt, begin=a, end=a)
    if os.environ.get('JEV'):
        _cpu = [0]
        def _ev(uc, a, sz, nm):
            c = uc.reg_read(UC_ARM_REG_R0) if nm in ('j_enter', 'arm7_step') else 0
            cpu = c or _cpu[0]
            if c: _cpu[0] = c
            if not cpu: return
            cy = struct.unpack('<Q', uc.mem_read(cpu + 184, 8))[0]
            if int(os.environ.get('JTBEG','0')) <= cy <= _tend:
                pcv, cps = struct.unpack('<II', uc.mem_read(cpu + 60, 8))
                _tf.write('  ev %s r0=%x r1=%x r11=%x lr=%x cyc %d gpc %x cpsr %x\n' % (nm, uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R11), uc.reg_read(UC_ARM_REG_LR), cy, pcv, cps))
        for nm in ('j_enter', 't18_jit_lookup', 't18_jit_link', 'arm7_step', 't18_jh_slow', 't18_jit_exit', 'j_indbx', 'j_ind'):
            if nm in syms:
                b = syms[nm] & ~1
                u.hook_add(UC_HOOK_CODE, _ev, nm, begin=b, end=b)
u.mem_map(0x30000000, 0x80000)
EXACT = [0]
EXON = [False]
def _cnt(uc, a, sz, ud): EXACT[0] += 1
PSAMP = {} if os.environ.get('PSAMP') else None
_fsyms = sorted((v & ~1, k) for k, v in syms.items() if not k.startswith('__') and k not in ('JC',))
import bisect
def _fname(a):
    if syms.get('__jc_start', 0) <= a < syms.get('__jc_end', 0): return '<JIT code>'
    i = bisect.bisect_right(_fsyms, (a, '~')) - 1
    return _fsyms[i][1] if i >= 0 else '?'
def psamp_report():
    if PSAMP is None: return
    agg = {}
    for a, c in PSAMP.items(): f = _fname(a); agg[f] = agg.get(f, 0) + c
    tot = sum(agg.values()) or 1
    print('   profile: ' + ', '.join('%s %d%%' % (f, c * 100 // tot) for f, c in sorted(agg.items(), key=lambda x: -x[1])[:14]))
    PSAMP.clear()
TUS = (syms['__jc_start'], syms['__jc_end'], syms['t18_tu_pc']) if 't18_tu_pc' in syms and '__jc_end' in syms else None
if os.environ.get('TUK'): K = int(os.environ['TUK'])
def run_until_idle():
    global pc, total
    n = 0; e0 = EXACT[0]
    while True:
        try: u.emu_start(pc, 0xFFFFFFFF, count=K)
        except UcError as e: crash(e)
        pc = u.reg_read(UC_ARM_REG_PC) | 1; n += K
        if TUS is not None and TUS[0] <= (pc & ~1) < TUS[1]: u.mem_write(TUS[2], struct.pack('<I', pc))
        if PSAMP is not None: PSAMP[pc & ~1] = PSAMP.get(pc & ~1, 0) + 1
        # idle when spinning and spin counter grows
        if u.reg_read(UC_ARM_REG_PC) & ~1 in spin_pcs and u.reg_read(UC_ARM_REG_R3) > 1000:
            spins = u.reg_read(UC_ARM_REG_R3)
            ex = n - spins * 4
            if EXON[0]: print('   exact %d (sliced %d)' % (EXACT[0] - e0 - spins * 4, ex))
            total += ex
            return ex
spin_pcs = set(range(syms['mb_spin'] & ~1, (syms['mb_spin'] & ~1) + 12, 2))
def cmd(c, *a):
    global seq
    wr(0, c)
    for i, v in enumerate(a): wr(1 + i, v)
    seq += 1; wr(9, seq)
    u.reg_write(UC_ARM_REG_R3, 0)
    return run_until_idle()
run_until_idle()   # boot to the first wait
ms_total = 0; host_total = 0
for l in open(script):
    p = l.split()
    if not p: continue
    if p[0] == 'wait':
        ms = float(p[1]); ex = cmd(1, int(ms * 1000)); ms_total += ms; host_total += ex
        if os.environ.get('IDLELOG'): print(' '.join('%x' % x for x in struct.unpack('<64I', bytes(u.mem_read(syms['t18_idle_log'], 256)))))
        psamp_report()
        if CHKR is not None: print('   chk: ' + ', '.join('%s/%s/%s %d' % (k[0],k[1],k[2],v) for k, v in CHKR.most_common(14))); CHKR.clear()
        if os.environ.get('CHKKEY'): print('   keys: ' + ' '.join('%08x:%d' % kv for kv in CHKK.most_common(24))); CHKK.clear()
        if 't18_kstat' in syms:
            kb = struct.unpack('<40I', bytes(u.mem_read(syms['t18_kstat'], 160)))
            nm = ['COPY','HI','LDRPC','MEM','SPMEM','ADDPC','ADDSP','SPADJ','PUSH','POP','LDM','STM','BCC','B','BL','?','-','?','begin','end+exits']
            print('   kstat: ' + ' '.join('%s %d/%d=%.1f' % (nm[i], kb[i]*2, kb[20+i], kb[i]*2/max(kb[20+i],1)) for i in range(20) if kb[20+i]))
        if 't18_slow_hist' in syms:
            h = struct.unpack('<128I', bytes(u.mem_read(syms['t18_slow_hist'], 512)))
            top = sorted([(h[2*i+1], h[2*i]) for i in range(64) if h[2*i]], reverse=True)[:12]
            print('   slow: ' + ' '.join('%08x%s:%d' % (k & ~1, 'R' if k & 1 else 'W', c) for c, k in top))
            u.mem_write(syms['t18_slow_hist'], bytes(512))
        cy, ic = struct.unpack('<QQ', bytes(u.mem_read(syms['g_t'] + 184, 16))); isk = struct.unpack('<Q', bytes(u.mem_read(syms['g_t'] + 288, 8)))[0]
        ti = struct.unpack('<2I', bytes(u.mem_read(syms['t18_interp'], 8))) if 't18_interp' in syms else (0, 0)
        L = LASTC[0]; LASTC[0] = (cy, ic, isk, ti[0] + ti[1])
        print('   guest cycles %d M, idle %d M, skipped %d M, interpreted %d K' % ((cy - L[0]) // 1000000, (ic - L[1]) // 1000000, (isk - L[2]) // 1000000, (ti[0] + ti[1] - L[3]) // 1000))
        if os.environ.get('SYMDUMP'):
            for it in os.environ['SYMDUMP'].split(','):
                nm, n = it.split(':')
                if nm in syms:
                    w = struct.unpack('<%dI' % int(n), bytes(u.mem_read(syms[nm], 4 * int(n)))); print('   ' + nm, ' '.join(str(x) for x in w)); u.mem_write(syms[nm], bytes(4 * int(n)))
        unc = UNC[0]; UNC[0] = 0
        if os.environ.get('UNCPC') and os.environ.get('UNCOUNT', '1') == '1':
            agg = collections.Counter()
            for (a, sz), c in UNPC.items(): agg[(_fname(a & ~1), sz)] += c
            print('   unc by fn: ' + ' '.join('%s/%d:%d' % (k[0], k[1], v) for k, v in agg.most_common(12))); UNPC.clear()
        if os.environ.get('UNBLK') and os.environ.get('UNCOUNT', '1') == '1':
            print('   blocks ' + ' '.join('%02x:%d' % kv for kv in UNB.most_common(30))); UNB.clear()
        print('   uncached reads %d  (+%.1f Minstr/s at 96 each) bytes %d' % (unc, unc * 96 / ms / 1000, struct.unpack('<I', bytes(u.mem_read(syms['t18_jit_bytes'], 4)))[0]))
        js = struct.unpack('<8I', bytes(u.mem_read(syms['t18_jit_stat'], 32)))
        print('wait %8.0f ms  host %10d instr  %6.1f Minstr/s  jit blocks %d entries %d flushes %d slow %d idle %d instr %d fr %s used %d' % (ms, ex, ex / ms / 1000, js[0], js[1], js[3], js[4], js[2], js[7], struct.unpack('<4I', bytes(u.mem_read(syms['t18_jit_fr'], 16))), struct.unpack('<I', bytes(u.mem_read(syms['J'], 4)))[0]))
    elif p[0] == 'press':
        m = 0
        for ch in p[1]: m |= {'A': 1, 'B': 2, 'C': 4}.get(ch, 0)
        ms = float(p[2]) if len(p) > 2 else 150
        cmd(2, m); ex = cmd(1, int(ms * 1000)); cmd(2, 0); ms_total += ms; host_total += ex
    elif p[0] == 'hash':
        cmd(3); cyc = rd(2) | rd(3) << 32
        print('cyc %d pc %08x sram %08x' % (cyc, rd(4), rd(5)))
        print('  hole pages %d%s' % (rd(7) >> 8, ' OUT OF PAGES' if rd(7) & 1 else ''), file=sys.stderr)
    elif p[0] == 'stat':
        cmd(4)
        print('ginstr %d bytes %d (%.1f/instr) ' % (rd(13), rd(14), rd(14) / max(1, rd(13))), end=''); print('blocks %d entries %d stuck %d flushes %d slow %d links %d lookups %d used %d | interp thumb %d arm %d' % (rd(1), rd(2), rd(3), rd(4), rd(5), rd(6), rd(11), rd(12), rd(7), rd(10)))
    elif p[0] == 'loadst':
        b = open(p[1], 'rb').read(); n = len(b) - 0x20000
        u.mem_write(0x60FC0000, b); cmd(7, n)
        if rd(2) & 0x80000000: print('loadst failed', rd(2)); sys.exit(3)
    elif p[0] == 'shot':
        src = rd(1); o = src - 0xF8000000
        if 0 <= o < 0x20000 and not (0xE600 <= o < 0x14400):
            open(p[1], 'wb').write(bytes(u.mem_read(0x18000000 + o, 128 * 128 * 2)))
        else: print('shot: picture at', hex(src))
    elif p[0] == 'time':
        cmd(5, *[int(x) for x in p[1:7]])
    elif p[0] == 'verify':
        cmd(6)
    elif p[0] == 'exact':
        EXON[0] = True; u.hook_add(UC_HOOK_CODE, _cnt); u.ctl_flush_tb()
    elif p[0] == 'prof':
        # host profile by function over <ms>; guest pcs at dispatcher entries too
        import bisect, collections
        syms_s = []
        for l2 in subprocess.run(['arm-none-eabi-nm', '-S', elf], capture_output=True, text=True).stdout.splitlines():
            q = l2.split()
            if len(q) == 4 and q[2] in 'tTwW': syms_s.append((int(q[0], 16), int(q[1], 16), q[3]))
        syms_s.sort(); starts = [x[0] for x in syms_s]
        jc = syms.get('JC', syms.get('__jc_start', 0)); jcsz = int(os.environ.get('JCSZ', str(syms['__jc_end'] - jc if '__jc_end' in syms else 32768)))
        cnt = collections.Counter(); gpc = collections.Counter(); jpc = collections.Counter()
        PFN = os.environ.get('PROFFN'); fpc = collections.Counter()
        LASTF = collections.deque(maxlen=6); WHY = collections.Counter(); WPC = int(os.environ.get('WHYPC', '0'), 16)
        def hcode(uc, a, sz, ud):
            if jc <= a < jc + jcsz:
                cnt['<JIT code>'] += 1; jpc[a] += 1
                if WPC and (not LASTF or LASTF[-1] != 'JIT'): LASTF.append('JIT')
                return
            i = bisect.bisect_right(starts, a) - 1
            nm = syms_s[i][2] if i >= 0 and a < syms_s[i][0] + syms_s[i][1] + 4 else '?'
            cnt[nm] += 1
            if nm == PFN: fpc[a] += 1
            if WPC and (not LASTF or LASTF[-1] != nm): LASTF.append(nm)
        def hjt(uc, a, sz, ud):
            c = uc.reg_read(UC_ARM_REG_R0); g = struct.unpack('<I', uc.mem_read(c + 60, 4))[0]; gpc[g] += 1
            if WPC and (g & ~1) == (WPC & ~1): WHY[' > '.join(list(LASTF)[-5:])] += 1
            if g == int(os.environ.get('WATCHPC', '0'), 16) and gpc[g] < int(os.environ.get('WATCHN', '4')):
                v = struct.unpack('<17I', uc.mem_read(c, 68)); x = struct.unpack('<QQIiii', uc.mem_read(c + 184, 32))
                print('  at %08x cpsr %08x cyc %d mult %d irq %d fiq %d halt %d r12 %08x lr %08x r3 %08x' % (g, v[16], x[0], x[2], x[3], x[4], x[5], v[12], v[14], v[3]))
                jh = syms.get('JH', syms.get('JHa', 0))
                for i in range(1024):
                    k, off, mc, hot, none, hot2 = struct.unpack('<IHHBBB', u.mem_read(jh + 12 * i, 11)); code = syms['JC'] + 2 * (off - 1) if off else 0
                    if k == g: print('  entry code %x maxcyc %d hot %d none %d' % (code, mc, hot, none))
        h1 = u.hook_add(UC_HOOK_CODE, hcode)
        h2 = u.hook_add(UC_HOOK_CODE, hjt, begin=syms['t18_jit_try'] & ~1, end=syms['t18_jit_try'] & ~1)
        u.ctl_flush_tb()
        ms = float(p[1]); cmd(1, int(ms * 1000))
        u.hook_del(h1); u.hook_del(h2)
        tot = sum(cnt.values())
        for k, v in cnt.most_common(22): print('  %-26s %5.1f%%' % (k, 100 * v / tot))
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
        hot = sorted(jpc.items(), key=lambda x: -x[1])[:1]
        if hot:
            a0 = hot[0][0]
            # show the hottest 160 bytes around
            code = bytes(u.mem_read(a0 - 64, 224))
            for ins in md.disasm(code, a0 - 64): print('   %6d %x %s %s' % (jpc.get(ins.address, 0), ins.address, ins.mnemonic, ins.op_str))
        if PFN:
            md2 = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
            fs = [x for x in syms_s if x[2] == PFN][0]
            code = bytes(u.mem_read(fs[0], fs[1]))
            for ins in md2.disasm(code, fs[0]):
                if fpc.get(ins.address, 0) * 2000 > sum(fpc.values()): print('   F %7d %x %s %s' % (fpc[ins.address], ins.address, ins.mnemonic, ins.op_str))
        for k, v in WHY.most_common(6): print('  why %6d %s' % (v, k))
        print('  dispatcher entries by guest pc:')
        for k, v in gpc.most_common(int(os.environ.get('PROFN', '15'))): print('   %08x %d' % (k, v))
print('total %.1f s guest, %.0f M host instr, %.1f Minstr per guest second' % (ms_total / 1000, host_total / 1e6, host_total / ms_total / 1000))
if os.environ.get('JKEYS'):
    lo, hi = [int(x, 16) for x in os.environ['JKEYS'].split('-')]
    jn = int(os.environ.get("JHN","1024"))
    jadr = [syms['JH'] + 12 * i for i in range(jn)] if 'JH' in syms else [syms['JHa'] + 12 * i for i in range(jn // 2)] + [int(os.environ.get('JHB', '0x20000100'), 16) + 12 * i for i in range(jn // 2)]
    jcs = syms.get('JC', syms.get('__jc_start'))
    for i in range(jn):
        k, off, mc, hot, none = struct.unpack('<IHHBB', u.mem_read(jadr[i], 10)); code = jcs + 2 * (off - 1) if off else 0
        if lo <= k < hi:
            print('key %08x code %x maxcyc %d hot %d none %d' % (k, code, mc, hot, none))
            if code and os.environ.get('JDIS'):
                import capstone
                md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
                for ins in list(md.disasm(bytes(u.mem_read(code, 120)), code))[:int(os.environ['JDIS'])]: print('    %x %s %s' % (ins.address, ins.mnemonic, ins.op_str))
if os.environ.get('BLKDUMP'):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    lo, hi = [int(x, 16) for x in os.environ['BLKDUMP'].split('-')]
    p = syms['__jc_start']; jend = struct.unpack('<I', bytes(u.mem_read(syms['J'], 4)))[0]
    tot = {}
    while p + 8 <= jend:
        k0, k1, gl, hs = struct.unpack('<HHHH', bytes(u.mem_read(p, 8))); key = k0 | k1 << 16; code = p + 8
        if lo <= key < hi:
            print('block %08x guest %d hw, host %d bytes at %x' % (key, gl, 2 * hs, code))
            for ins in md.disasm(bytes(u.mem_read(code, 2 * hs)), code): print('    %x %s %s' % (ins.address, ins.mnemonic, ins.op_str))
        p = code + 2 * hs
if os.environ.get('JDUMP'):
    import capstone
    want = int(os.environ['JDUMP'], 16); jh = syms['JH']
    for i in range(int(os.environ.get("JHN","512"))):
        k, off, mc = struct.unpack('<IHH', u.mem_read(jh + 12 * i, 8)); code = syms['JC'] + 2 * (off - 1) if off else 0
        if k == want and code:
            print('block %08x code %08x maxcyc %d' % (k, code, mc))
            md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
            for ins in md.disasm(bytes(u.mem_read(code, 200)), code): print('   ', hex(ins.address), ins.mnemonic, ins.op_str)
if os.environ.get('GDIS'):
    import capstone
    for it in os.environ['GDIS'].split(','):
        a, n, m = it.split(':'); a = int(a, 16); n = int(n, 16)
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if m == 't' else capstone.CS_MODE_ARM)
        src = bytes(u.mem_read(0x18000000 + (a - 0xF8000000), n)) if a >= 0xF8000000 else bytes(u.mem_read(0x60000000 + (a - 0x20000000), n))
        for ins in md.disasm(src, a): print('   ', hex(ins.address), ins.mnemonic, ins.op_str)
if os.environ.get('SRAMDUMP'): open(os.environ['SRAMDUMP'], 'wb').write(bytes(u.mem_read(0x18000000, 0x20000)))
if os.environ.get('LKDBG'):
    v = struct.unpack('<1536I', bytes(u.mem_read(0x20008000, 6144)))
    rows = sorted([(v[i + 2], v[i], v[i + 1]) for i in range(0, 1536, 3) if v[i]], reverse=True)
    for c, k, r in rows[:int(os.environ['LKDBG'])]: print('lk %08x r%d %d' % (k, r, c))
if os.environ.get('IPCH'):
    v = struct.unpack('<2048I', bytes(u.mem_read(0x20008000, 8192)))
    rows = sorted([(v[i + 1], v[i]) for i in range(0, 2048, 2) if v[i + 1]], reverse=True)
    for c, pc in rows[:int(os.environ['IPCH'])]: print('ipc %08x %d' % (pc, c))
if os.environ.get('TRYPCS'):
    v = struct.unpack('<%dI' % (1024 * 3), bytes(u.mem_read(0x20004000, 1024 * 12)))
    names = ['calls', 'stuck', 'sp', 'seq', 'noblock', 'none', 'budget', 'entered', 'target']
    rows = sorted([(v[i + 2], v[i], v[i + 1]) for i in range(0, len(v), 3) if v[i] and v[i + 1] != 0], reverse=True)
    for c, pc, r in [x for x in rows if x[2] != 3 or os.environ.get('TRYSEQ')][:int(os.environ['TRYPCS'])]: print('trypc %08x %-8s %d' % (pc, names[r], c))

if JSH is not None:
    tot = sum(JSH.values()); print('j_slow entries', tot)
    for k, v in JSH.most_common(20): print('  jslow %08x %d' % (k, v))

if FNCNT: print('fncount', ' '.join('%s=%d' % kv for kv in FNCNT.items()))

if KSEQ is not None: open(os.environ['KSEQ'], 'wb').write(struct.pack('<%dI' % len(KSEQ), *KSEQ))
