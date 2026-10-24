/*
 * Paramix Studio: install a color Tama emulator into the Paradise ROM.
 * The emulator program (emu.bin, GPL-3.0, core derived from tamaemu) and
 * the stub are embedded as Base64 at build time. The user supplies the
 * color Tama firmware; it never leaves the computer.
 *
 * Layout in the Paradise flash:
 *   0x0A0000  emulator program (copied to PRAM at startup)
 *   0x0B0000  stub and home screen hook (hold A+C)
 *   0x0C0000  saved run state (written by the emulator itself)
 *   0x940000  color Tama firmware, packed in 64 KB blocks; behind it, up to
 *             0xD40000, spare pages for blocks the guest writes (item stores)
 *   0x840000  instead, for firmware that does not fit there (P's, 4U+): up to 0xD48000.
 *             The menu's field saves then have no room.
 *
 * Author: mameise
 */
(function (root) {
'use strict';

const XIP_DEC = 0x11000;
const xip = (off) => 0x10000000 + off - XIP_DEC;
const APP_OFF = 0xA0000, STUB_OFF = 0xB0000, SAVE_OFF = 0xC0000, SAVE_END = 0x110000;
const GUEST_OFF = 0x940000, GUEST_END = 0xD40000;
const BIG_OFF = 0x840000, BIG_END = 0xD48000, SPARE_MIN = 0x4000;   // big firmware (1.3.2); 256 KB aligned like 0x940000
const APP_MAX = 0x10000;
const PMXC = 0x43584D50;
// Placeholders in the prebuilt stub (stub_tpl.bin)
const PH_APP = 0x1234ABCD, PH_AUTO = 0x66666661, PH_ORIG = [0x22222221, 0x33333331, 0x44444441, 0x55555551];
const HOOK_OFF = [0x40, 0x46, 0x4C, 0x52];
const DEVICES = {
  pc:  { kind: 0, size: 0x400000, label: '4 MB', par: 0x3F },
  idl: { kind: 1, size: 0x800000, label: '8 MB', par: 0x00 },
  ps:  { kind: 2, size: 0x800000, label: "P's", par: 0x00 },
  '4u': { kind: 3, size: 0x800000, label: '4U+', par: 0x00 },
  '4u-plain': { kind: 4, size: 0x800000, label: '4U', par: 0x00 },
  t7:  { kind: 99, size: 0, label: 'T7Emu', par: 0x00 },   // V7 (T7Emu), not packed like the others
  t18: { kind: 98, size: 0, label: 'T18Emu', par: 0x00 },  // TG18 (T18Emu), own block map
};
// 8 MB firmware by its reset vector (file offset 0x400000), as tamaemu's dlc tool tells them apart
const RESET_VEC = { 0x024FDFE6: 'ps', 0x024F6256: '4u', 0x024C680A: '4u-plain' };
// Blocks with many data accesses first: they sit in the cached 1 MB window
const HOT = {
  pc:  [0x25, 0x21, 0x04, 0x02, 0x01, 0x03, 0x05, 0x20, 0x22, 0x23, 0x24, 0x26, 0x27, 0x28, 0x29, 0x2a],
  idl: [0x0b, 0x0e, 0x0d, 0x19, 0x08, 0x09, 0x16, 0x18, 0x0c, 0x17, 0x4f, 0x50, 0x40, 0x15, 0x4c, 0x4d],   // v0.8.17 after measuring (0x08/0x09 added)
  ps: [], '4u': [], '4u-plain': [],
};

const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
function assetEnd(b) { return 0x111000 + 16 + u32(b, 0x111000 + 8); }
function isFF(b, a, n) { for (let i = a; i < a + n; i++) if (b[i] !== 0xFF) return false; return true; }
function b64(s) {
  if (typeof atob === 'function') { const t = atob(s); const o = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) o[i] = t.charCodeAt(i); return o; }
  return new Uint8Array(Buffer.from(s, 'base64'));
}

/** Which firmware is this? Returns 'pc' or 'idl', or throws an error. */
function emuDetect(g) {
  if (g.length >= 0x100 && g.length <= 0x400000) {
    const v = u32(g, 0);
    if (v >= 0xC00000 && v < 0x1000000) return 'pc';
  }
  if (g.length > 0x400000 && g.length <= 0x800000) {
    const v = u32(g, 0x400000);
    if (v >= 0x2000000 && v < 0x2800000) return RESET_VEC[v] || 'idl';
  }
  throw new Error('firmware not recognised (expected a 4 MB or 8 MB image of a colour Tamagotchi)');
}

/** Paradise scene table: name -> flash offset of the 28-byte entry. */
function sceneTable(d) {
  const names = new Map();
  const pat = [0x50, 0x53, 0x49, 0x44, 0x5F];   // "PSID_"
  for (let i = 0; i + 5 < d.length; i++) {
    if (d[i] !== 0x50 || d[i + 1] !== 0x53 || d[i + 2] !== 0x49 || d[i + 3] !== 0x44 || d[i + 4] !== 0x5F) continue;
    let j = i + 5; while (j < d.length && ((d[j] >= 0x41 && d[j] <= 0x5A) || (d[j] >= 0x30 && d[j] <= 0x39) || d[j] === 0x5F)) j++;
    if (d[j] !== 0) continue;
    let s = ''; for (let k = i; k < j; k++) s += String.fromCharCode(d[k]);
    names.set(xip(i), s);
    i = j;
  }
  void pat;
  const ptrs = [];
  for (let o = 0; o + 4 <= d.length; o += 4) { const w = u32(d, o); if (names.has(w)) ptrs.push([o, w]); }
  if (!ptrs.length) throw new Error('scene table not found');
  let best = [], run = [ptrs[0]];
  for (let k = 1; k < ptrs.length; k++) {
    if (ptrs[k][0] - ptrs[k - 1][0] === 28) run.push(ptrs[k]);
    else { if (run.length > best.length) best = run; run = [ptrs[k]]; }
  }
  if (run.length > best.length) best = run;
  const base = best[0][0] - 4, tab = {};
  best.forEach(([o, w], i) => { tab[names.get(w)] = base + i * 28; });
  return tab;
}

/** Pack the firmware into 64 KB blocks. Returns { map, blob, blocks }. */
function packGuest(fw, dev) {
  const D = DEVICES[dev];
  const g = new Uint8Array(D.size); g.fill(0xFF); g.set(fw.subarray(0, Math.min(fw.length, D.size)));
  const n = D.size >> 16;
  let used = [];
  for (let i = 0; i < n; i++) if (i === D.par || !isFF(g, i << 16, 0x10000)) used.push(i);
  // 1.6.6: a save of another emulator (P's with download content) can hold more blocks than fit. The sparsest
  // blocks then go into spare pages (8 KB parts behind the packed firmware, as the device keeps written empty
  // blocks); the device reads them from there.
  const parts = (b) => { const r = []; for (let k = 0; k < 8; k++) if (!isFF(g, (b << 16) + (k << 13), 0x2000)) r.push((b << 3) + k); return r; };
  const room = BIG_END - BIG_OFF, PG_RES = 4;
  const need = (nb, np) => np ? ((((nb << 16) + 0x1FFF) & ~0x1FFF) + 0x2000 + ((np + PG_RES) << 13)) : (nb << 16) + SPARE_MIN;
  let paged = [];
  if (need(used.length, 0) > room) {
    const cand = used.filter((b) => b !== D.par).map((b) => ({ b, p: parts(b) })).sort((x, y) => x.p.length - y.p.length);
    let nb = used.length, np = 0;
    for (const c of cand) {
      if (need(nb, np) <= room) break;
      if (c.p.length >= 8 || np + c.p.length + PG_RES > 64) break;
      paged.push(c); nb--; np += c.p.length;
    }
    if (need(nb, np) > room) throw new Error('firmware too big: ' + used.length + ' blocks');
    used = used.filter((b) => !paged.some((c) => c.b === b));
  }
  const order = HOT[dev].filter((b) => used.includes(b)).concat(used.filter((b) => !HOT[dev].includes(b)));
  const map = new Uint8Array(128); map.fill(0xFF);
  const blob = new Uint8Array(order.length << 16);
  order.forEach((b, idx) => { map[b] = idx; blob.set(g.subarray(b << 16, (b + 1) << 16), idx << 16); });
  const pages = [].concat(...paged.map((c) => c.p.map((v) => ({ v, data: g.subarray(v << 13, (v + 1) << 13) }))));
  if (BIG_OFF + blob.length + SPARE_MIN > BIG_END) throw new Error('firmware too big: ' + order.length + ' blocks');
  return { map, blob, blocks: order.length, big: pages.length > 0 || GUEST_OFF + blob.length + SPARE_MIN > GUEST_END, pages };
}
/** Spare pages of the guest behind the packed firmware: table (as state.c of the device) and 8 KB parts. */
function putPages(out, gOff, gEnd, blobLen, pages) {
  const lo = (gOff + blobLen + 0x1FFF) & ~0x1FFF, hi = gEnd & ~0x1FFF, data = lo + 0x2000;
  const n = Math.min(64, (hi - data) >> 13);
  if (pages.length > n) throw new Error('no room for the spare pages');
  const vp = new Array(64).fill(0xFFFF);
  pages.forEach((p, i) => { vp[i] = p.v; out.set(p.data, data + (i << 13)); });
  let x = (0x50584D50 ^ Math.imul(n, 0x9E3779B1)) >>> 0;
  for (let i = 0; i < 64; i++) x = (((x << 5) | (x >>> 27)) ^ vp[i]) >>> 0;
  out.fill(0xFF, lo, lo + 0x1000);
  w32(out, lo, 0x50584D50); w32(out, lo + 4, n); w32(out, lo + 8, x); w32(out, lo + 12, 0);
  vp.forEach((v, i) => { out[lo + 16 + 2 * i] = v & 255; out[lo + 17 + 2 * i] = v >> 8; });
}

/** Is an emulator already installed? Returns the original handlers or null. */
function installedOrigs(rom, tab) {
  const o = tab.PSID_HOME;
  const cur = [0, 1, 2, 3].map((k) => u32(rom, o + 12 + 4 * k));
  const lo = xip(STUB_OFF), hi = lo + 0x1000;
  if (!cur.some((v) => v >= lo && v < hi)) return null;
  return PH_ORIG.map((_, k) => u32(rom, STUB_OFF + 0x94 + 4 * k));
}

/** Install the stub with its home screen hook (shared by the emulator and DOOM). */
function installStub(out, tab, prev, stubTpl) {
  const origs = prev || [0, 1, 2, 3].map((k) => u32(out, tab.PSID_HOME + 12 + 4 * k));
  const st = new Uint8Array(stubTpl);
  const put = (ph, v) => { let n = 0; for (let i = 0; i + 4 <= st.length; i += 2) if (u32(st, i) === ph) { w32(st, i, v); n++; } if (n !== 1) throw new Error('launcher template does not fit'); };
  put(PH_APP, xip(APP_OFF));
  PH_ORIG.forEach((ph, k) => put(ph, origs[k]));
  for (let i = 0; i + 4 <= st.length; i += 2) if (u32(st, i) === PH_AUTO) w32(st, i, 0);   // autostart off, see autoSet
  out.fill(0xFF, STUB_OFF, STUB_OFF + 0x1000);
  out.set(st, STUB_OFF);
  HOOK_OFF.forEach((h, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, (xip(STUB_OFF) + h) | 1));
}
/** Install only the program and stub (for DOOM). Returns a new image. */
function installApp(rom, app, stubTpl, opts = {}) {
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB');
  const out = new Uint8Array(rom);
  const tab = sceneTable(out);
  if (!tab.PSID_HOME) throw new Error('home screen not found');
  const prev = installedOrigs(out, tab);
  if (!prev && !isFF(out, APP_OFF, 0x11000)) throw new Error('area 0x0A0000 is in use');
  if (app.length > APP_MAX) throw new Error('program too big');
  if (!opts.keep) freeT18(out);   // opts.keep (1.6.8): the same program again (stub swap), T18Emu keeps its blocks
  out.fill(0x00, APP_OFF, APP_OFF + APP_MAX);
  out.set(app, APP_OFF);
  installStub(out, tab, prev, stubTpl);
  return out;
}

/**
 * Install the emulator. rom: 16 MB Paradise image, guest: color Tama
 * firmware, app/stub: embedded binaries. Returns a new image.
 */
function emuPatch(rom, guest, app, stubTpl, opts = {}) {
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB');
  const dev = opts.device || emuDetect(guest);
  const out = new Uint8Array(rom);
  const tab = sceneTable(out);
  if (!tab.PSID_HOME) throw new Error('home screen not found');
  const prev = installedOrigs(out, tab);
  freeT18(out);
  // Regions must be free (or already used by us)
  const { map, blob, blocks, big, pages } = packGuest(guest, dev);
  const gOff = big ? BIG_OFF : GUEST_OFF, gEnd = big ? BIG_END : GUEST_END;
  if (big && assetEnd(out) > 0x830000) throw new Error('the character data reaches 0x' + assetEnd(out).toString(16) + ', the emulator needs 0x830000 onwards');
  if (!prev) {
    if (!isFF(out, APP_OFF, 0x11000)) throw new Error('area 0x0A0000 is in use');
    if (!isFF(out, gOff, gEnd - gOff)) throw new Error('area 0x' + gOff.toString(16) + ' is in use');
  }
  if (!isFF(out, SAVE_OFF, SAVE_END - SAVE_OFF) && !prev) throw new Error('area 0x0C0000 is in use');
  // Store the packed guest (an earlier one may sit in the other layout)
  if (prev && u32(out, APP_OFF + 0x140) === PMXC) { const o = u32(out, APP_OFF + 0x148); if (o === BIG_OFF) out.fill(0xFF, BIG_OFF, BIG_END); else if (o === GUEST_OFF) out.fill(0xFF, GUEST_OFF, GUEST_END); }
  out.fill(0xFF, gOff, gEnd);
  out.set(blob, gOff);
  if (pages.length) putPages(out, gOff, gEnd, blob.length, pages);
  // Program with settings
  if (app.length > APP_MAX) throw new Error('emulator program too big');
  const a = new Uint8Array(app);
  let ci = -1; for (let i = 0; i + 4 <= a.length; i += 4) if (u32(a, i) === PMXC) { ci = i; break; }
  if (ci < 0) throw new Error('settings block missing in the program');
  // version 3: the room behind the packed firmware up to GUEST_END holds the guest's spare pages
  // version 4 (1.5.1): flags at +156, bit 1 = clock from the Paradise at every start (on unless opts.clock === false)
  const v4 = a.length >= ci + 160 && u32(a, ci + 156) === 0;
  w32(a, ci + 4, v4 ? 4 : 3); w32(a, ci + 8, gOff); w32(a, ci + 12, blob.length); w32(a, ci + 16, SAVE_OFF); w32(a, ci + 20, DEVICES[dev].kind);
  a.set(map, ci + 24);
  w32(a, ci + 152, gEnd);
  if (v4) w32(a, ci + 156, (opts.clock === false ? 0 : 2) | (opts.lang === 'en' ? 1 : 0));   // bit 0 (1.6.6): English texts
  out.fill(0x00, APP_OFF, APP_OFF + APP_MAX);
  out.set(a, APP_OFF);
  installStub(out, tab, prev, stubTpl);
  // erase the old save (it may belong to a different firmware)
  out.fill(0xFF, SAVE_OFF, SAVE_END);
  return { rom: out, device: dev, blocks, big };
}

/**
 * Newer emulator program into a ROM that already has the emulator (v1.2.6).
 * Only the program at 0x0A0000 changes: settings (firmware layout) are taken over,
 * the packed firmware, the spare pages and the saved session stay. Returns null if
 * the program is already the same.
 */
function emuUpgrade(rom, app) {
  const ci0 = 0x140;
  if (u32(rom, APP_OFF + ci0) !== PMXC) return null;
  const a = new Uint8Array(app);
  let ci = -1; for (let i = 0; i + 4 <= a.length; i += 4) if (u32(a, i) === PMXC) { ci = i; break; }
  if (ci !== ci0) return null;
  const ver = u32(rom, APP_OFF + ci + 4);
  if (ver < 2) return null;
  for (let i = 8; i < 152; i++) a[ci + i] = rom[APP_OFF + ci + i];
  w32(a, ci + 4, 4);
  w32(a, ci + 152, ver >= 3 ? u32(rom, APP_OFF + ci + 152) : GUEST_END);
  w32(a, ci + 156, ver >= 4 ? u32(rom, APP_OFF + ci + 156) : 2);   // 1.5.1: the clock switch, on for older ROMs
  const go = u32(a, ci + 8);
  if (!(go === GUEST_OFF && u32(a, ci + 152) <= GUEST_END) && !(go === BIG_OFF && u32(a, ci + 152) <= BIG_END)) return null;
  let same = true;
  for (let i = 0; i < APP_MAX && same; i++) if (rom[APP_OFF + i] !== (i < a.length ? a[i] : 0)) same = false;
  if (same) return null;
  const out = new Uint8Array(rom);
  out.fill(0x00, APP_OFF, APP_OFF + APP_MAX);
  out.set(a, APP_OFF);
  return out;
}

/*
 * Autostart (v1.2.7): the stub holds a word (0/1, placeholder PH_AUTO in the template).
 * stubs = [[offset in the ROM, template], ...]. The ROM's stub must match the template
 * everywhere except the placeholder words.
 */
const PH_ALL = [PH_APP, 0x6789EF01, PH_AUTO, ...PH_ORIG];
function autoAt(rom, stubs) {
  for (const [off, tpl] of stubs) {
    if (off < 0 || off + tpl.length > rom.length) continue;
    let at = -1, ok = true;
    const skip = new Set();
    for (let i = 0; i + 4 <= tpl.length; i += 2) { const w = u32(tpl, i); if (PH_ALL.includes(w)) { for (let k = 0; k < 4; k++) skip.add(i + k); if (w === PH_AUTO) at = i; } }
    if (at < 0) continue;
    for (let i = 0; i < tpl.length && ok; i++) if (!skip.has(i) && rom[off + i] !== tpl[i]) ok = false;
    if (ok && u32(rom, off + at) <= 1) return off + at;
  }
  return -1;
}
// Stubs of 1.2.7 (autostart once per power-on): only read, the Studio puts in the current one
const STUB_127 = ['crYA8IW4AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAN/4UMAu4N/4UMAr4N/4TMAo4N/4TMAl4AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACEiIiIxMzMzQURERFFVVVUt6Q9QFEgBaBRKUrEUShNoFEiDQgXQEGAR9ABvAdD/952/DUgQShNoEfTAbw/RGAxO8qFxiEID0EDyAAPO8qFzATMTYJiyWii/9IivAeAAIxNgvegPUGBHAIABQGFmZmb8DwAgQVJQQfgPACBytk/wikNI8kECGmAEItpgT/SAAdpoRCoB0AE5+tENSAAhT/SAMlD4BDtB+AQ7BDr50b/zT4+/82+PB0gAIQFgBkgBaAH04GEFShFDAWC/80+P/ufNqzQSCO0A4AztAOAEAPoF', 'crYBTADwirjNqzQSAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAN/4UMAu4N/4UMAr4N/4TMAo4N/4TMAl4AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACEiIiIxMzMzQURERFFVVVUt6Q9QFkgBaBZKUrEWShNoFkiDQgXQEGAR9ABvAdD/952/D0gSShNoEfTAbxPRGAxO8qFxiEID0EDyAAPO8qFzATMTYJiyWigH0wpMACw/9IWvcrYQ4AAjE2C96A9QYEcAgAFAYWZmZvwPACBBUlBB+A8AIAHviWdytk/wikNI8kECGmAEItpgT/SAAdpoRCoB0AE5+tEgRgAhT/SAMlD4BDtB+AQ7BDr50b/zT4+/82+PBkgAIQFgBUgBaAH04GEEShFDAWC/80+P/ucI7QDgDO0A4AQA+gU='];
// Menu stub of 1.6.0 to 1.6.7 (menu address at 0x148, before the launcher entry of 1.6.8)
const STUB_FELD_167 = 'crYBTADworjNqzQScrYERgTxoEQE9Yg0APCYuAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAN/4UMAu4N/4UMAr4N/4TMAo4N/4TMAl4AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACEiIiIxMzMzQURERFFVVVUt6Q9QH0gBaB9K2rEfSpN4w7n/I5Nwk3ijsR1KE2gdSINCDdAcSINCA9AcSABoHigG0hlIEGAR9ABvA9D/946/FUgQYA9IFkoTaBH0wG8T0RgMTvKhcYhCA9BA8gADzvKhcwEzE2CYslooB9MNTAAsP/R0r3K2F+AAIxNgvegPUGBHAAAAgAFAYWZmZhjtAOD8DwAgUE1YTFBNWFIEAwBF+A8AIAHviWdytk/wikNI8kECGmAEItpgT/SAAdpoRCoB0AE5+tEgRgAhT/SAMlD4BDtB+AQ7BDr50b/zT4+/82+PBkgAIQFgBUgBaAH04GEEShFDAWC/80+P/ucI7QDgDO0A4AQA+gU=';
function legacyStubs(stubs) {
  const out = [];
  for (const [off] of stubs) for (const t of STUB_127.concat([STUB_FELD_167])) out.push([off, b64(t)]);
  return out;
}
/** Autostart setting of the ROM: true, false, or null (stub without the switch). */
function autoGet(rom, stubs) { const a = autoAt(rom, stubs); return a < 0 ? null : u32(rom, a) === 1; }
/** Was the autostart on in a stub of 1.2.7? */
function autoGetOld(rom, stubs) { const a = autoAt(rom, legacyStubs(stubs)); return a >= 0 && u32(rom, a) === 1; }
/** Set the autostart word. Returns a new image, or null if the stub has no such word. */
function autoSet(rom, stubs, on) {
  const a = autoAt(rom, stubs);
  if (a < 0) return null;
  const out = new Uint8Array(rom); w32(out, a, on ? 1 : 0); return out;
}

/** Remove the emulator again (home screen back to the original handlers). */
function emuRemove(rom) {
  const out = new Uint8Array(rom);
  const tab = sceneTable(out);
  const prev = installedOrigs(out, tab);
  if (!prev) throw new Error('no emulator built in');
  const c18 = t18Cfg(rom);
  if (c18) for (const b of c18.map.concat(c18.spare)) if (b !== 0xFFFF) out.fill(0xFF, b << 16, (b + 1) << 16);
  prev.forEach((v, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, v));
  out.fill(0xFF, APP_OFF, APP_OFF + 0x11000);
  out.fill(0xFF, SAVE_OFF, SAVE_END);
  out.fill(0xFF, GUEST_OFF, GUEST_END);
  if (u32(rom, APP_OFF + 0x140) === PMXC && u32(rom, APP_OFF + 0x148) === BIG_OFF) out.fill(0xFF, BIG_OFF, BIG_END);
  // DOOM: program part in the flash window and WAD (only behind the character data)
  out.fill(0xFF, STUB_OFF + 0x1000, SAVE_OFF);
  const assetEnd = (0x111000 + 16 + u32(out, 0x111000 + 8) + 0xFFF) & ~0xFFF;
  out.fill(0xFF, Math.max(0x830000, assetEnd), 0xD48000);
  return out;
}

// ------------------------------------------------------------ download content
/*
 * Download content (DLC) for the 8 MB firmware (iD L). Store geometry and routing
 * come from tamaemu (tools/dlc.c, GPL-3.0, maragotchi): a record is a TAMAGO
 * block, routed by (type [0x4E], category [0x50], flag [0x51]) into a store;
 * it goes into the first fully erased slot, existing slots are never overwritten.
 */
const IDL_STORES = [
  { kind: 0, base: 0x590000, size: 0x0800, n: 20, label: 'Meals' },
  { kind: 1, base: 0x5A0000, size: 0x0800, n: 20, label: 'Snacks' },
  { kind: 2, base: 0x600000, size: 0x4000, n: 5, label: 'Clothing' },
  { kind: 3, base: 0x650000, size: 0x2000, n: 10, label: 'Accessories' },
  { kind: 4, base: 0x6A0000, size: 0x2000, n: 10, label: 'Items' },
  { kind: 5, base: 0x740000, size: 0x8000, n: 5, label: 'Daily items' },
  { kind: 6, base: 0x780000, size: 0x2000, n: 10, label: 'Wallpapers' },
  { kind: 7, base: 0x7A0000, size: 0x8000, n: 2, label: 'Games' },
  { kind: 8, base: 0x7B0000, size: 0x8000, n: 2, label: 'Destinations' },
];
const IDL_ROUTE = [
  [0x81, 0x02, 0x01, 0], [0x81, 0x0C, 0x02, 1], [0x81, 0x0D, 0x02, 1], [0x81, 0x1F, 0x00, 6],
  [0x81, 0x29, 0x00, 5], [0x81, 0x29, 0x02, 5], [0x81, 0x2A, 0x01, 4], [0x81, 0x33, 0x00, 2],
  [0x81, 0x34, 0x00, 3], [0x81, 0x35, 0x00, 3], [0x94, 0x47, 0x02, 8], [0x94, 0x5B, 0x01, 7],
];
// 1.6.6: P's, 4U+ and 4U, stores and routing likewise from tamaemu's tools/dlc.c
const PS_STORES = [
  { kind: 0, base: 0x5A0000, size: 0x0800, n: 20, label: 'Meals' },
  { kind: 1, base: 0x5B0000, size: 0x0800, n: 15, label: 'Snacks' },
  { kind: 2, base: 0x610000, size: 0x4000, n: 5, label: 'Clothing' },
  { kind: 3, base: 0x660000, size: 0x2000, n: 10, label: 'Accessories' },
  { kind: 4, base: 0x6B0000, size: 0x2000, n: 10, label: 'Items' },
  { kind: 6, base: 0x6F0000, size: 0x2000, n: 10, label: 'Wallpapers' },
  { kind: 7, base: 0x710000, size: 0x8000, n: 2, label: 'Games' },
  { kind: 8, base: 0x720000, size: 0x8000, n: 2, label: 'Destinations' },
  { kind: 10, base: 0x750000, size: 0x8000, n: 4, label: 'VDP recipes' },
];
const PS_ROUTE = [[0x81, 0x02, 0], [0x81, 0x0D, 1], [0x81, 0x33, 2], [0x81, 0x34, 3], [0x81, 0x35, 3], [0x81, 0x2A, 4], [0x81, 0x1F, 6], [0x81, 0x29, 10], [0x94, 0x47, 8]];
const FOURU_STORES = [
  { kind: 15, base: 0x5A0000, size: 0x2000, n: 4, label: 'Decorations' },
  { kind: 0, base: 0x5B0000, size: 0x0800, n: 20, label: 'Meals' },
  { kind: 1, base: 0x5C0000, size: 0x0800, n: 20, label: 'Snacks' },
  { kind: 2, base: 0x610000, size: 0x4000, n: 5, label: 'Clothing' },
  { kind: 3, base: 0x660000, size: 0x2000, n: 10, label: 'Accessories' },
  { kind: 4, base: 0x6C0000, size: 0x2000, n: 15, label: 'Items' },
  { kind: 6, base: 0x6F0000, size: 0x2000, n: 10, label: 'Wallpapers' },
  { kind: 7, base: 0x710000, size: 0x8000, n: 2, label: 'Games' },
  { kind: 8, base: 0x720000, size: 0x8000, n: 4, label: 'Destinations' },
  { kind: 16, base: 0x740000, size: 0x2000, n: 1, label: 'Coupon' },
  { kind: 17, base: 0x750000, size: 0x8000, n: 1, label: 'Bingo' },
  { kind: 18, base: 0x760000, size: 0x8000, n: 1, label: 'Gashapon' },
  { kind: 19, base: 0x780000, size: 0x8000, n: 8, label: 'Characters' },
];
const FOURU_ROUTE = [
  [0x81, 0x01, 0x01, 0], [0x81, 0x02, 0x01, 0], [0x81, 0x01, 0x02, 1], [0x81, 0x0D, 0x02, 1], [0x81, 0x1F, 0x00, 6],
  [0x81, 0x33, 0x00, 2], [0x81, 0x34, 0x00, 3], [0x81, 0x35, 0x00, 3], [0x94, 0x5B, 0x01, 7], [0x94, 0x5B, 0x02, 8],
  [0x94, 0x48, 0x03, 19], [0x94, 0x47, 0x02, 8],
];
const DLC_DEV = { idl: IDL_STORES, ps: PS_STORES, '4u': FOURU_STORES, '4u-plain': FOURU_STORES };
const dlcDev = (g) => { let d = null; try { d = emuDetect(g); } catch (e) { /* unknown: as before */ } return DLC_DEV[d] ? d : g.length >= 0x7B0000 && !d ? 'idl' : null; };
const dlcAscii = (r) => { let s = ''; for (let i = 0x34; i < 0x48 && i < r.length && r[i]; i++) s += String.fromCharCode(r[i]); return s; };
function fouruId(s) { const m = /[0-9]{2,}/.exec(s); return m ? Number(m[0]) : -1; }
// kept for older callers: the game store
const GAME_BASE = 0x7A0000, GAME_SIZE = 0x8000, GAME_SLOTS = 2;
const isTamago = (g, o) => o + 6 <= g.length && g[o] === 0x54 && g[o + 1] === 0x41 && g[o + 2] === 0x4D && g[o + 3] === 0x41 && g[o + 4] === 0x47 && g[o + 5] === 0x4F;
function dlcName(r) {
  let s = ''; for (let i = 6; i + 1 < 0x34; i += 2) { const c = (r[i] << 8) | r[i + 1]; if (!c) break; s += String.fromCharCode(c); }
  return s.replace(/^DL_[A-Za-z]+_/, '').replace(/\.(jpg|JPG)$/, '').trim();
}
/** All stores with their slots: [{ kind, label, slots: [{ slot, name|null }] }] (8 MB firmware: iD L, P's, 4U+, 4U). */
function dlcStores(g) {
  const dev = dlcDev(g); if (!dev) return [];
  return DLC_DEV[dev].map((k) => ({ kind: k.kind, label: k.label, slots: Array.from({ length: k.n }, (_, i) => {
    const o = k.base + i * k.size;
    return { slot: i, name: isTamago(g, o) ? dlcName(g.subarray(o, o + 0x34)) : null };
  }) }));
}
/** Game slots (compatibility with the old single-store view). */
function dlcSlots(g) {
  const st = dlcStores(g).find((k) => k.kind === 7);
  return st ? st.slots : [];
}
/** Store for a record: { kind, label } or null. dev: 'idl' (default), 'ps', '4u', '4u-plain'. */
function dlcRoute(r, dev = 'idl') {
  const t = r[0x4E], c = r[0x50], f = r[0x51], stores = DLC_DEV[dev] || IDL_STORES;
  let kind = -1;
  if (dev === 'ps') {
    if (t === 0x94 && c === 0x5B) kind = f === 0x01 ? 7 : 8;
    else if (t === 0x81 && c === 0x29 && dlcAscii(r).startsWith('idn_rec')) kind = -1;   // recipe: store not known
    else { const m = PS_ROUTE.find((x) => x[0] === t && x[1] === c); if (m) kind = m[2]; }
  } else if (dev === '4u' || dev === '4u-plain') {
    const id = fouruId(dlcAscii(r)), m1 = id % 1000;
    if (t === 0x81 && c === 0x2A && f === 0x01) kind = id < 0 ? -1 : m1 >= 601 && m1 <= 749 ? 16 : m1 >= 801 && m1 <= 949 ? 15 : 4;
    else if (t === 0x81 && c === 0x29 && f === 0x00) kind = id >= 0 && m1 >= 301 && m1 <= 449 ? 17 : id >= 0 && m1 >= 601 && m1 <= 749 ? 18 : -1;
    else { const m = FOURU_ROUTE.find((x) => x[0] === t && x[1] === c && x[2] === f); if (m) kind = m[3]; }
  } else {
    const m = IDL_ROUTE.find((x) => x[0] === t && x[1] === c && x[2] === f); if (m) kind = m[3];
  }
  return kind < 0 ? null : stores.find((k) => k.kind === kind) || null;
}
/** Extract and check the TAMAGO record of a download file (.jpg or raw). dev: firmware it goes into. */
function dlcRecord(d, dev = 'idl') {
  let m = -1;
  if (isTamago(d, 0)) m = 0;
  else {
    let e = -1; for (let i = 0; i + 1 < d.length; i++) if (d[i] === 0xFF && d[i + 1] === 0xD9) { e = i + 2; break; }
    for (let i = Math.max(0, e); i + 6 <= d.length; i++) if (isTamago(d, i)) { m = i; break; }
  }
  if (m < 0) throw new Error('no download content in the file');
  const b = d.subarray(m);
  const ok = (n) => { if (n < 3) return false; let s = 0; for (let i = 0; i < n - 2; i++) s += b[i]; return (s & 0xFFFF) === ((b[n - 2] << 8) | b[n - 1]); };
  const decl = b.length >= 0x4C ? ((b[0x48] << 24) | (b[0x49] << 16) | (b[0x4A] << 8) | b[0x4B]) >>> 0 : 0;
  let len = 0;
  if (decl >= 0x60 && decl <= b.length && ok(decl)) len = decl;
  else for (let s = 0; s <= 16; s++) { const n = b.length - s; if (n >= Math.max(0x60, decl) && ok(n)) { len = n; break; } }
  if (!len) throw new Error('content checksum does not match');
  const r = b.slice(0, len);
  const st = dlcRoute(r, dev);
  if (!st) throw new Error('this content fits no store of this firmware');
  if (r.length > st.size) throw new Error('content too big for its store');
  return r;
}
function grow(g) { const out = new Uint8Array(Math.max(g.length, 0x800000)); out.fill(0xFF); out.set(g); return out; }
/** Add a record to the first free slot of its store. Returns { guest, store, slot }. */
function dlcAdd(g, rec) {
  const dev = dlcDev(g); if (!dev) throw new Error('only for the 8 MB firmware');
  const st = dlcRoute(rec, dev); if (!st) throw new Error('no matching store');
  const out = grow(g);
  for (let i = 0; i < st.n; i++) { const o = st.base + i * st.size; if (isTamago(out, o) && out.subarray(o, o + rec.length).every((v, k) => v === rec[k])) throw new Error('already added'); }
  for (let i = 0; i < st.n; i++) {
    const o = st.base + i * st.size;
    if (isTamago(out, o)) continue;
    if (!out.subarray(o, o + st.size).every((v) => v === 0xFF)) continue;
    out.set(rec, o);
    return { guest: out, store: st, slot: i };
  }
  throw new Error(st.label + ': all slots used, remove one first');
}
/** Empty one slot. Returns the new firmware. */
function dlcFree(g, kind, slot) {
  const dev = dlcDev(g) || 'idl';
  const st = DLC_DEV[dev].find((k) => k.kind === kind); if (!st || slot < 0 || slot >= st.n) throw new Error('no such slot');
  const out = grow(g); const o = st.base + slot * st.size;
  out.fill(0xFF, o, o + st.size);
  return out;
}
/** Put a game into a given slot (replaces what is there). Returns the new firmware. */
function dlcPut(g, rec, slot) {
  if (g.length < 0x7B0000) throw new Error('only for the 8 MB firmware');
  const out = grow(g);
  const o = GAME_BASE + slot * GAME_SIZE;
  out.fill(0xFF, o, o + GAME_SIZE); out.set(rec, o);
  return out;
}


// ------------------------------------------------------------ T7Emu (V7, 6502)
/*
 * T7Emu (1.4.0): the V7 core of the T7Emu app (after asterick/tamago, BSD) as a second
 * emulator. Same launcher and program place as TamaEmu (only one of them), settings block
 * "PMX7" at 0x140 of the program. Layout: the ROM (Tama-Go 640 KB, Friends 768 KB) at 0x940000, the figures right
 * behind it (trailing zero bytes cut off, the core reads them as zero anyway), up to
 * 0xD40000. Save area at 0x0C0000: session (4 KB), the pet's EEPROM in two slots from 0x0C1000, settings at 0x0C5000.
 */
const T7_MAGIC = 0x37584D50, T7_ROM_SIZE = 0xA0000, T7_FIG_MAX = 16, T7_EE = 0xC1000, T7_EE_END = 0xC6000;   // EEPROM slots, then the settings sector
const T7_APP_B64 = '__T7_APP_B64__';
/* V1Emu (1.6.8): the first Tamagotchis and the Digital Monster (E0C6S46/48, TamaLIB core). Program 64 KB, session 4 KB, ROM. */
const V1_APP_B64 = '__V1_APP_B64__';
const V1_MAGIC = 0x31584D50;   // "PMX1"
function v1Embedded() { return V1_APP_B64.startsWith('__') ? null : b64(V1_APP_B64); }
/* 1.6.9: V3Emu, the Tamagotchi Connection V3 (Sunplus SPLB32, core from BrickEmuPy by azya52, CC0). It has the settings
 * block of V1Emu (same magic and layout), so the Studio handles it as a V1Emu; the program is picked by the ROM. */
const GB_APP_B64 = '__GB_APP_B64__';
function gbEmbedded() { return GB_APP_B64.startsWith('__') ? null : b64(GB_APP_B64); }
/* 1.6.9 test36: GBEmu (Game Boy, DMG). Settings block "PMXG" at 0x140: magic, version, ROM, its length, save (4 KB settings,
 * then the cartridge RAM up to 32 KB), flags (bit 0 English, bit 1 wheel the other way round), name (16), keys (12 bytes). */
const GB_MAGIC = 0x47584D50;   // "PMXG"
/** keys[i] = Game Boy buttons (1 A, 2 B, 4 SELECT, 8 START, 16 RIGHT, 32 LEFT, 64 UP, 128 DOWN) for the Paradise input i:
 * 0 A, 1 B, 2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B */
const GB_KEYS_DEFAULT = [32, 2, 16, 1, 64, 128, 8, 0, 0];   // test37: A left, C right; B+C free (run to the right), SELECT in the device's settings
/** A Game Boy ROM: 32 KB to 2 MB, size code at 0x148 matching, header checksum at 0x14D right. Colour-only games are refused. */
function gbDetect(r) {
  if (!r || r.length < 0x8000 || r.length > 0x200000 || (r.length & 0x7FFF)) return false;
  if (r[0x148] > 6 || (0x8000 << r[0x148]) !== r.length) return false;
  let x = 0; for (let i = 0x134; i <= 0x14C; i++) x = (x - r[i] - 1) & 255;
  return x === r[0x14D];
}
function gbColorOnly(r) { return r[0x143] === 0xC0; }
function gbTitle(r) { let s = ''; for (let i = 0x134; i < 0x144; i++) { const c = r[i]; if (!c || c > 0x7E) break; if (c >= 0x20) s += String.fromCharCode(c); } return s.trim().toUpperCase() || 'GAME BOY'; }
function gbCfgWrite(a, c, x, romOff, saveOff, flags) {
  w32(a, c + 4, 1); w32(a, c + 8, romOff); w32(a, c + 12, x.rom.length); w32(a, c + 16, saveOff); w32(a, c + 20, (flags & 1) | (x.inv ? 2 : 0));
  const nm = gbTitle(x.rom); a.fill(0, c + 24, c + 40); for (let i = 0; i < nm.length && i < 15; i++) a[c + 24 + i] = nm.charCodeAt(i);
  const k = (x.keys && x.keys.length === 9) ? x.keys : GB_KEYS_DEFAULT; a.fill(0, c + 40, c + 52); k.forEach((v, i) => { a[c + 40 + i] = v & 255; });
}
/* 1.6.10 test2 SMSEmu (Sega Master System). Settings block "PMXS" at 0x140, laid out as GBEmu's: magic, version, ROM, its
 * length, save (4 KB settings, then the cartridge RAM, 32 KB), flags (bit 0 English, bit 1 wheel the other way round),
 * name (16, from the file name), keys (12 bytes). */
const SMS_APP_B64 = '__SMS_APP_B64__';
function smsEmbedded() { return SMS_APP_B64.startsWith('__') ? null : b64(SMS_APP_B64); }
const SMS_MAGIC = 0x53584D50;   // "PMXS"
/** keys[i] = Master System buttons (1 up, 2 down, 4 left, 8 right, 16 button 1, 32 button 2, 64 pause) for the Paradise
 * input i: 0 A, 1 B, 2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B */
const SMS_KEYS_DEFAULT = [4, 16, 8, 32, 1, 2, 64, 0, 0];
/** "TMR SEGA" header at 0x7FF0, 0x3FF0 or 0x1FF0 (a 512 byte copier header in front is allowed) */
function smsHead(r) {
  const o0 = (r.length & 0x3FFF) === 512 ? 512 : 0;
  for (const h of [0x7FF0, 0x3FF0, 0x1FF0]) {
    const o = o0 + h; if (o + 16 > r.length) continue;
    if (String.fromCharCode(...r.subarray(o, o + 8)) === 'TMR SEGA') return { off: o, region: r[o + 15] >> 4 };
  }
  return null;
}
/** A Master System ROM: 8 KB to 1 MB; by its header, or (some games have none) by the file name ending .sms */
function smsDetect(r, name) {
  if (!r) return false;
  const n = (r.length & 0x3FFF) === 512 ? r.length - 512 : r.length;
  if (n < 0x2000 || n > 0x100000 || (n & 0x1FFF)) return false;
  const h = smsHead(r);
  if (h) return h.region === 3 || h.region === 4;   // 3 SMS Japan, 4 SMS export (5..7 Game Gear)
  return /\.sms$/i.test(name || '');
}
function smsGameGear(r) { const h = smsHead(r); return !!h && h.region >= 5 && h.region <= 7; }
/** a short name from the file name: without brackets and the ending, upper case, at most 15 letters */
function smsTitle(fname) {
  let s = String(fname || '').replace(/\.[^.]*$/, '').replace(/[([].*$/, '').replace(/[^A-Za-z0-9 !'&.-]/g, ' ').toUpperCase();
  s = s.split(/\s+/).filter((w) => w && !['IN', 'OF', 'THE', 'AND', 'STARRING'].includes(w)).join(' ');
  return s.slice(0, 15).trim() || 'MASTER SYSTEM';
}
function smsCfgWrite(a, c, x, romOff, saveOff, flags) {
  w32(a, c + 4, 1); w32(a, c + 8, romOff); w32(a, c + 12, x.rom.length); w32(a, c + 16, saveOff); w32(a, c + 20, (flags & 1) | (x.inv ? 2 : 0));
  const nm = x.title || 'MASTER SYSTEM'; a.fill(0, c + 24, c + 40); for (let i = 0; i < nm.length && i < 15; i++) a[c + 24 + i] = nm.charCodeAt(i);
  const k = (x.keys && x.keys.length === 9) ? x.keys : SMS_KEYS_DEFAULT; a.fill(0, c + 40, c + 52); k.forEach((v, i) => { a[c + 40 + i] = v & 255; });
}
function cfgName(rom, c) { let s = ''; for (let i = 0; i < 15; i++) { const v = rom[c + 24 + i]; if (!v) break; s += String.fromCharCode(v); } return s; }
/* 1.6.10 test1 Wolfenstein 3D (Wolf4SDL, GPL): the data pack (wolf.js, built in the browser from the user's game
 * files) first, then the program (128 KB: the PRAM part and the part it copies to RAM itself), then the save (64 KB:
 * 4 KB settings, the saved game). Settings block "PMXW" at 0x140: magic, version, pack, its length, save, flags
 * (bit 0 English, bit 1 wheel the other way round), name (16), keys fire, forward, back, use, hold key (masks A 1,
 * B 2, C 4, wheel press 8), run with forward twice, degrees per wheel step, 0, then the program's own place. */
const WOLF_APP_B64 = '__WOLF_APP_B64__';
function wolfEmbedded() { return WOLF_APP_B64.startsWith('__') ? null : b64(WOLF_APP_B64); }
const WOLF_MAGIC = 0x57584D50;   // "PMXW"
const WOLF_KEYS_DEFAULT = { fire: 1, fwd: 2, back: 4, use: 8, mod: 4, run: true, invert: true, turn: 15 };   // 1.6.10 test1: wheel reversed by default
function wolfPackOk(p) { return !!p && p.length > 64 && p[0] === 0x57 && p[1] === 0x33 && p[2] === 0x44 && p[3] === 0x50; }
function wolfInfo(p) { return wolfPackOk(p) ? { shareware: !!(u32(p, 8) & 1), episodes: u32(p, 60), maps: u32(p, 28) } : null; }
function wolfCfgWrite(a, c, x, packOff, saveOff, progOff, flags) {
  const k = Object.assign({}, WOLF_KEYS_DEFAULT, x.keys || {});
  w32(a, c + 4, 1); w32(a, c + 8, packOff); w32(a, c + 12, x.pack.length); w32(a, c + 16, saveOff); w32(a, c + 20, (flags & 1) | (k.invert ? 2 : 0));
  const nm = 'WOLFENSTEIN 3D'; a.fill(0, c + 24, c + 40); for (let i = 0; i < nm.length; i++) a[c + 24 + i] = nm.charCodeAt(i);
  a[c + 40] = k.fire; a[c + 41] = k.fwd; a[c + 42] = k.back; a[c + 43] = k.use; a[c + 44] = k.mod; a[c + 45] = k.run ? 1 : 0; a[c + 46] = k.turn; a[c + 47] = 0;
  w32(a, c + 48, progOff);
}
function wolfKeysRead(rom, c) {
  return { fire: rom[c + 40], fwd: rom[c + 41], back: rom[c + 42], use: rom[c + 43], mod: rom[c + 44], run: !!rom[c + 45], invert: !!(u32(rom, c + 20) & 2), turn: rom[c + 46] || 15 };
}
const V3_APP_B64 = '__V3_APP_B64__';
function v3Embedded() { return V3_APP_B64.startsWith('__') ? null : b64(V3_APP_B64); }
/** ROM of the Connection V3: 512 KB, the 6502 vectors (NMI, reset, IRQ) at 0x7FFA in the top half of the address space */
function v3Detect(r) {
  if (!r || r.length !== 0x80000) return false;
  const v = (o) => r[o] | (r[o + 1] << 8);
  return v(0x7FFA) >= 0xC000 && v(0x7FFC) >= 0xC000 && v(0x7FFE) >= 0xC000;
}
/** the program for this V1Emu ROM: V3Emu for a V3 ROM */
function v1App(r, app) { return v3Detect(r) ? (app && app.v3) || v3Embedded() : app; }   // tests hang v3.bin on the V1 program as .v3
/** ROM of an E0C6S4x toy: 12 KB or 16 KB of 12-bit words (two bytes each, high byte first, top nibble 0) */
function v1Detect(r) {
  if (v3Detect(r)) return true;
  if (!r || (r.length !== 0x3000 && r.length !== 0x4000)) return false;
  for (let i = 0; i < r.length; i += 2) if (r[i] > 0x0F) return false;
  return true;
}
const V1_NAMES = { 0x1A03E02A: 'TAMA P1', 0xF0A58E57: 'TAMA P2', 0xD1BF093B: 'ANGEL', 0x02E0C792: 'GENJIN', 0x4E0B9922: 'MORINO',
  0x5E1091E3: 'TAMAOTCH', 0x0809B7FA: 'UMINO', 0xDFEA1952: 'YASASHII', 0x58200E73: 'DIGIMON V1', 0x55DF2A89: 'DIGIMON V2', 0xD32D442C: 'DIGIMON V3',
  0xEF932FA1: 'TAMA P1 EN' };   // test31: the English P1 (tama.bin), same program as the Japanese one apart from its texts   // 1.6.9 test24: Digimon V3
/* V1Emu settings block (at 0x140 of its image): ROM, save (session 4 KB, device settings 4 KB), flags, name, icons */
/* 1.6.8: toys whose LCD sits the other way round (checked on the host with every known ROM: Digimon V1/V2 and Yasashii) */
function v1Turned(rom) { const n = v1Name(rom); return n.startsWith('DIGIMON') || n === 'YASASHII'; }
function v1CfgWrite(a, c, rom, romOff, saveOff, flags) {
  w32(a, c + 4, 1); w32(a, c + 8, romOff); w32(a, c + 12, rom.length); w32(a, c + 16, saveOff);
  w32(a, c + 20, (flags & 3) | (v1Clock(rom) ? 8 : 0) | 16 | (v1Turned(rom) ? 32 : 0) | (v1Name(rom).startsWith('DIGIMON') ? 64 : 0) | (v1Name(rom) === 'YASASHII' ? 128 : 0));   // bit 7 (test31): Yasashii, segment n is column n   // bit 6: Digimon (clock at RAM 0x1A, 12 h, set with B and C; own icon segments, keys by interrupt, one-shot beeps)   // bit 1 clock, bit 3: V1Emu knows the toy's clock, bit 4: bit 1 is meant, bit 5: LCD turned (Digimon)
  const nm = v1Name(rom); a.fill(0, c + 24, c + 40); for (let i = 0; i < nm.length && i < 15; i++) a[c + 24 + i] = nm.charCodeAt(i);
  // a turned LCD lights place 7 - i for the toy's icon i: the P set goes in reversed then (Yasashii)
  v1IconsWrite(a, c, rom);
}
function v1IconsWrite(a, c, rom) {
  const nm = v1Name(rom), set = V1_ICONS[nm] || (nm.startsWith('DIGIMON') ? V1_ICONS.DIGIMON : v1Turned(rom) ? V1_ICONS.P.slice().reverse() : V1_ICONS.P); w32(a, c + 40, 1);
  set.forEach((rows, i) => rows.forEach((v, r) => { a[c + 44 + i * 32 + r * 2] = v & 255; a[c + 45 + i * 32 + r * 2] = v >> 8; }));
}
/** 1.6.8: V1Emu as the main program (A and C, start when switched on): image at 0x0A0000, ROM at 0x940000,
 * session and settings at 0x0C0000. The session stays when the same ROM was in before. */
function v1Patch(rom, v1rom, app, stubTpl, opts = {}) {
  if (!v1Detect(v1rom)) throw new Error('not a V1 ROM');
  app = v1App(v1rom, app); if (!app) throw new Error('V3Emu missing');
  if (assetEnd(rom) > 0x830000) throw new Error('the character data reaches 0x' + assetEnd(rom).toString(16) + ', the emulator needs 0x830000 onwards');
  const old = v1FromRom(rom), same = !!old && old.length === v1rom.length && old.every((v, i) => v === v1rom[i]);
  const oldTama = u32(rom, APP_OFF + 0x140) === PMXC, bigOld = oldTama && u32(rom, APP_OFF + 0x148) === BIG_OFF;
  const out = installApp(rom, app, stubTpl);
  if (bigOld) out.fill(0xFF, BIG_OFF, BIG_END);
  out.fill(0xFF, GUEST_OFF, GUEST_END); out.set(v1rom, GUEST_OFF);
  const c = APP_OFF + 0x140;
  if (u32(out, c) !== V1_MAGIC) throw new Error('settings block missing in the program');
  v1CfgWrite(out, c, v1rom, GUEST_OFF, SAVE_OFF, (opts.lang === 'en' ? 1 : 0) | (opts.clock === false ? 0 : 2));
  if (!same) out.fill(0xFF, SAVE_OFF, SAVE_OFF + 0x1000);   // the settings of the device (second 4 KB) stay
  out.fill(0xFF, SAVE_OFF + 0x2000, SAVE_END);
  return out;
}
/** The V1 ROM of V1Emu as the main program, or null. */
function v1FromRom(rom, base = APP_OFF) {
  if (u32(rom, base + 0x140) !== V1_MAGIC) return null;
  const o = u32(rom, base + 0x148), n = u32(rom, base + 0x14C);
  return o + n <= rom.length && (n === 0x3000 || n === 0x4000 || n === 0x80000) ? rom.slice(o, o + n) : null;
}
/** Newer V1Emu program as the main program: the settings block stays. null if nothing to do. */
function v1Upgrade(rom, app) {
  if (u32(rom, APP_OFF + 0x140) !== V1_MAGIC) return null;
  app = v1App(v1FromRom(rom), app); if (!app) return null;
  const a = new Uint8Array(app); if (u32(a, 0x140) !== V1_MAGIC) return null;
  a.set(rom.subarray(APP_OFF + 0x140, APP_OFF + 0x140 + 300), 0x140);
  const f = u32(a, 0x140 + 20); if (!(f & 16)) w32(a, 0x140 + 20, f | 2 | 16);
  { const r = v1FromRom(rom); if (r && v1Name(r).startsWith('DIGIMON')) w32(a, 0x140 + 20, u32(a, 0x140 + 20) | 32 | 64 | 8);   // LCD turned
    else if (r && v1Turned(r)) w32(a, 0x140 + 20, u32(a, 0x140 + 20) | 32);
    if (r) v1IconsWrite(a, 0x140, r);
    if (r && v1Name(r) === 'YASASHII') w32(a, 0x140 + 20, u32(a, 0x140 + 20) | 128);   // test31: segment = column
    if (r) { const nm = v1Name(r); a.fill(0, 0x140 + 24, 0x140 + 40); for (let i = 0; i < nm.length && i < 15; i++) a[0x140 + 24 + i] = nm.charCodeAt(i); if (v1Clock(r)) w32(a, 0x140 + 20, u32(a, 0x140 + 20) | 8); } }   // test31: a newly known ROM (tama.bin) gets its name and clock
  let same = true; for (let i = 0; i < APP_MAX && same; i++) if (rom[APP_OFF + i] !== (i < a.length ? a[i] : 0)) same = false;
  if (same) return null;
  const out = new Uint8Array(rom); out.fill(0x00, APP_OFF, APP_OFF + APP_MAX); out.set(a, APP_OFF);
  return out;
}
/* 1.6.8: the eight LCD icons (16x16, bit 15 left; 0..3 above, 4..7 below), drawn by mameise. The P1/P2 set
 * for all toys without their own set. */
const V1_ICONS = { ANGEL: [[0x0000,0x0000,0x0000,0x1C38,0x2244,0x4182,0x4422,0x500A,0x4202,0x2204,0x1108,0x0910,0x0420,0x0240,0x0180,0x0000],[0x0000,0x0000,0x0038,0x002C,0x0024,0x003C,0x0040,0x0080,0x0100,0x1A00,0x2400,0x4A00,0x5A00,0x4400,0x3800,0x0000],[0x0000,0x0100,0x0820,0x0000,0x2108,0x0280,0x1C70,0x0820,0x2548,0x06C0,0x0440,0x1010,0x0000,0x0440,0x0000,0x0000],[0x0000,0x0000,0x1FF8,0x2404,0x4202,0x5A03,0x5A03,0x5B57,0x5AAA,0x5A02,0x4202,0x2404,0x1FF8,0x0000,0x0000,0x0000],[0x0000,0x0004,0x000C,0x0008,0x01E0,0x0210,0x0788,0x0848,0x1030,0x2F20,0x30C0,0x2640,0x1080,0x0F00,0x0000,0x0000],[0x0000,0x0000,0x0000,0x1C3C,0x1C44,0x1C84,0x0304,0x0304,0x0484,0x0844,0x1024,0x1014,0x100C,0x1FFC,0x0000,0x0000],[0x0000,0x0000,0x0180,0x0420,0x0000,0x1048,0x00C0,0x01C0,0x1248,0x0240,0x0810,0x03C0,0x03C0,0x0180,0x0180,0x0000],[0x0000,0x0000,0x0700,0x0918,0x112E,0x2226,0x242C,0x3870,0x0088,0x0124,0x0F4C,0x1114,0x1AA8,0x0E70,0x0C00,0x0000]],
  DIGIMON: [[0x0000,0x07E0,0x1818,0x2084,0x4892,0x8CB1,0xA425,0xB00D,0x500A,0x4402,0x4202,0x2104,0x2004,0x13C8,0x0C30,0x0000],[0x0000,0x1800,0x29C0,0x4660,0x6430,0x1A50,0x1188,0x1148,0x2A38,0x2628,0x2546,0x18E2,0x0494,0x0318,0x0000,0x0000],[0x0800,0x0800,0x1C00,0x1C08,0x1C1C,0x1C08,0x1C08,0x1CFC,0x1C1C,0x1C1C,0x1C1C,0x1C08,0x1C14,0x0824,0x0022,0x0000],[0x0000,0x0000,0x0408,0x0E1C,0x0408,0x0808,0x1CFC,0x1C1C,0x1F9C,0x1C1C,0x1C1C,0x0808,0x1414,0x1224,0x2222,0x0000],[0x0000,0x0000,0x0000,0x0000,0x0300,0x0380,0x07C0,0x0FE0,0x07C0,0x0000,0x1FF0,0x0FE0,0x0000,0x3FF8,0x1FF0,0x0000],[0x0000,0x0240,0x0810,0x03C0,0x25A4,0x0810,0x1008,0x542A,0x1428,0x1668,0x53CA,0x0990,0x0420,0x23C4,0x07E0,0x0000],[0x0000,0x0420,0x0A50,0x1188,0x2104,0x4202,0x2404,0x1808,0x1018,0x2024,0x4042,0x2084,0x1188,0x0A50,0x0420,0x0000],[0x0000,0x0000,0x0100,0x01F0,0x03F8,0x07DC,0x0FFE,0x0FFE,0x007E,0x70FC,0x01F8,0x0BF0,0x1000,0x2000,0x0000,0x0000]],
  P: [[0x0000,0x2A0C,0x2A1C,0x2A3C,0x3E3C,0x1C3C,0x083C,0x1C3C,0x1C3C,0x1C0C,0x1C0C,0x1C0C,0x1C0C,0x1C0C,0x0000,0x0000],[0x0000,0x0000,0x0000,0x0240,0x0000,0x13C8,0x0420,0x2814,0x0A50,0x0A50,0x2994,0x0420,0x13C8,0x0000,0x03C0,0x0000],[0x0000,0x3800,0x4400,0x441C,0x443C,0x387C,0x00F8,0x01F0,0x03E0,0x07C0,0x1F80,0x1F00,0x0E00,0x0600,0x0000,0x0000],[0x0000,0x0020,0x0010,0x0128,0x0094,0x014A,0x0220,0x0410,0x0828,0x1840,0x1C80,0x1F00,0x1E00,0x2000,0x0000,0x0000],[0x0000,0x0000,0x0000,0x1C00,0x2200,0x2200,0x5218,0x2224,0x35E4,0x1224,0x1BE4,0x0808,0x0E38,0x03E0,0x0000,0x0000],[0x0000,0x0000,0x07E0,0x1818,0x2244,0x6816,0x6006,0x2204,0x2304,0x310C,0x1008,0x1008,0x0FF0,0x0000,0x0000,0x0000],[0x0000,0x0000,0x0000,0x0F80,0x1FC0,0x3FC2,0x7F85,0x7F25,0x7E55,0x7F15,0x7F89,0x3FC1,0x1FC0,0x0F80,0x0000,0x0000],[0x0000,0x0E00,0x3180,0x2080,0x4A40,0x4040,0x5170,0x2E0C,0x3004,0x0E82,0x02FA,0x0252,0x0104,0x018C,0x0070,0x0000]] };
/* 1.6.8: toys whose clock V1Emu sets (RAM nibble 0x10, see v1/src/app/main.c) */
const V1_CLOCK = ['TAMA P1', 'TAMA P1 EN', 'TAMA P2', 'ANGEL', 'MORINO', 'DIGIMON V1', 'DIGIMON V2', 'DIGIMON V3', 'TAMA V3'];   // 1.6.9: V3Emu (V3_CLOCK)
function v1Clock(r) { return V1_CLOCK.includes(v1Name(r)); }
function v1Name(r) { if (v3Detect(r)) return 'TAMA V3'; let h = 2166136261; for (const x of r) h = Math.imul(h ^ x, 16777619) >>> 0; return V1_NAMES[h] || (r.length === 0x3000 ? 'TAMA 46' : 'TAMA 48'); }
function t7Embedded() { return T7_APP_B64.startsWith('__') ? null : b64(T7_APP_B64); }
/** V7 ROM: Tama-Go (640 KB, reset vector in the static part at 0x3FFC) or Tamagotchi Friends
 * (768 KB, 16 KB more in front: reset vector at 0x7FFC, nothing at 0x3FFC). 'go', 'friends' or null. */
const T7_FRIENDS_SIZE = 0xC0000;
function t7Kind(b) {
  if (!b) return null;
  const v = (o) => b[o] | (b[o + 1] << 8);
  if (b.length === T7_ROM_SIZE && v(0x3FFC) >= 0xC000) return 'go';
  if (b.length === T7_FRIENDS_SIZE && v(0x3FFC) === 0 && v(0x7FFC) >= 0xC000) return 'friends';
  return null;
}
function t7Detect(b) { return !!t7Kind(b); }
/** Figure: "AA 55" at 1 and at 0x11. */
function t7FigDetect(b) { return !!b && b.length >= 0x40 && b[1] === 0xAA && b[2] === 0x55 && b[0x11] === 0xAA && b[0x12] === 0x55; }
/* Figure data as stored: dumps of the whole chip (the "full" figures, 4 MB) hold the 1 MB of the figure
 * four times (the address wraps) and a few bytes of text from the dump tool behind; one copy is enough.
 * Then the zeros at the end are cut. */
function t7FigData(b) {
  let n = b.length, p = 1; while (p * 2 <= n) p *= 2;
  if (n - p <= 64 && p >= 0x40000) n = p;                         // dump tool text behind the chip
  const same = (h) => { for (let i = 1; i < h; i++) if (b[i] !== b[h + i]) return false; return true; };
  while (n >= 0x80000 && (n & (n - 1)) === 0 && same(n / 2)) n /= 2;
  let e = n; while (e > 0 && b[e - 1] === 0) e--;
  return b.slice(0, Math.max(e, 0x40));
}
function t7Trim(b) { return t7FigData(b); }
/** Name for the menu on the device from the file name: "(03)_Tama-Go_Mametchi.bin" -> "Mametchi". */
function t7FigName(file) {
  let n = String(file || '').split(/[\\/]/).pop().replace(/\.[^.]*$/, '');
  n = n.replace(/^\(\d+\)[_ -]*/, '').replace(/^tama[-_ ]?go[_ -]*/i, '').replace(/[_ -]*figure$/i, ' Full').replace(/_/g, ' ');
  n = n.replace(/[^\x20-\x7E]/g, '').trim();
  return (n || 'Figur').slice(0, 15);
}
function t7Cfg(rom, base = APP_OFF) {
  if (u32(rom, base + 0x140) !== T7_MAGIC) return null;
  const c = base + 0x140, n = Math.min(u32(rom, c + 20), T7_FIG_MAX), figs = [];
  for (let i = 0; i < n; i++) {
    const f = c + 24 + i * 24; let name = '';
    for (let k = 0; k < 16 && rom[f + 8 + k]; k++) name += String.fromCharCode(rom[f + 8 + k]);
    figs.push({ off: u32(rom, f), len: u32(rom, f + 4), name });
  }
  return { version: u32(rom, c + 4), romOff: u32(rom, c + 8), romLen: u32(rom, c + 12), saveOff: u32(rom, c + 16), figs, flags: u32(rom, c + 24 + T7_FIG_MAX * 24) };
}
/** What is installed, as input for t7Patch again (to add or remove figures). */
function t7FromRom(rom) {
  const c = t7Cfg(rom); if (!c) return null;
  return { rom: rom.slice(c.romOff, c.romOff + c.romLen), figs: c.figs.map((f) => ({ name: f.name, data: rom.slice(f.off, f.off + f.len) })) };
}
/** Room the figures may still use (bytes), for the Studio's display. */
function t7Room(figs, romLen = T7_ROM_SIZE) { let a = GUEST_OFF + romLen; for (const f of figs) a = (a + f.data.length + 0xFFF) & ~0xFFF; return GUEST_END - a; }
/**
 * Install T7Emu. rom: 16 MB Paradise image, t7rom: 640 KB V7 ROM, figs: [{name, data}],
 * app: t7.bin, stubTpl: launcher. opts.lang 'en' for English texts on the device. Returns a new image.
 */
function t7Patch(rom, t7rom, figs, app, stubTpl, opts = {}) {
  if (!t7Detect(t7rom)) throw new Error('not a V7 ROM (Tama-Go 640 KB or Tamagotchi Friends 768 KB)');
  if (t7Kind(t7rom) === 'friends') figs = [];                     // Friends has no figure port
  if (figs.length > T7_FIG_MAX) throw new Error('at most ' + T7_FIG_MAX + ' figures');
  if (assetEnd(rom) > 0x830000) throw new Error('the character data reaches 0x' + assetEnd(rom).toString(16) + ', the emulator needs 0x830000 onwards');
  const oldT7 = t7Cfg(rom), oldTama = u32(rom, APP_OFF + 0x140) === PMXC;
  const sameRom = !!oldT7 && oldT7.romLen === t7rom.length && rom.subarray(oldT7.romOff, oldT7.romOff + oldT7.romLen).every((v, i) => v === t7rom[i]);
  const bigOld = oldTama && u32(rom, APP_OFF + 0x148) === BIG_OFF;
  const out = installApp(rom, app, stubTpl);
  if (!installedOrigs(rom, sceneTable(rom)) && !isFF(rom, GUEST_OFF, GUEST_END - GUEST_OFF)) throw new Error('area 0x940000 is in use');
  if (bigOld) out.fill(0xFF, BIG_OFF, BIG_END);
  out.fill(0xFF, GUEST_OFF, GUEST_END);
  out.set(t7rom, GUEST_OFF);
  let at = GUEST_OFF + t7rom.length;
  const list = [];
  for (const f of figs) {
    const d = t7Trim(f.data);
    if (at + d.length > GUEST_END) throw new Error('no room for figure ' + f.name);
    out.set(d, at); list.push({ off: at, len: d.length, name: t7FigName(f.name) });
    at = (at + d.length + 0xFFF) & ~0xFFF;
  }
  const c = APP_OFF + 0x140;
  if (u32(out, c) !== T7_MAGIC) throw new Error('settings block missing in the program');
  w32(out, c + 4, 1); w32(out, c + 8, GUEST_OFF); w32(out, c + 12, t7rom.length); w32(out, c + 16, SAVE_OFF); w32(out, c + 20, list.length);
  out.fill(0, c + 24, c + 24 + T7_FIG_MAX * 24);
  list.forEach((f, i) => { const o = c + 24 + i * 24; w32(out, o, f.off); w32(out, o + 4, f.len); for (let k = 0; k < f.name.length; k++) out[o + 8 + k] = f.name.charCodeAt(k); });
  w32(out, c + 24 + T7_FIG_MAX * 24, (opts.lang === 'en' ? 1 : 0) | (opts.clock === false ? 0 : 2));
  // save area: the pet's EEPROM stays when the same ROM was in before, the session only with the same figures
  const sameFigs = sameRom && oldT7.figs.length === list.length && oldT7.figs.every((f, i) => f.len === list[i].len && f.off === list[i].off);
  if (!sameRom) out.fill(0xFF, SAVE_OFF, SAVE_END);
  else { out.fill(0xFF, T7_EE_END, SAVE_END); if (!sameFigs) out.fill(0xFF, SAVE_OFF, T7_EE); }
  return out;
}
/**
 * Local time when writing to the device, for the first time setting in the game (T7Emu fills
 * it in). counter: the device's seconds counter, date: now. Returns a new image.
 */
function t7Wall(rom, counter, date) {
  if (u32(rom, APP_OFF + 0x140) !== T7_MAGIC) return rom;
  const out = new Uint8Array(rom), c = APP_OFF + 0x140 + 24 + T7_FIG_MAX * 24 + 4;
  w32(out, c, counter >>> 0);
  w32(out, c + 4, date.getHours() * 3600 + date.getMinutes() * 60 + date.getSeconds());
  w32(out, c + 8, (date.getFullYear() << 16) | ((date.getMonth() + 1) << 8) | date.getDate());
  return out;
}
/** Newer T7 program into a ROM that already has T7Emu: settings stay. null if nothing to do. */
function t7Upgrade(rom, app) {
  if (u32(rom, APP_OFF + 0x140) !== T7_MAGIC) return null;
  const a = new Uint8Array(app);
  if (u32(a, 0x140) !== T7_MAGIC) return null;
  const n = 24 + T7_FIG_MAX * 24 + 4;
  a.set(rom.subarray(APP_OFF + 0x140, APP_OFF + 0x140 + n), 0x140);
  let same = true;
  for (let i = 0; i < APP_MAX && same; i++) if (rom[APP_OFF + i] !== (i < a.length ? a[i] : 0)) same = false;
  if (same) return null;
  const out = new Uint8Array(rom);
  out.fill(0x00, APP_OFF, APP_OFF + APP_MAX);
  out.set(a, APP_OFF);
  return out;
}

// ------------------------------------------------------------ T18Emu (Meets/On, TG18, ARM7)
/*
 * T18Emu (1.5.0): the TG18 core (from T18Emu) with a translator from ARM/Thumb to Cortex-M3 code.
 * Same launcher and program place as TamaEmu and T7Emu (only one of them). Settings block "PMX8"
 * at 0x140 of the program. Since 1.5.1 the program has two parts: t18.bin (PRAM, copied by the
 * launcher from 0x0A0000) and t18x.bin (code run from the flash window, at T18_XIP_OFF).
 * The 8 MB firmware is stored in 64 KB blocks in free blocks of the Paradise flash, from the top
 * down (from 0xF00000, then from 0xD40000 downwards), so the bottom (from 0x830000) stays free for
 * the field saves of the menu. Empty blocks are left out. Behind them up to T18_SPARE_WANT spares
 * for the blocks the firmware writes (its save data: 6 blocks after the first start).
 * Save area at 0x0C0000: block map, clock note, session (see t18/src/app/t18app.h).
 */
const T18_MAGIC = 0x38584D50, T18_SPARE_MAX = 64, T18_SPARE_MIN = 6, T18_SPARE_WANT = 10, T18_SPARE_GROW = 15, T18_SAVE_LEN = 0x24000;   // GROW (test29): spares added in a start device, at most 15 (the sector pool counts to 254)
const T18_XIP_OFF = 0xF0000, T18_XIP_MAX = 0x10000, T18_XIP_MAGIC = 0x58383154;
/* Hot window (1.6.6): the device's cached flash window shows 1 MB. With 0xF00000..0xFFFFFF free the
 * program moves it there (flag 4) and needs its flash part a second time at T18_XIP_HOT; the blocks
 * with the firmware's code (the same in all known TG18 firmwares) and two busy picture blocks go into
 * the rest of that MB. */
const T18_HOT_BASE = 0xF00000, T18_XIP_HOT = 0xFDF000, T18F_HOTWIN = 4;
// 1.6.6 test11 (T18_HOT_V2): the blocks the TG18 firmware reads most (measured over please wait, egg and pet with
// its calls): code 0x12..0x16, 0x1A, 0x00 and the picture data 0x3F, 0x4A, 0x32, 0x40 (pet), 0x18, 0x19, 0x3B (egg).
// The first list had 0x01, 0x1B, 0x17 (hardly read) and 0x5E, 0x63 (empty in the firmware: never placed) instead.
const T18_HOT_V2 = true, T18F_HOT2 = 16;   // 8 was the list of test10
const T18_HOT_GUEST = T18_HOT_V2 ? [0x12, 0x14, 0x15, 0x13, 0x1A, 0x00, 0x16, 0x3F, 0x4A, 0x32, 0x18, 0x19, 0x3B, 0x40]
  : [0x12, 0x14, 0x15, 0x13, 0x1A, 0x00, 0x16, 0x18, 0x19, 0x01, 0x1B, 0x17, 0x5E, 0x63];
const T18_HOT_SLOTS = [0xFF, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0];
const T18_AREAS = [[0x830000, 0xD40000], [0xF00000, 0x1000000]];
const T18_APP_B64 = '__T18_APP_B64__', T18_XIP_B64 = '__T18_XIP_B64__';
/** The program: PRAM part, with the flash part as .xip */
function t18Embedded() {
  if (T18_APP_B64.startsWith('__') || T18_XIP_B64.startsWith('__')) return null;
  const a = b64(T18_APP_B64); a.xip = b64(T18_XIP_B64); return a;
}
function t18XipOk(app) { return !!app && !!app.xip && app.xip.length >= 8 && app.xip.length <= T18_XIP_MAX && u32(app.xip, 0) === T18_XIP_MAGIC; }
/** TG18 firmware: 8 MB, "SPII" header, entry 0x2000000C. */
function t18Detect(b) {
  return !!b && b.length === 0x800000 && b[0] === 0x53 && b[1] === 0x50 && b[2] === 0x49 && b[3] === 0x49 && u32(b, 4) === 0x2000000C;
}
/** Addresses the core needs (same searches as tg18_init in t18soc.c): idle counter, soft RTC, DMA table. */
function t18Addrs(fw) {
  const n = fw.length >>> 2, w = (i) => u32(fw, i * 4);
  const cnt = new Map(); let best = 0, bestc = 0;
  for (let i = 0; i + 8 < n; i++) {
    const v = w(i);
    if (v < 0xF8000000 || v >= 0xF8020000) continue;
    let h1 = false, h2 = false;
    for (let k = 1; k < 8; k++) { const x = w(i + k); if (x === v + 1) h1 = true; if (x === v + 2) h2 = true; }
    if (!h1 || !h2) continue;
    if (!cnt.has(v)) { if (cnt.size === 64) continue; cnt.set(v, 0); }
    const c = cnt.get(v) + 1; cnt.set(v, c);
    if (c > bestc) { bestc = c; best = v; }
  }
  const idle = bestc >= 40 ? (best - 0x0F) >>> 0 : 0;
  let soft = 0;
  for (let i = 0; i + 8 <= fw.length && i < 0x20000; i += 4) {
    const a1 = u32(fw, i + 4);
    if (u32(fw, i) === 0xC0040000 && a1 >= 0xF8008000 && a1 < 0xF8010000 && a1 !== 0xF800128C && (a1 & 3) === 0) { soft = a1; break; }
  }
  let dma = 0;
  for (let i = 0; i + 12 <= fw.length && i < 0x400000; i += 4) {
    const a2 = u32(fw, i + 8);
    if (u32(fw, i) === 0xD0300000 && u32(fw, i + 4) === 0x3210 && a2 >= 0xF8000000 && a2 < 0xF8020000) { dma = a2; break; }
  }
  return { idle, soft, dma, param: t18Param(fw) };
}
/** 1.6.8 (test47): the game's Param block in the SRAM (grow up). The literal pool of DispYobidashiIconInit holds it right
 * before the address of its debug text (as the Android T18Emu finds it); checked against its table for all 9 firmwares. */
function t18Param(fw) {
  const pat = [...'DispYobidashiIconInit() pYobi'].map((c) => c.charCodeAt(0));
  let s = -1;
  outer: for (let i = 0; i + pat.length <= fw.length; i++) { for (let k = 0; k < pat.length; k++) if (fw[i + k] !== pat[k]) continue outer; s = i; break; }
  if (s < 0) return 0;
  while (s > 0 && fw[s - 1] !== 0) s--;
  const addr = (0x20000000 + s) >>> 0;
  for (let p = 4; p + 4 <= fw.length; p += 1) if (u32(fw, p) === addr) { const v = u32(fw, p - 4); return v >= 0xF800C000 && v <= 0xF800F000 ? v : 0; }
  return 0;
}
function t18ParamSet(out, c, fw) { const pa = t18Param(fw); w32(out, c + 428, pa); w32(out, c + 432, pa === 0xF800D8B4 ? 1 : 0); }   // Sanrio: Flowertchi
function t18Cfg(rom, base = APP_OFF) {
  const c = base + 0x140;
  if (u32(rom, c) !== T18_MAGIC) return null;
  const map = [], spare = [], nspare = Math.min(u32(rom, c + 12), T18_SPARE_MAX);
  for (let i = 0; i < 128; i++) map.push(rom[c + 16 + 2 * i] | (rom[c + 17 + 2 * i] << 8));
  for (let i = 0; i < nspare; i++) spare.push(rom[c + 272 + 2 * i] | (rom[c + 273 + 2 * i] << 8));
  return { version: u32(rom, c + 4), saveOff: u32(rom, c + 8), map, spare, flags: u32(rom, c + 400) };
}
/** An installed T18Emu gives its blocks back (before another emulator or DOOM goes in). */
function freeT18(out) {
  const c = t18Cfg(out); if (!c) return;
  for (const b of c.map.concat(c.spare)) if (b !== 0xFFFF) out.fill(0xFF, b << 16, (b + 1) << 16);
  if (c.flags & T18F_HOTWIN) out.fill(0xFF, 0xFD0000, 0xFF0000);
  out.fill(0xFF, SAVE_OFF, SAVE_END);
}
/** ID page of the edition (0xD49000, Tropical/Glacier 0xD29000): the Paradise's save follows it */
function idPageOf(rom) { const v = u32(rom, 0xD49000); return v >= 1 && v <= 6 ? 0xD49000 : 0xD29000; }
/** Free 64 KB blocks for the firmware: empty, behind the character data. */
function t18Free(rom, launch) {
  const from = Math.max(launch ? 0x200000 : 0x830000, (assetEnd(rom) + 0xFFFF) & ~0xFFFF), list = [];   // launcher ROM (1.6.8): all room behind the setup pictures
  const areas = launch ? [[from, Math.min(0xD40000, idPageOf(rom))], T18_AREAS[1]] : T18_AREAS;
  for (const [a, e] of areas) for (let o = Math.max(a, a < 0xF00000 ? from : a); o < e; o += 0x10000) if (isFF(rom, o, 0x10000)) list.push(o >>> 16);
  return list.sort((x, y) => y - x);   // from the top down: the bottom stays free for the field saves
}
/** Lowest flash offset T18Emu uses below 0xF00000 (the field saves of the menu end there); 0 = none */
function t18Low(rom) {
  const c = t18Cfg(rom); if (!c) return 0;
  const b = c.map.concat(c.spare).filter((x) => x !== 0xFFFF && x < 0xF0);
  return b.length ? Math.min(...b) << 16 : 0xD40000;
}
/**
 * Install T18Emu. rom: 16 MB Paradise image, fw: 8 MB TG18 firmware, app: t18.bin, stubTpl: launcher.
 * opts.lang 'en'. Returns { rom, blocks, spares }.
 */
function t18Patch(rom, fw, app, stubTpl, opts = {}) {
  if (!t18Detect(fw)) throw new Error('not a TG18 firmware (8 MB)');
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB');
  // opts.extra (1.6.8): placed as an extra program (appsPlace); the program at 0x0A0000 is someone else's
  const old = opts.extra ? null : t18Cfg(rom), oldTama = !opts.extra && u32(rom, APP_OFF + 0x140) === PMXC, oldT7 = !opts.extra && u32(rom, APP_OFF + 0x140) === T7_MAGIC;
  const work = new Uint8Array(rom);
  // what an earlier emulator used becomes free again
  if (old) { for (const b of old.map) if (b !== 0xFFFF) work.fill(0xFF, b << 16, (b + 1) << 16); for (const b of old.spare) work.fill(0xFF, b << 16, (b + 1) << 16); if (old.flags & T18F_HOTWIN) work.fill(0xFF, 0xFD0000, 0xFF0000); }
  if (oldTama || oldT7) { work.fill(0xFF, GUEST_OFF, GUEST_END); if (oldTama && u32(rom, APP_OFF + 0x148) === BIG_OFF) work.fill(0xFF, BIG_OFF, BIG_END); }
  let used = []; for (let i = 0; i < 128; i++) if (!isFF(fw, i << 16, 0x10000)) used.push(i);
  let free = t18Free(work, !!opts.launch);
  if (free.length < used.length + T18_SPARE_MIN) throw new Error('not enough room: the firmware needs ' + (used.length + T18_SPARE_MIN) + ' blocks of 64 KB, ' + free.length + ' are free');
  // hot window: the whole MB at 0xF00000 free and still room enough without 0xFD/0xFE
  const hot = opts.hotWindow !== false && [...Array(16).keys()].every((i) => free.includes(0xF0 + i)) && free.length - 2 >= used.length + T18_SPARE_MIN;
  if (hot) {
    const rest = free.filter((b) => b < 0xF0);
    free = T18_HOT_SLOTS.concat(rest);
    used = T18_HOT_GUEST.filter((g) => used.includes(g)).concat(used.filter((g) => !T18_HOT_GUEST.includes(g)));
  }
  const out = opts.extra ? new Uint8Array(work) : installApp(work, app, stubTpl);
  if (opts.extra) { out.fill(0x00, APP_OFF, APP_OFF + APP_MAX); out.set(app, APP_OFF); }
  const map = new Array(128).fill(0xFFFF);
  used.forEach((g, k) => { const b = free[k]; map[g] = b; out.set(fw.subarray(g << 16, (g + 1) << 16), b << 16); });
  // spares: up to T18_SPARE_WANT, but 4 blocks (256 KB) stay free for the menu's field saves if possible
  const nsp = Math.max(T18_SPARE_MIN, Math.min(T18_SPARE_WANT, free.length - used.length - 4));
  const spare = free.slice(used.length, used.length + nsp);
  const c = APP_OFF + 0x140;
  if (u32(out, c) !== T18_MAGIC) throw new Error('settings block missing in the program');
  const ad = t18Addrs(fw);
  w32(out, c + 4, 1); w32(out, c + 8, SAVE_OFF); w32(out, c + 12, spare.length);
  map.forEach((b, i) => { out[c + 16 + 2 * i] = b & 255; out[c + 17 + 2 * i] = b >> 8; });
  out.fill(0xFF, c + 272, c + 272 + 2 * T18_SPARE_MAX);
  spare.forEach((b, i) => { out[c + 272 + 2 * i] = b & 255; out[c + 273 + 2 * i] = b >> 8; });
  w32(out, c + 404, ad.idle); w32(out, c + 408, ad.soft); w32(out, c + 412, ad.dma); w32(out, c + 428, ad.param); w32(out, c + 432, ad.param === 0xF800D8B4 ? 1 : 0);
  w32(out, c + 416, 0); w32(out, c + 420, 0); w32(out, c + 424, 0);
  // save area: block map, clock note and session belong to this firmware
  // (the stored map sector points at the blocks: same places needed, not only the same firmware)
  const same = !!old && old.map.every((b, i) => b === map[i]) && old.spare.length === spare.length && old.spare.every((b, i) => b === spare[i]) && used.every((g) => rom.subarray(old.map[g] << 16, (old.map[g] + 1) << 16).every((v, k) => v === fw[(g << 16) + k]));
  if (!same) out.fill(0xFF, SAVE_OFF, SAVE_END);
  else out.set(rom.subarray(SAVE_OFF, SAVE_OFF + T18_SAVE_LEN), SAVE_OFF);   // (installApp cleared it)
  w32(out, c + 400, optBits(opts.opt) | (opts.lang === 'en' ? 1 : 0) | (opts.clock === false ? 0 : 2) | (hot ? T18F_HOTWIN | (T18_HOT_V2 ? T18F_HOT2 : 0) : 0));
  t18PutXip(out, app);
  return { rom: out, blocks: used.length, spares: spare.length, hot };
}

/*
 * T18Emu items (1.6.8, test47): MyMeets records ("TAMAGO" files, e.g. tg18_as15003_1.txt) into the TG18 save, as the
 * Android T18Emu does it (Inhalte.kt, TamaSatz.kt; every number there was measured on the device). The save partition
 * starts at guest flash 0x5E0000 (0x220000 bytes). Areas with fixed slot size; a slot shows on the device only with
 * its record AND its counter (92 counters of 2 bits from 0x21E053: at most 3 pieces). The active room: record at
 * 0x0F0000 and the choice byte 0x21E070 = 0 (8 = built-in room). Tamapoints u16 at 0x21E020.
 */
const T18I_PART = 0x5E0000, T18I_CNT = 0x21E053, T18I_PTS = 0x21E020, T18I_WP = 0x0F0000, T18I_WPLEN = 0x4000, T18I_WPSEL = 0x21E070;
const T18I_AREAS = [
  { key: 'food', base: 0x050000, step: 0x800, n: 22, idx0: 0, arts: ['meal', 'snack'], basic: [0, 1] },
  { key: 'item', base: 0x0A0000, step: 0x2000, n: 24, idx0: 53, arts: ['item'], basic: [0] },
  { key: 'acc', base: 0x060000, step: 0x2000, n: 16, idx0: 37, arts: ['acc'], basic: [] },
];
function be16(b, o) { return (b[o] << 8) | b[o + 1]; }
function t18iArt(z) {
  switch (z >>> 0) {
    case 0x81010101: return 'meal'; case 0x81010102: return 'snack';
    case 0x81042A01: case 0x81042A03: return 'item';
    case 0x81023400: case 0x81023500: return 'acc';
    case 0x81071F00: return 'room';
  }
  if ((z >>> 16) === 0x8101) return (z & 0xFF) === 1 ? 'meal' : 'snack';   // derived, not measured (Android T18Emu)
  return '';
}
function t18iPics(b, at) {
  if (b.length < at + 2) return null;
  const cnt = be16(b, at); if (cnt < 1 || cnt > 32) return null;
  const p = at + 2; if (p + 8 > b.length) return null;
  const ln = be16(b, p), w = b[p + 2], h = b[p + 3], nf = b[p + 4];
  if (!w || !h || !nf || p + 2 + ln > b.length) return null;
  const np = (w * h) >> 1, pd = p + 2 + ln - np, te = p + 8 + 2 * nf;
  if (pd < te || pd - te > 4) return null;
  const rgba = new Uint8ClampedArray(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const by = b[pd + (i >> 1)], n = i & 1 ? by >> 4 : by & 15;
    if (n >= nf) continue;
    const v = be16(b, p + 8 + 2 * n), bl = (v >> 11) & 31, gr = (v >> 5) & 63, rt = v & 31;   // BGR565
    if (n === 0 && bl === 0 && rt === 0 && gr === 63) continue;                               // pure green: see-through
    rgba[i * 4] = rt * 255 / 31; rgba[i * 4 + 1] = gr * 255 / 63; rgba[i * 4 + 2] = bl * 255 / 31; rgba[i * 4 + 3] = 255;
  }
  return { w, h, rgba };
}
/** A TAMAGO record: name, kind, catalogue number, price, first picture. null if it is none. */
function t18iRecord(b) {
  if (!b || b.length < 0x100 || String.fromCharCode(...b.subarray(0, 6)) !== 'TAMAGO') return null;
  let file = ''; for (let i = 0; i < 20; i++) { const c = b[52 + i]; if (!c) break; if (c >= 32 && c < 127) file += String.fromCharCode(c); }
  let name = '', unk = 0;
  for (let i = 0; i < 12; i++) { const v = be16(b, 94 + 2 * i); if (!v) break; const n = v & 0xFF;
    name += n >= 0xE0 && n <= 0xF9 ? String.fromCharCode(65 + n - 0xE0) : n >= 0xC6 && n <= 0xCF ? String.fromCharCode(48 + n - 0xC6) : n === 0x6C ? ' ' : (unk++, '?'); }
  name = name.trim();
  const readable = name.length > 0 && unk * 2 <= name.length;
  const z = ((be16(b, 78) << 16) | be16(b, 80)) >>> 0;
  let pic = t18iPics(b, 0x200);
  if (!pic && b.length > 0x202) pic = t18iPics(b, 0x202 + be16(b, 0x200));   // accessories: a block of its own first
  return { bytes: b, file, name: readable ? name : file, readable, art: t18iArt(z), target: z, cat: be16(b, 90), price: be16(b, 118), pic, model: file.split('_')[0].toLowerCase() };
}
/** All TAMAGO records in a file (several can be in one). */
function t18iScan(b) {
  const out = [];
  for (let a = 0; a + 0x60 <= b.length; a++) {
    if (b[a] !== 0x54 || b[a + 1] !== 0x41 || b[a + 2] !== 0x4D || b[a + 3] !== 0x41 || b[a + 4] !== 0x47 || b[a + 5] !== 0x4F || b[a + 6] !== 0) continue;
    if (b[a + 0x4E] !== 0x81) continue;
    const len = (b[a + 0x48] << 24 | b[a + 0x49] << 16 | b[a + 0x4A] << 8 | b[a + 0x4B]) >>> 0;
    if (len < 0x60 || len > 0x1F000 || a + len > b.length) continue;
    let sum = 0; for (let i = a; i < a + len - 2; i++) sum = (sum + b[i]) & 0xFFFF;
    if (sum !== be16(b, a + len - 2)) continue;
    const r = t18iRecord(b.slice(a, a + len)); if (r) { out.push(r); a += len - 1; }
  }
  return out;
}
function t18iCount(fw, idx) { const v = fw[T18I_PART + T18I_CNT + (idx >> 2)]; return (v >> ((idx & 3) * 2)) & 3; }
function t18iSetCount(fw, idx, c) { const o = T18I_PART + T18I_CNT + (idx >> 2), s = (idx & 3) * 2; fw[o] = (fw[o] & ~(3 << s)) | ((Math.max(0, Math.min(3, c)) & 3) << s); }
/** The save exists (the game has run once): the Param block at 0x21E000 is filled */
function t18iHasSave(fw) { const o = T18I_PART + 0x21E000; for (let i = 0; i < 0x60; i++) if (fw[o + i] !== 0xFF && fw[o + i] !== 0) return true; return false; }
/** Every area with its slots (a slot counts only with record and counter); the room and the Tamapoints. */
function t18Items(fw) {
  const areas = T18I_AREAS.map((A) => {
    const slots = [];
    for (let n = 0; n < A.n; n++) {
      const o = T18I_PART + A.base + n * A.step, cnt = t18iCount(fw, A.idx0 + n);
      const rec = fw[o] === 0x54 ? t18iRecord(fw.slice(o, o + A.step)) : null;
      const basic = A.basic.includes(n) && rec && rec.cat >= 10000 && rec.cat <= 14999;   // the start items: always there
      slots.push({ n, rec: rec && cnt > 0 && !basic ? rec : null, count: cnt, basic });
    }
    return { key: A.key, slots, free: slots.filter((x) => !x.rec && !x.basic).length };
  });
  const wo = T18I_PART + T18I_WP;
  const room = fw[wo] === 0x54 && fw[T18I_PART + T18I_WPSEL] === 0 ? t18iRecord(fw.slice(wo, wo + T18I_WPLEN)) : null;
  return { areas, room, points: fw[T18I_PART + T18I_PTS] | (fw[T18I_PART + T18I_PTS + 1] << 8), save: t18iHasSave(fw) };
}
/** Put a record in: the first free slot of its area (count 1..3), a room becomes the active room. Returns { area, slot } or throws. */
function t18ItemAdd(fw, rec, count = 1) {
  if (!t18iHasSave(fw)) throw new Error('no save yet');
  if (rec.art === 'room') {
    if (rec.bytes.length > T18I_WPLEN) throw new Error('record too large');
    const o = T18I_PART + T18I_WP; fw.fill(0xFF, o, o + T18I_WPLEN); fw.set(rec.bytes, o); fw[T18I_PART + T18I_WPSEL] = 0;
    return { area: 'room', slot: 0 };
  }
  const A = T18I_AREAS.find((x) => x.arts.includes(rec.art)); if (!A) throw new Error('unknown kind');
  if (rec.bytes.length > A.step) throw new Error('record too large');
  const it = t18Items(fw).areas.find((x) => x.key === A.key);
  const s = it.slots.find((x) => !x.rec && !x.basic); if (!s) throw new Error('full');
  const o = T18I_PART + A.base + s.n * A.step; fw.fill(0xFF, o, o + A.step); fw.set(rec.bytes, o); t18iSetCount(fw, A.idx0 + s.n, count || 1);
  return { area: A.key, slot: s.n };
}
function t18ItemDel(fw, key, n) { const A = T18I_AREAS.find((x) => x.key === key); const o = T18I_PART + A.base + n * A.step; fw.fill(0xFF, o, o + A.step); t18iSetCount(fw, A.idx0 + n, 0); }
function t18ItemCount(fw, key, n, c) { const A = T18I_AREAS.find((x) => x.key === key); if (fw[T18I_PART + A.base + n * A.step] === 0x54) t18iSetCount(fw, A.idx0 + n, c); }
function t18RoomReset(fw) { const o = T18I_PART + T18I_WP; fw.fill(0xFF, o, o + T18I_WPLEN); fw[T18I_PART + T18I_WPSEL] = 8; }
function t18Points(fw, v) { v = Math.max(0, Math.min(65535, v | 0)); fw[T18I_PART + T18I_PTS] = v & 0xFF; fw[T18I_PART + T18I_PTS + 1] = v >> 8; }

/**
 * 1.6.8 (test47): changed sectors of the guest flash (items) into an installed T18Emu, the way the device keeps them:
 * a mapped block (save map "T8MP" or the settings) gets them in place; a sector of an empty block goes to its entry
 * of the sector table ("T8SS", T18_SEC_SPARE) or a new pool sector (spare block k >> 4, sector k & 15; spare blocks
 * used whole are skipped). The layout stays, only the running state (session) is dropped: the game starts from its
 * own save. Throws when there is no room.
 */
function t18RomWriteGuest(rom, fw, base = APP_OFF) {
  const c = t18Cfg(rom, base); if (!c) throw new Error('T18Emu not installed');
  const out = new Uint8Array(rom), so = c.saveOff, SROWS = 19, SOFF = so + 4 + 256 + T18_SPARE_MAX;
  const old = t18FromRom(rom, base); if (!old) throw new Error('T18Emu not complete');
  let map = c.map, used = new Array(T18_SPARE_MAX).fill(0);
  const hasMap = u32(out, so) === 0x504D3854;
  if (hasMap) { map = []; for (let i = 0; i < 128; i++) map.push(out[so + 4 + 2 * i] | (out[so + 5 + 2 * i] << 8)); for (let i = 0; i < T18_SPARE_MAX; i++) used[i] = out[so + 4 + 256 + i]; }
  const idx = new Uint8Array(128), rows = []; for (let r = 0; r < SROWS; r++) rows.push(new Uint8Array(16));
  if (hasMap && u32(out, SOFF) === 0x53533854) { for (let i = 0; i < 128; i++) idx[i] = out[SOFF + 4 + i] <= SROWS ? out[SOFF + 4 + i] : 0; for (let r = 0; r < SROWS; r++) rows[r].set(out.subarray(SOFF + 132 + 16 * r, SOFF + 148 + 16 * r)); }
  const inUse = new Set(); for (let r = 0; r < SROWS; r++) if (idx.includes(r + 1)) for (const k of rows[r]) if (k) inUse.add(k - 1);
  let tables = false;
  for (let sec = 0; sec < 0x800000; sec += 0x1000) {
    let same = true; for (let i = 0; i < 0x1000 && same; i++) if (fw[sec + i] !== old[sec + i]) same = false;
    if (same) continue;
    const b = sec >> 16, s = (sec >> 12) & 15, data = fw.subarray(sec, sec + 0x1000);
    if (map[b] !== 0xFFFF) { out.set(data, (map[b] << 16) | (s << 12)); continue; }
    let i = idx[b];
    if (!i) { let r = 0; while (r < SROWS && idx.includes(r + 1)) r++; if (r >= SROWS) throw new Error('no room for more save data'); idx[b] = i = r + 1; rows[r].fill(0); tables = true; }
    let k = rows[i - 1][s];
    if (!k) {
      let p = -1; for (let q = 0; q < Math.min(c.spare.length * 16, 254); q++) if (!used[q >> 4] && !inUse.has(q)) { p = q; break; }
      if (p < 0) throw new Error('no room for more save data');
      inUse.add(p); rows[i - 1][s] = k = p + 1; tables = true;
    }
    k--; out.set(data, (c.spare[k >> 4] << 16) | ((k & 15) << 12));
  }
  if (tables) {
    if (!hasMap) { out.fill(0xFF, so, so + 0x1000); w32(out, so, 0x504D3854); map.forEach((v, j) => { out[so + 4 + 2 * j] = v & 255; out[so + 5 + 2 * j] = v >> 8; }); for (let j = 0; j < T18_SPARE_MAX; j++) out[so + 4 + 256 + j] = used[j]; }
    w32(out, SOFF, 0x53533854); out.set(idx, SOFF + 4); for (let r = 0; r < SROWS; r++) out.set(rows[r], SOFF + 132 + 16 * r);
  }
  out.fill(0xFF, so + 0x2000, so + 0x3000);   // session header: the game starts from its own save
  return out;
}

/*
 * 1.6.8 (test47): pictures for "grow up" on the device, from the user's own files (the Studio has none). One per adult,
 * 64x64 with up to 256 colours, at 0x0E4000 behind the T18 save area ("T8PI": magic, count, then per picture: id,
 * 3 bytes 0, 256 colours RGB565, 4096 indices). The settings get its place at +436.
 */
const T18_PIC_OFF = 0xE4000, T18_PIC_END = 0xF0000, T18_PIC_SZ = 4 + 512 + 4096;
const T18_ADULTS = [[16, 'Mametchi'], [17, 'Kuchipatchi'], [20, 'Kikitchi'], [25, 'Kuromametchi'], [24, 'Gozarutchi'], [19, 'Lovelitchi'],
  [23, 'Nijifuwatchi'], [35, 'Pompomtchi'], [18, 'Memetchi'], [22, 'Watawatatchi'], [21, 'Flowertchi']];
function t18AdultFind(name) {
  const n = name.toLowerCase().replace(/\.[a-z]+$/, '').replace(/[^a-z0-9]/g, '');
  if (/^\d+$/.test(n)) { const id = +n; return T18_ADULTS.some((a) => a[0] === id) ? id : -1; }
  for (const [id, nm] of T18_ADULTS) if (n.includes(nm.toLowerCase())) return id;
  return -1;
}
/** an image { w, h, rgba } (any size) to 64x64, palette of up to 256 RGB565 colours */
function t18PicMake(img) {
  const px = new Uint16Array(64 * 64);
  for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
    const x0 = Math.floor(x * img.w / 64), x1 = Math.max(x0 + 1, Math.floor((x + 1) * img.w / 64)), y0 = Math.floor(y * img.h / 64), y1 = Math.max(y0 + 1, Math.floor((y + 1) * img.h / 64));
    let r = 0, g = 0, b = 0, n = 0;
    for (let yy = y0; yy < y1; yy++) for (let xx = x0; xx < x1; xx++) { const o = (yy * img.w + xx) * 4; r += img.rgba[o]; g += img.rgba[o + 1]; b += img.rgba[o + 2]; n++; }
    r /= n; g /= n; b /= n;
    px[y * 64 + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  }
  const reduce = [(v) => v, (v) => v & 0xF79E, (v) => v & 0xE71C, (v) => v & 0xC618];
  for (const f of reduce) {
    const pal = new Map(); let ok = true;
    for (const v of px) { const c = f(v); if (!pal.has(c)) { if (pal.size === 256) { ok = false; break; } pal.set(c, pal.size); } }
    if (!ok) continue;
    const out = new Uint8Array(T18_PIC_SZ - 4);
    for (const [c, i] of pal) { out[2 * i] = c & 255; out[2 * i + 1] = c >> 8; }
    for (let i = 0; i < 4096; i++) out[512 + i] = pal.get(f(px[i]));
    return out;
  }
  throw new Error('too many colours');
}
/** pictures (Map id -> 4608 bytes from t18PicMake) into the ROM of an installed T18Emu (main program) */
function t18PicsWrite(rom, pics) {
  const c = APP_OFF + 0x140; if (u32(rom, c) !== T18_MAGIC) return rom;
  const out = new Uint8Array(rom); out.fill(0xFF, T18_PIC_OFF, T18_PIC_END);
  const sanrio = (u32(rom, c + 432) & 1) !== 0, ids = T18_ADULTS.map((a) => a[0]).filter((id) => pics.has(id) && id !== (sanrio ? 35 : 21)).slice(0, 10);
  if (!ids.length) { w32(out, c + 436, 0); return out; }
  w32(out, T18_PIC_OFF, 0x49503854); w32(out, T18_PIC_OFF + 4, ids.length);
  ids.forEach((id, k) => { const o = T18_PIC_OFF + 16 + k * T18_PIC_SZ; out.fill(0, o, o + 4); out[o] = id; out.set(pics.get(id), o + 4); });
  w32(out, c + 436, T18_PIC_OFF);
  return out;
}
function t18PicsRead(rom) {
  const m = new Map(); if (u32(rom, APP_OFF + 0x140) !== T18_MAGIC || u32(rom, T18_PIC_OFF) !== 0x49503854) return m;
  const n = Math.min(u32(rom, T18_PIC_OFF + 4), 10);
  for (let k = 0; k < n; k++) { const o = T18_PIC_OFF + 16 + k * T18_PIC_SZ; m.set(rom[o], rom.slice(o + 4, o + T18_PIC_SZ)); }
  return m;
}
/** for showing: RGBA of a stored picture */
function t18PicRgba(p) {
  const rgba = new Uint8ClampedArray(64 * 64 * 4);
  for (let i = 0; i < 4096; i++) { const k = p[512 + i], v = p[2 * k] | (p[2 * k + 1] << 8); rgba[4 * i] = (v >> 11) * 255 / 31; rgba[4 * i + 1] = ((v >> 5) & 63) * 255 / 63; rgba[4 * i + 2] = (v & 31) * 255 / 31; rgba[4 * i + 3] = 255; }
  return { w: 64, h: 64, rgba };
}
/** The flash part of the program at T18_XIP_OFF */
function t18PutXip(out, app) {
  if (!t18XipOk(app)) throw new Error('T18Emu program incomplete (flash part missing)');
  out.fill(0xFF, T18_XIP_OFF, T18_XIP_OFF + T18_XIP_MAX);
  out.set(app.xip, T18_XIP_OFF);
  if (u32(out, APP_OFF + 0x140) === T18_MAGIC && (u32(out, APP_OFF + 0x140 + 400) & T18F_HOTWIN)) {
    out.fill(0xFF, 0xFD0000, 0xFF0000);
    out.set(app.xip, T18_XIP_HOT);
  }
}
/** Newer T18 program into a ROM that already has T18Emu: settings (block map) stay. null if nothing to do.
 * Before 1.5.1 the clock switch did not exist: it is switched on then. */
function t18Upgrade(rom, app, stubTpl) {
  if (u32(rom, APP_OFF + 0x140) !== T18_MAGIC) return null;
  // 1.6.6: a ROM without the hot window gets it when there is room. The firmware comes from the ROM as the
  // device last saw it (the block map in the save area holds the blocks the firmware wrote), so the pet stays;
  // only the running session starts anew.
  // test10: a hot window with the first block list gets the new one (same way, the pet stays)
  const fl0 = u32(rom, APP_OFF + 0x140 + 400);
  if (stubTpl && (!(fl0 & T18F_HOTWIN) || (T18_HOT_V2 && !(fl0 & T18F_HOT2)))) {
    const fw = t18FromRom(rom);
    if (fw) {
      const fl = u32(rom, APP_OFF + 0x140 + 400);
      try { const r = t18Patch(rom, fw, app, stubTpl, { lang: fl & 1 ? 'en' : 'de', clock: (fl & 2) !== 0 }); if (r.hot) return r.rom; } catch (e) { /* stays as it is */ }
    }
  }
  const a = new Uint8Array(app);
  if (u32(a, 0x140) !== T18_MAGIC) return null;
  a.set(rom.subarray(APP_OFF + 0x140, APP_OFF + 0x140 + 428), 0x140);
  { const fw = t18FromRom(rom); if (fw) t18ParamSet(a, 0x140, fw); }   // test47: the Param block (grow up)
  const hotw = (u32(rom, APP_OFF + 0x140 + 400) & T18F_HOTWIN) !== 0;
  const xipSame = t18XipOk(app) && rom.subarray(T18_XIP_OFF, T18_XIP_OFF + app.xip.length).every((v, i) => v === app.xip[i])
    && (!hotw || rom.subarray(T18_XIP_HOT, T18_XIP_HOT + app.xip.length).every((v, i) => v === app.xip[i]));
  if (!xipSame) a[0x140 + 400] |= 2;
  let same = xipSame;
  for (let i = 0; i < APP_MAX && same; i++) if (rom[APP_OFF + i] !== (i < a.length ? a[i] : 0)) same = false;
  if (same) return null;
  const out = new Uint8Array(rom);
  out.fill(0x00, APP_OFF, APP_OFF + APP_MAX);
  out.set(a, APP_OFF);
  t18PutXip(out, app);
  return out;
}
/** The TG18 firmware as the ROM holds it: the block map of the save area (blocks the firmware wrote)
 * or the one of the settings. null if it is not complete. */
function t18FromRom(rom, base = APP_OFF) {
  const c = t18Cfg(rom, base); if (!c) return null;
  let map = c.map;
  const so = c.saveOff;
  if (so + 0x1000 <= rom.length && u32(rom, so) === 0x504D3854) { map = []; for (let i = 0; i < 128; i++) map.push(rom[so + 4 + 2 * i] | (rom[so + 5 + 2 * i] << 8)); }
  const fw = new Uint8Array(0x800000).fill(0xFF);
  map.forEach((b, g) => { if (b !== 0xFFFF && b < 0x100) fw.set(rom.subarray(b << 16, (b + 1) << 16), g << 16); });
  // test45 (T18_SEC_SPARE): single 4 KB sectors of empty guest blocks from the spare blocks ("T8SS" behind the map)
  const o = so + 4 + 256 + T18_SPARE_MAX;
  if (map !== c.map && o + 132 + 16 * 19 <= rom.length && u32(rom, o) === 0x53533854) {
    for (let g = 0; g < 128; g++) {
      const i = rom[o + 4 + g]; if (!i || i > 19 || map[g] !== 0xFFFF) continue;
      for (let sct = 0; sct < 16; sct++) {
        let k = rom[o + 132 + 16 * (i - 1) + sct]; if (!k) continue; k--;
        const b = c.spare[k >> 4]; if (b === undefined || b === 0xFFFF) continue;
        const p = (b << 16) | ((k & 15) << 12); fw.set(rom.subarray(p, p + 0x1000), (g << 16) | (sct << 12));
      }
    }
  }
  return t18Detect(fw) ? fw : null;
}
/*
 * More programs (1.6.0): besides the program at 0x0A0000 (started by A+C or by the menu) the menu can
 * start more programs, each a full 64 KB PRAM image somewhere in the flash (started through the stub
 * entry at +0x0C). For now: T7Emu with its own ROM (Tama-Go or Friends), no figures. Placed from the
 * top of the emulator area (0xD40000) down, each in its own block: image (64 KB), save (24 KB), ROM.
 * The menu lists them (settings block version 5, see feld.js).
 */
const APPX_MAX = 12, T7X_SAVE = 0x6000;   // 1.6.8: 12 (menu version 8)
function t7XName(rom) { return t7Kind(rom) === 'friends' ? 'FRIENDS' : 'TAMA-GO'; }
/** Reserved by the program at 0x0A0000 although empty (spares): [from, to) ranges. */
function appReserved(rom) {
  const r = [], m = u32(rom, APP_OFF + 0x140);
  if (m === T18_MAGIC) { const c = t18Cfg(rom); for (const b of c.map.concat(c.spare)) if (b !== 0xFFFF) r.push([b << 16, (b + 1) << 16]); if (c.flags & T18F_HOTWIN) r.push([0xFD0000, 0xFF0000]); }
  if (m === PMXC && u32(rom, APP_OFF + 0x144) >= 3) { const g = u32(rom, APP_OFF + 0x148); r.push([g, Math.max(g, u32(rom, APP_OFF + 0x140 + 152))]); }
  // 1.6.8: the save areas behind the program (empty until used)
  if (m === PMXC) r.push([SAVE_OFF, SAVE_END]);
  if (m === T7_MAGIC) r.push([SAVE_OFF, SAVE_OFF + 0x6000]);
  if (m === V1_MAGIC) r.push([SAVE_OFF, SAVE_OFF + 0x2000]);
  if (m === T18_MAGIC) r.push([SAVE_OFF, SAVE_OFF + T18_SAVE_LEN], [T18_XIP_OFF, T18_XIP_OFF + T18_XIP_MAX], [T18_PIC_OFF, T18_PIC_END]);
  return r;
}
/*
 * 1.6.8: besides T7Emu the extra programs can be TamaEmu (a colour Tama firmware) and T18Emu (one only, its
 * flash part has a fixed place at T18_XIP_OFF). An extra is { rom } (T7), { kind: 'tama', guest, dev } or
 * { kind: 't18', fw }. Each brings its own 64 KB image (settings block inside) and its own save area:
 *   T7Emu:   image, save (64 KB, 24 KB used), ROM                      (one piece, as before)
 *   TamaEmu: packed firmware with room for spare pages (256 KB aligned, it is the base of the cached
 *            window), then image and run state (320 KB, like 0x0C0000..0x110000)
 *   T18Emu:  firmware blocks anywhere (block map), image, save (T18_SAVE_LEN)
 */
const TX_STATE = 0x50000, TX_SPARE = 0x90000, T18X_SAVE = (T18_SAVE_LEN + 0xFFFF) & ~0xFFFF;
const TX_NAMES = { pc: 'PLUS COLOR', idl: 'ID L', ps: 'PS', '4u': '4U+', '4u-plain': '4U' };
function extraKind(x) { return x.kind || (v1Detect(x.rom) ? 'v1' : 't7'); }
/* 1.6.8: iD L versions by their reset vector (all run as iD L): Japanese 1.0, English, 15th anniversary, Princess Spacy */
const IDL_NAMES = { 0x024CA8FE: 'ID L JP', 0x024CA942: 'ID L JP', 0x024CAA72: 'ID L', 0x024BD18A: 'ID L 15TH', 0x024C14F6: 'ID L SPACY' };
function tamaName(guest, dev) {
  if (dev === 'idl' && guest && guest.length >= 0x400004) { const n = IDL_NAMES[u32(guest, 0x400000)]; if (n) return n; }
  return TX_NAMES[dev] || 'TAMAEMU';
}
function extraName(x) { const k = extraKind(x); return k === 'tama' ? tamaName(x.guest, x.dev) : k === 't18' ? 'TAMA MEETS' : k === 'v1' ? v1Name(x.rom) : k === 'gb' ? gbTitle(x.rom) : k === 'sms' ? (x.title || 'MASTER SYSTEM') : k === 'wolf' ? 'WOLFENSTEIN 3D' : t7XName(x.rom); }
/** Rough flash use of an extra (for the overview) */
function extraSize(x) {
  const k = extraKind(x);
  if (k === 'tama') { try { return packGuest(x.guest, x.dev).blob.length + TX_SPARE + 0x10000 + TX_STATE; } catch (e) { return 0; } }
  if (k === 'v1') return v3Detect(x.rom) ? 0x92000 : 0x12000 + x.rom.length;
  if (k === 'gb' || k === 'sms') return ((x.rom.length + 0xFFFF) & ~0xFFFF) + 0x20000;
  if (k === 'wolf') return ((x.pack.length + 0xFFFF) & ~0xFFFF) + 0x30000;
  if (k === 't18') { let n = 0; for (let i = 0; i < 128; i++) if (!isFF(x.fw, i << 16, 0x10000)) n++; return (n + T18_SPARE_WANT) * 0x10000 + 0x10000 + T18X_SAVE; }
  return 0x20000 + ((x.rom.length + 0xFFFF) & ~0xFFFF) + (x.figs || []).reduce((n, f) => n + ((f.data.length + 0xFFF) & ~0xFFF), 0);
}
/**
 * Put the extra programs into rom. app: t7.bin, or { t7, tama, t18 } (the embedded programs).
 * opts: lang, clock, launch (launcher ROM: anywhere free behind the setup pictures), keep: more [from, to)
 * ranges that must stay free (DOOM's data area). Returns { rom, list: [{ src, name, off }], low }.
 */
/* 1.6.10 test3 APPS_PACK: start device: the biggest programs first, each into the highest gap that fits (before: one
 * after the other below the lowest one, gaps above stayed empty); Game Boy and Master System games apart from their
 * program. The list keeps its order. */
const APPS_PACK = true;
function appsPlace(rom, extras, app, opts = {}) {
  const apps = app instanceof Uint8Array ? { t7: app } : app;
  const out = new Uint8Array(rom), list = [], res = appReserved(out).concat(opts.keep || []);
  if (extras.length > APPX_MAX) throw new Error('at most ' + APPX_MAX + ' more programs');
  let top = GUEST_END;
  // launcher ROM (1.6.8): anywhere free between the setup pictures and 0xD40000, from the top down
  const floor = opts.launch ? Math.max(0x200000, (assetEnd(out) + 0xFFFF) & ~0xFFFF) : GUEST_OFF;
  const ceil = opts.launch ? Math.min(GUEST_END, idPageOf(out)) : GUEST_END;   // never into the Paradise's save (Tropical/Glacier: from 0xD29000)
  if (opts.launch) top = ceil;
  const fits = (at, size) => at >= floor && at + size <= ceil && isFF(out, at, size) && !res.some(([a, b]) => a < at + size && b > at);
  const find = (size, align) => { let at = ((APPS_PACK && opts.launch ? ceil : top) - size) & ~(align - 1); if (opts.launch || align > 0x10000) while (at >= floor && !fits(at, size)) at -= align; return fits(at, size) ? at : -1; };
  // start device (1.6.8): the Paradise never writes behind its save (0xF00000..0xFFFFFF); without T18Emu (it takes that
  // MB itself) programs that fit go there first
  const hiOk = !!opts.launch && !extras.some((x) => extraKind(x) === 't18') && u32(out, APP_OFF + 0x140) !== T18_MAGIC;
  const fitsHi = (at, size) => at >= 0xF00000 && at + size <= 0x1000000 && isFF(out, at, size) && !res.some(([a, b]) => a < at + size && b > at);
  const findHi = (size, align) => { if (!hiOk) return -1; let at = (0x1000000 - size) & ~(align - 1); while (at >= 0xF00000 && !fitsHi(at, size)) at -= align; return fitsHi(at, size) ? at : -1; };
  const place = (size, align) => { const h = findHi(size, align); return h >= 0 ? { at: h, hi: true } : { at: find(size, align), hi: false }; };
  // 1.6.8: no room: how much is missing for this program and the ones after it (not just the name of the one that hit it)
  const freeKB = () => { let n = 0; for (let a = floor; a + 0x10000 <= ceil; a += 0x10000) if (fits(a, 0x10000)) n++; if (hiOk) for (let a = 0xF00000; a < 0x1000000; a += 0x10000) if (fitsHi(a, 0x10000)) n++; return n * 64; };
  const noRoom = (i) => {
    let need = opts.dlApp ? 0x10000 : 0; for (let j = 0; j < extras.length; j++) if (left.has(j) && !kept.has(extras[j])) need += extraSize(extras[j]);
    const miss = Math.max(64, Math.ceil((need / 1024 - freeKB()) / 64) * 64);
    return Object.assign(new Error('no room: about ' + miss + ' KB missing'), { room: true, missing: miss });
  };
  // test2: an installed T18Emu (keepOff) stays where it is: its save grows while it runs, so placing it anew may no
  // longer fit. Its blocks are taken first; the program, its settings and the items are renewed in place below.
  const kept = new Set();
  for (const x of extras) {
    if (extraKind(x) !== 't18' || x.keepOff === undefined || !t18XipOk(apps.t18)) continue;
    const c = t18Cfg(out, x.keepOff); if (!c) continue;
    kept.add(x);
    for (const b of c.map.concat(c.spare)) if (b !== 0xFFFF) res.push([b << 16, (b + 1) << 16]);
    res.push([T18_XIP_OFF, T18_XIP_OFF + T18_XIP_MAX]); if (c.flags & T18F_HOTWIN) res.push([0xFD0000, 0xFF0000]);
    res.push([x.keepOff, x.keepOff + 0x10000], [c.saveOff, c.saveOff + T18X_SAVE]);
  }
  const pack = APPS_PACK && !!opts.launch;
  const order = extras.map((x, i) => i);
  if (pack) order.sort((a, b) => (kept.has(extras[b]) - kept.has(extras[a])) || (extraSize(extras[b]) - extraSize(extras[a])) || (a - b));
  const left = new Set(order);
  for (const xi of order) {
    const x = extras[xi];
    const k = extraKind(x), name = extraName(x);
    // 1.6.8: clock and NFC per program (x.clock / x.nfc), otherwise the common setting
    const clk = x.clock !== undefined ? x.clock !== false : opts.clock !== false, nfc = x.nfc !== undefined ? x.nfc !== false : opts.nfc !== false;
    const flags = (opts.lang === 'en' ? 1 : 0) | (clk ? 2 : 0) | (nfc ? 0 : 4) | optBits(x.opt);
    if (k !== 't7' && k !== 'v1' && k !== 'gb' && k !== 'sms' && k !== 'wolf' && !opts.launch) throw new Error(name + ': only in a start device');
    if (k === 't7') {
      if (!t7Detect(x.rom)) throw new Error('not a V7 ROM');
      if (!apps.t7) throw new Error('T7Emu missing');
      // 1.6.8: figures for the further T7Emus as well (behind the ROM, 4 KB aligned)
      const figs = t7Kind(x.rom) === 'friends' ? [] : (x.figs || []).slice(0, T7_FIG_MAX).map((f) => ({ d: t7Trim(f.data), name: t7FigName(f.name) }));
      let figLen = 0; for (const f of figs) figLen += (f.d.length + 0xFFF) & ~0xFFF;
      const size = 0x20000 + ((x.rom.length + 0xFFF) & ~0xFFF) + figLen;
      const pl = place((size + 0xFFFF) & ~0xFFFF, 0x10000), at = pl.at;
      if (at < 0) throw noRoom(xi);
      const a = new Uint8Array(0x10000); a.set(apps.t7);
      const c = 0x140;
      if (u32(a, c) !== T7_MAGIC) throw new Error('settings block missing in the program');
      w32(a, c + 4, 1); w32(a, c + 8, at + 0x20000); w32(a, c + 12, x.rom.length); w32(a, c + 16, at + 0x10000); w32(a, c + 20, figs.length);
      a.fill(0, c + 24, c + 24 + T7_FIG_MAX * 24);
      out.set(x.rom, at + 0x20000);
      let fo = at + 0x20000 + ((x.rom.length + 0xFFF) & ~0xFFF);
      figs.forEach((f, i) => { const o = c + 24 + i * 24; out.set(f.d, fo); w32(a, o, fo); w32(a, o + 4, f.d.length); for (let q = 0; q < f.name.length && q < 15; q++) a[o + 8 + q] = f.name.charCodeAt(q); fo += (f.d.length + 0xFFF) & ~0xFFF; });
      w32(a, c + 24 + T7_FIG_MAX * 24, flags);
      out.set(a, at);
      list.push({ src: xip(at), name, off: at });
      res.push([at, at + size]); if (!pl.hi) top = Math.min(top, at);
    } else if (k === 'gb') {
      // 1.6.9 test36 GBEmu: the ROM first (64 KB aligned, the base of the cached window), then program, settings, cartridge RAM
      const gb = apps.gb || gbEmbedded(); if (!gb) throw new Error('GBEmu missing');
      const ra = (x.rom.length + 0xFFFF) & ~0xFFFF, size = ra + 0x20000;
      let pl, at, po;
      if (pack) {   // test3: the game and the program (with its save) each where there is room
        pl = place(ra, 0x10000); at = pl.at; if (at < 0) throw noRoom(xi);
        res.push([at, at + ra]); if (!pl.hi) top = Math.min(top, at);
        const p2 = place(0x20000, 0x10000); po = p2.at; if (po < 0) throw noRoom(xi);
        res.push([po, po + 0x20000]); if (!p2.hi) top = Math.min(top, po);
      } else { pl = place(size, 0x10000); at = pl.at; if (at < 0) throw noRoom(xi); po = at + ra; }
      const a = new Uint8Array(0x10000); a.set(gb);
      const c = 0x140;
      if (u32(a, c) !== GB_MAGIC) throw new Error('settings block missing in the program');
      gbCfgWrite(a, c, x, at, po + 0x10000, flags);
      out.set(x.rom, at);
      out.set(a, po);
      if (x.save && x.save.length) { const sv = new Uint8Array(0x10000).fill(0xFF); sv.set(x.save.subarray(0, 0x10000)); out.set(sv, po + 0x10000); }
      else out.fill(0xFF, po + 0x10000, po + 0x20000);
      list.push({ src: xip(po), name, off: po });
      if (!pack) { res.push([at, at + size]); if (!pl.hi) top = Math.min(top, at); }
    } else if (k === 'sms') {
      // 1.6.10 test2 SMSEmu: as GBEmu (the ROM first, 64 KB aligned, then program, settings, cartridge RAM)
      const sm = apps.sms || smsEmbedded(); if (!sm) throw new Error('SMSEmu missing');
      const ra = (x.rom.length + 0xFFFF) & ~0xFFFF, size = ra + 0x20000;
      let pl, at, po;
      if (pack) {   // test3: the game and the program (with its save) each where there is room
        pl = place(ra, 0x10000); at = pl.at; if (at < 0) throw noRoom(xi);
        res.push([at, at + ra]); if (!pl.hi) top = Math.min(top, at);
        const p2 = place(0x20000, 0x10000); po = p2.at; if (po < 0) throw noRoom(xi);
        res.push([po, po + 0x20000]); if (!p2.hi) top = Math.min(top, po);
      } else { pl = place(size, 0x10000); at = pl.at; if (at < 0) throw noRoom(xi); po = at + ra; }
      const a = new Uint8Array(0x10000); a.set(sm);
      const c = 0x140;
      if (u32(a, c) !== SMS_MAGIC) throw new Error('settings block missing in the program');
      smsCfgWrite(a, c, x, at, po + 0x10000, flags);
      out.set(x.rom, at);
      out.set(a, po);
      if (x.save && x.save.length) { const sv = new Uint8Array(0x10000).fill(0xFF); sv.set(x.save.subarray(0, 0x10000)); out.set(sv, po + 0x10000); }
      else out.fill(0xFF, po + 0x10000, po + 0x20000);
      list.push({ src: xip(po), name, off: po });
      if (!pack) { res.push([at, at + size]); if (!pl.hi) top = Math.min(top, at); }
    } else if (k === 'wolf') {
      // 1.6.10 test1 Wolf3D: the pack first (64 KB aligned, the base of the cached window), then the program (128 KB), the save
      const w = apps.wolf || wolfEmbedded(); if (!w) throw new Error('Wolf3D program missing');
      if (!wolfPackOk(x.pack)) throw new Error('Wolf3D data missing');
      if (w.length > 0x20000) throw new Error('Wolf3D program too big');
      const pa = (x.pack.length + 0xFFFF) & ~0xFFFF, size = pa + 0x30000, pl = place(size, 0x10000), at = pl.at;
      if (at < 0) throw noRoom(xi);
      const a = new Uint8Array(0x20000).fill(0xFF); a.set(w);
      const c = 0x140, po = at + pa;
      if (u32(a, c) !== WOLF_MAGIC) throw new Error('settings block missing in the program');
      wolfCfgWrite(a, c, x, at, po + 0x20000, po, flags);
      out.fill(0xFF, at, at + pa); out.set(x.pack, at);
      out.set(a, po);
      if (x.save && x.save.length) { const sv = new Uint8Array(0x10000).fill(0xFF); sv.set(x.save.subarray(0, 0x10000)); out.set(sv, po + 0x20000); }
      else out.fill(0xFF, po + 0x20000, po + 0x30000);
      list.push({ src: xip(po), name, off: po });
      res.push([at, at + size]); if (!pl.hi) top = Math.min(top, at);
    } else if (k === 'v1' && v3Detect(x.rom)) {
      // 1.6.9 V3Emu: the ROM first (64 KB aligned, the base of the cached window), then program, session, settings
      const v3 = apps.v3 || (apps.v1 && apps.v1.v3) || v3Embedded(); if (!v3) throw new Error('V3Emu missing');
      const size = 0x92000, pl = place(size, 0x10000), at = pl.at;
      if (at < 0) throw noRoom(xi);
      const a = new Uint8Array(0x10000); a.set(v3);
      const c = 0x140, po = at + 0x80000;
      if (u32(a, c) !== V1_MAGIC) throw new Error('settings block missing in the program');
      v1CfgWrite(a, c, x.rom, at, po + 0x10000, flags);
      for (let i = 0; i < 16; i++) a[c + 24 + i] = i < name.length ? name.charCodeAt(i) : 0;
      out.set(x.rom, at);
      out.set(a, po);
      out.fill(0xFF, po + 0x10000, po + 0x12000);
      list.push({ src: xip(po), name, off: po });
      res.push([at, at + size]); if (!pl.hi) top = Math.min(top, at);
    } else if (k === 'v1') {
      if (!apps.v1) throw new Error('V1Emu missing');
      const size = 0x12000 + ((x.rom.length + 0xFFF) & ~0xFFF);   // program, session 4 KB, settings 4 KB, ROM
      const pl = place(size, 0x1000), at = pl.at;
      if (at < 0) throw noRoom(xi);
      const a = new Uint8Array(0x10000); a.set(apps.v1);
      const c = 0x140;
      if (u32(a, c) !== V1_MAGIC) throw new Error('settings block missing in the program');
      v1CfgWrite(a, c, x.rom, at + 0x12000, at + 0x10000, flags);
      for (let i = 0; i < 16; i++) a[c + 24 + i] = i < name.length ? name.charCodeAt(i) : 0;
      out.set(a, at);
      out.fill(0xFF, at + 0x10000, at + 0x12000);
      out.set(x.rom, at + 0x12000);
      list.push({ src: xip(at), name, off: at });
      res.push([at, at + size]); if (!pl.hi) top = Math.min(top, at);
    } else if (k === 'tama') {
      if (!apps.tama) throw new Error('TamaEmu missing');
      const { map, blob, pages } = packGuest(x.guest, x.dev);
      const gLen = (blob.length + TX_SPARE + 0xFFFF) & ~0xFFFF, size = gLen + 0x10000 + TX_STATE;
      const pl = place(size, 0x40000), gOff = pl.at;
      if (gOff < 0) throw noRoom(xi);
      const gEnd = gOff + gLen, img = gEnd, st = gEnd + 0x10000;
      out.set(blob, gOff);
      if (pages.length) putPages(out, gOff, gEnd, blob.length, pages);
      const a = new Uint8Array(apps.tama.length > 0x10000 ? 0 : 0x10000); a.set(apps.tama);
      let ci = -1; for (let i = 0; i + 4 <= a.length; i += 4) if (u32(a, i) === PMXC) { ci = i; break; }
      if (ci < 0 || u32(a, ci + 156) !== 0) throw new Error('settings block missing in the program');
      w32(a, ci + 4, 4); w32(a, ci + 8, gOff); w32(a, ci + 12, blob.length); w32(a, ci + 16, st); w32(a, ci + 20, DEVICES[x.dev].kind);
      a.set(map, ci + 24); w32(a, ci + 152, gEnd); w32(a, ci + 156, flags);
      out.set(a, img);
      list.push({ src: xip(img), name, off: img });
      res.push([gOff, gOff + size]); if (!pl.hi) top = Math.min(top, gOff);
    } else if (k === 't18' && kept.has(x)) {
      const off = x.keepOff, a = new Uint8Array(0x10000); a.set(apps.t18);
      if (u32(a, 0x140) !== T18_MAGIC) throw new Error('settings block missing in the program');
      // its block map, spares and save place stay (up to +400); the addresses come from its firmware again (an older
      // program had code where +428.. is now: test2 copied that and grow up wrote into nowhere)
      a.set(out.subarray(off + 0x140, off + 0x140 + 400), 0x140);
      const hot = u32(out, off + 0x140 + 400) & (T18F_HOTWIN | T18F_HOT2), ad = t18Addrs(x.fw);
      w32(a, 0x140 + 404, ad.idle); w32(a, 0x140 + 408, ad.soft); w32(a, 0x140 + 412, ad.dma);
      w32(a, 0x140 + 416, 0); w32(a, 0x140 + 420, 0); w32(a, 0x140 + 424, 0);
      w32(a, 0x140 + 428, ad.param); w32(a, 0x140 + 432, ad.param === 0xF800D8B4 ? 1 : 0); w32(a, 0x140 + 436, 0);
      w32(a, 0x140 + 400, hot | optBits(x.opt) | (opts.lang === 'en' ? 1 : 0) | (clk ? 2 : 0));
      out.set(a, off);
      out.fill(0xFF, T18_XIP_OFF, T18_XIP_OFF + T18_XIP_MAX); out.set(apps.t18.xip, T18_XIP_OFF);
      if (hot & T18F_HOTWIN) { out.fill(0xFF, 0xFD0000, 0xFF0000); out.set(apps.t18.xip, T18_XIP_HOT); }
      if (x.items) out.set(t18RomWriteGuest(out, x.fw, off));
      list.push({ src: xip(off), name, off });
    } else if (k === 't18') {
      if (!t18XipOk(apps.t18)) throw new Error('T18Emu missing');
      if (u32(out, APP_OFF + 0x140) === T18_MAGIC || list.some((l) => u32(out, l.off + 0x140) === T18_MAGIC)) throw new Error('only one T18Emu');
      if (res.some(([a, b]) => a < T18_XIP_OFF + T18_XIP_MAX && b > T18_XIP_OFF) || !isFF(out, T18_XIP_OFF, T18_XIP_MAX)) throw new Error('T18Emu needs the area at 0x0F0000, the main program uses it: choose T18Emu in the Emulator tab');
      // T18Emu's own placement in a scratch copy where everything taken is not empty
      const work = new Uint8Array(out);
      for (const [a, b] of res) work.fill(0, Math.max(0, a), Math.min(work.length, b));
      const img = find(0x10000 + T18X_SAVE, 0x10000);
      if (img < 0) throw noRoom(xi);
      work.fill(0, img, img + 0x10000 + T18X_SAVE);
      const r = t18Patch(work, x.fw, apps.t18, null, { lang: opts.lang, clock: clk, launch: true, extra: true, opt: x.opt });
      const w2 = r.rom, c = APP_OFF + 0x140;
      const a = w2.slice(APP_OFF, APP_OFF + 0x10000);
      w32(a, 0x140 + 8, img + 0x10000);   // its own save area
      const blocks = [];
      for (let i = 0; i < 128; i++) { const b = w2[c + 16 + 2 * i] | (w2[c + 17 + 2 * i] << 8); if (b !== 0xFFFF) blocks.push(b); }
      const ns = Math.min(u32(w2, c + 12), T18_SPARE_MAX);
      for (let i = 0; i < ns; i++) blocks.push(w2[c + 272 + 2 * i] | (w2[c + 273 + 2 * i] << 8));
      for (const b of blocks) { out.set(w2.subarray(b << 16, (b + 1) << 16), b << 16); res.push([b << 16, (b + 1) << 16]); }
      out.set(w2.subarray(T18_XIP_OFF, T18_XIP_OFF + T18_XIP_MAX), T18_XIP_OFF); res.push([T18_XIP_OFF, T18_XIP_OFF + T18_XIP_MAX]);
      if (u32(w2, c + 400) & T18F_HOTWIN) { out.set(w2.subarray(0xFD0000, 0xFF0000), 0xFD0000); res.push([0xFD0000, 0xFF0000]); }
      out.fill(0xFF, img + 0x10000, img + 0x10000 + T18X_SAVE);
      out.set(a, img);
      list.push({ src: xip(img), name, off: img });
      res.push([img, img + 0x10000 + T18X_SAVE]); top = Math.min(top, img);
    } else throw new Error('unknown program');
    list[list.length - 1].xi = xi; left.delete(xi);
  }
  list.sort((a, b) => a.xi - b.xi); for (const l of list) delete l.xi;
  // start device (1.6.8): the flash app as its own 64 KB image (menu entry DOWNLOAD)
  let dl = 0;
  if (opts.dlApp) {
    if (opts.dlApp.length > 0x10000) throw new Error('flash app too big');
    const pl = place(0x10000, 0x10000), at = pl.at;
    if (at < 0) throw noRoom(extras.length);
    out.fill(0x00, at, at + 0x10000); out.set(opts.dlApp, at);
    dl = xip(at); res.push([at, at + 0x10000]); if (!pl.hi) top = Math.min(top, at);
  }
  // test29: T18Emu gets more spares where there is room, also an installed one (they are only added behind its
  // spares, so its save stays valid). With 6 the firmware's save filled them while growing up and its writes got lost.
  if (opts.t18More !== false) for (const l of list) {
    const c = l.off + 0x140; if (u32(out, c) !== T18_MAGIC) continue;
    const ns = Math.min(u32(out, c + 12), T18_SPARE_MAX); if (ns >= T18_SPARE_GROW) continue;
    const taken = (o) => res.some(([a, b]) => o < b && o + 0x10000 > a);
    const cand = t18Free(out, !!opts.launch).filter((b) => !taken(b << 16));
    const add = Math.min(T18_SPARE_GROW - ns, cand.length - 4);
    for (let i = 0; i < add; i++) { const b = cand[i]; out[c + 272 + 2 * (ns + i)] = b & 255; out[c + 273 + 2 * (ns + i)] = b >> 8; res.push([b << 16, (b + 1) << 16]); if (b < 0xF0) top = Math.min(top, b << 16); }
    if (add > 0) w32(out, c + 12, ns + add);
  }
  return { rom: out, list, low: top, dl };
}
/** Settings of an extra program at off: { kind, ... } or null */
function extraCfg(rom, off) {
  const m = u32(rom, off + 0x140);
  if (m === T7_MAGIC) { const ro = u32(rom, off + 0x148), rl = u32(rom, off + 0x14C); return ro + rl <= rom.length ? { kind: 't7', romOff: ro, romLen: rl, saveOff: u32(rom, off + 0x150) } : null; }
  if (m === T18_MAGIC) return Object.assign({ kind: 't18' }, t18Cfg(rom, off));
  if (m === GB_MAGIC) { const ro = u32(rom, off + 0x148), rl = u32(rom, off + 0x14C); return ro + rl <= rom.length ? { kind: 'gb', romOff: ro, romLen: rl, saveOff: u32(rom, off + 0x150) } : null; }
  if (m === SMS_MAGIC) { const ro = u32(rom, off + 0x148), rl = u32(rom, off + 0x14C); return ro + rl <= rom.length ? { kind: 'sms', romOff: ro, romLen: rl, saveOff: u32(rom, off + 0x150) } : null; }
  if (m === WOLF_MAGIC) { const ro = u32(rom, off + 0x148), rl = u32(rom, off + 0x14C); return ro + rl <= rom.length ? { kind: 'wolf', romOff: ro, romLen: rl, saveOff: u32(rom, off + 0x150) } : null; }
  if (m === V1_MAGIC) { const ro = u32(rom, off + 0x148), rl = u32(rom, off + 0x14C); return ro + rl <= rom.length ? { kind: 'v1', romOff: ro, romLen: rl, saveOff: u32(rom, off + 0x150) } : null; }
  if (m === PMXC && u32(rom, off + 0x144) >= 3) {
    const gOff = u32(rom, off + 0x148), gLen = u32(rom, off + 0x14C), saveOff = u32(rom, off + 0x150), kind = u32(rom, off + 0x154);
    const dev = Object.keys(DEVICES).find((d) => DEVICES[d].kind === kind);
    return dev ? { kind: 'tama', dev, guestOff: gOff, guestSize: gLen, saveOff, map: rom.slice(off + 0x158, off + 0x1D8), spareEnd: u32(rom, off + 0x140 + 152) } : null;
  }
  return null;
}
/** Free what the extra programs listed (from appsRead) use. */
function appsClear(rom, old) {
  const out = new Uint8Array(rom);
  for (const x of old || []) {
    const c = extraCfg(out, x.off); if (!c) continue;
    if (c.kind === 't7') { let e = x.off + 0x20000 + ((c.romLen + 0xFFFF) & ~0xFFFF); const tc = t7Cfg(out, x.off); if (tc) for (const f of tc.figs) e = Math.max(e, (f.off + f.len + 0xFFF) & ~0xFFF); out.fill(0xFF, x.off, e); }
    else if (c.kind === 'v1') { out.fill(0xFF, x.off, x.off + 0x12000); out.fill(0xFF, c.romOff, c.romOff + ((c.romLen + 0xFFF) & ~0xFFF)); }   // 1.6.9: V3Emu has its ROM in front
    else if (c.kind === 'gb' || c.kind === 'sms') { out.fill(0xFF, c.romOff, c.romOff + ((c.romLen + 0xFFFF) & ~0xFFFF)); out.fill(0xFF, x.off, x.off + 0x10000); out.fill(0xFF, c.saveOff, c.saveOff + 0x10000); }   // test3: may lie apart
    else if (c.kind === 'wolf') { out.fill(0xFF, c.romOff, c.saveOff + 0x10000); }
    else if (c.kind === 'tama') { out.fill(0xFF, c.guestOff, c.spareEnd); out.fill(0xFF, x.off, x.off + 0x10000 + TX_STATE); }
    else if (c.kind === 't18') {
      for (const b of c.map.concat(c.spare)) if (b !== 0xFFFF) out.fill(0xFF, b << 16, (b + 1) << 16);
      if (c.flags & T18F_HOTWIN) out.fill(0xFF, 0xFD0000, 0xFF0000);
      out.fill(0xFF, T18_XIP_OFF, T18_XIP_OFF + T18_XIP_MAX);
      out.fill(0xFF, x.off, x.off + 0x10000); if (c.saveOff === x.off + 0x10000) out.fill(0xFF, c.saveOff, c.saveOff + T18X_SAVE);
    }
  }
  return out;
}
/** The extra programs of a ROM, from the menu's list ([{ src, name }]): the sources again, as appsPlace takes them. */
function appsRead(rom, apps) {
  const out = [];
  for (const e of apps || []) {
    const off = e.src - 0x10000000 + XIP_DEC;
    if (off < 0 || off + 0x10000 > rom.length) continue;
    const c = extraCfg(rom, off); if (!c) continue;
    if (c.kind === 't7') { const tc = t7Cfg(rom, off); out.push({ rom: rom.slice(c.romOff, c.romOff + c.romLen), figs: tc ? tc.figs.map((f) => ({ name: f.name, data: rom.slice(f.off, f.off + f.len) })) : [], name: e.name, off, saveOff: c.saveOff, clock: !tc || !!(tc.flags & 2) }); }
    else if (c.kind === 'v1') out.push({ kind: 'v1', rom: rom.slice(c.romOff, c.romOff + c.romLen), name: e.name, off, saveOff: c.saveOff, clock: (u32(rom, off + 0x140 + 20) & 16) ? !!(u32(rom, off + 0x140 + 20) & 2) : true });   // bit 4: the clock bit is meant (test26..28 lost it): else on
    else if (c.kind === 'gb') out.push({ kind: 'gb', rom: rom.slice(c.romOff, c.romOff + c.romLen), name: e.name, off, saveOff: c.saveOff, keys: Array.from(rom.subarray(off + 0x140 + 40, off + 0x140 + 49)), inv: !!(u32(rom, off + 0x140 + 20) & 2), save: rom.slice(c.saveOff, c.saveOff + 0x10000) });
    else if (c.kind === 'sms') out.push({ kind: 'sms', rom: rom.slice(c.romOff, c.romOff + c.romLen), title: cfgName(rom, off + 0x140) || 'MASTER SYSTEM', name: e.name, off, saveOff: c.saveOff, keys: Array.from(rom.subarray(off + 0x140 + 40, off + 0x140 + 49)), inv: !!(u32(rom, off + 0x140 + 20) & 2), save: rom.slice(c.saveOff, c.saveOff + 0x10000) });
    else if (c.kind === 'wolf') out.push({ kind: 'wolf', pack: rom.slice(c.romOff, c.romOff + c.romLen), name: e.name, off, saveOff: c.saveOff, keys: wolfKeysRead(rom, off + 0x140), save: rom.slice(c.saveOff, c.saveOff + 0x10000) });
    else if (c.kind === 'tama') {
      const g = new Uint8Array(DEVICES[c.dev].size).fill(0xFF);
      for (let b = 0; b < (g.length >> 16); b++) { let k = c.map[b]; if (k === 0xFF) continue; if (k === 0xFE) k = b; g.set(rom.subarray(c.guestOff + (k << 16), c.guestOff + (k << 16) + 0x10000), b << 16); }
      const fl = u32(rom, off + 0x140 + 156);
      out.push({ kind: 'tama', dev: c.dev, guest: g, name: e.name, off, saveOff: c.saveOff, clock: !!(fl & 2), nfc: !(fl & 4), opt: optOf(fl) });
    } else if (c.kind === 't18') { const fw = t18FromRom(rom, off); if (fw) out.push({ kind: 't18', fw, name: e.name, off, saveOff: c.saveOff, clock: !!(u32(rom, off + 0x140 + 400) & 2), opt: optOf(u32(rom, off + 0x140 + 400)) }); }
  }
  return out;
}
/** Clock switch of all three emulators (1.5.1): at every start the guest clock is set to the Paradise's time,
 * nothing is caught up (true), or the old way (false). null: no emulator, or a TamaEmu program too old. */
function emuClockAt(rom) {
  const m = u32(rom, APP_OFF + 0x140);
  if (m === T18_MAGIC) return APP_OFF + 0x140 + 400;
  if (m === T7_MAGIC) return APP_OFF + 0x140 + 24 + T7_FIG_MAX * 24;
  if (m === PMXC && u32(rom, APP_OFF + 0x144) >= 4) return APP_OFF + 0x140 + 156;
  return -1;
}
/* 1.6.8: TamaEmu and T18Emu: volume (0 normal, 1 quiet, 2 loud, 3 off), brightness (0 100 %, 1 75, 2 50, 3 25 %),
 * screen off after (0 like the toy, 1..7 = 10 s, 20 s, 30 s, 1, 2, 5, 10 min) in the flags, bits 16-17, 18-19, 20-22 */
const OPT_MASK = 0xF7F0000;
// 1.6.8 (test45): dim = light step 0..10 (OPT_LV: 100, 5, 10, 20 .. 90 %), bits 24-27 = step + 1; 0 there = the old bits 18-19
const OPT_LV = [100, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90], OPT_OLD = [0, 8, 6, 3];
function optBits(o) { return o ? ((o.vol & 3) << 16) | (o.dim ? (((o.dim % 11) + 1) << 24) : 0) | ((o.off & 7) << 20) : 0; }
function optOf(f) { const n = (f >>> 24) & 15; return { vol: (f >> 16) & 3, dim: n && n <= 11 ? n - 1 : OPT_OLD[(f >> 18) & 3], off: (f >> 20) & 7 }; }
function emuOptGet(rom) { const m = u32(rom, APP_OFF + 0x140); if (m !== T18_MAGIC && m !== PMXC) return null; const a = emuClockAt(rom); return a < 0 ? null : optOf(u32(rom, a)); }
function emuOptSet(rom, o) { const m = u32(rom, APP_OFF + 0x140); if (m !== T18_MAGIC && m !== PMXC) return rom; const a = emuClockAt(rom); if (a < 0) return rom; const out = new Uint8Array(rom); w32(out, a, (u32(out, a) & ~OPT_MASK) | optBits(o)); return out; }
function emuClockGet(rom) { const a = emuClockAt(rom); return a < 0 ? null : !!(u32(rom, a) & 2); }
/** 4U/4U+ (1.6.8): NFC chip stand-in on (true, default) or off (flags bit 2). null without a 4U TamaEmu. */
function emuNfcGet(rom) { if (u32(rom, APP_OFF + 0x140) !== PMXC || u32(rom, APP_OFF + 0x144) < 4) return null; const k = u32(rom, APP_OFF + 0x140 + 20); if (k !== 3 && k !== 4) return null; return !(u32(rom, APP_OFF + 0x140 + 156) & 4); }
function emuNfcSet(rom, on) {
  if (emuNfcGet(rom) === null || emuNfcGet(rom) === !!on) return rom;
  const out = new Uint8Array(rom), a = APP_OFF + 0x140 + 156; w32(out, a, (u32(out, a) & ~4) | (on ? 0 : 4)); return out;
}
function emuClockSet(rom, on) {
  const a = emuClockAt(rom); if (a < 0 || !!(u32(rom, a) & 2) === !!on) return rom;
  const out = new Uint8Array(rom); w32(out, a, (u32(out, a) & ~2) | (on ? 2 : 0)); return out;
}
/** Clock switch of T18Emu: at every start the Paradise's time (true) or the game time caught up (false). */
function t18ClockGet(rom) { return u32(rom, APP_OFF + 0x140) === T18_MAGIC ? !!(u32(rom, APP_OFF + 0x140 + 400) & 2) : null; }
function t18ClockSet(rom, on) {
  if (u32(rom, APP_OFF + 0x140) !== T18_MAGIC) return rom;
  const out = new Uint8Array(rom), c = APP_OFF + 0x140 + 400;
  w32(out, c, (u32(out, c) & ~2) | (on ? 2 : 0));
  return out;
}
/** Local time when writing to the device (first start without a clock note). */
function t18Wall(rom, counter, date) {
  if (u32(rom, APP_OFF + 0x140) !== T18_MAGIC) return rom;
  const out = new Uint8Array(rom), c = APP_OFF + 0x140 + 416;
  w32(out, c, counter >>> 0);
  w32(out, c + 4, date.getHours() * 3600 + date.getMinutes() * 60 + date.getSeconds());
  w32(out, c + 8, (date.getFullYear() << 16) | ((date.getMonth() + 1) << 8) | date.getDate());
  return out;
}

const EMU_APP_B64 = '__EMU_APP_B64__', EMU_STUB_B64 = '__EMU_STUB_B64__';
function embedded() {
  if (EMU_APP_B64.startsWith('__')) return null;
  return { app: b64(EMU_APP_B64), stub: b64(EMU_STUB_B64) };
}

root.ParamixEmu = { smsEmbedded, smsDetect, smsGameGear, smsTitle, SMS_MAGIC, SMS_KEYS_DEFAULT, wolfEmbedded, wolfInfo, wolfPackOk, WOLF_MAGIC, WOLF_KEYS_DEFAULT, gbEmbedded, gbDetect, gbColorOnly, gbTitle, GB_MAGIC, GB_KEYS_DEFAULT, emuOptGet, emuOptSet, v1Embedded, v3Embedded, v3Detect, v1Detect, v1Name, v1Clock, v1Patch, v1FromRom, v1Upgrade, V1_MAGIC, installApp, dlcStores, dlcRoute, dlcAdd, dlcFree, dlcSlots, dlcRecord, dlcPut, dlcName, emuDetect, sceneTable, packGuest, emuPatch, emuUpgrade, emuRemove, t7Embedded, t7Detect, t7Kind, appsPlace, appsRead, appsClear, appReserved, extraKind, extraName, extraSize, extraCfg, t7XName, T7X_SAVE, APPX_MAX, t7FigDetect, t7FigData, t7FigName, t7Cfg, t7FromRom, t7Room, t7Patch, t7Upgrade, t7Wall, T7_MAGIC, t18Embedded, t18Detect, t18FromRom, t18Items, t18ItemAdd, t18ItemDel, t18ItemCount, t18RoomReset, t18Points, t18iScan, t18RomWriteGuest, T18_ADULTS, t18AdultFind, t18PicMake, t18PicsWrite, t18PicsRead, t18PicRgba, t18Addrs, t18Cfg, t18Free, t18Low, t18Patch, t18Upgrade, t18Wall, t18ClockGet, t18ClockSet, emuClockGet, emuClockSet, tamaName, emuNfcGet, emuNfcSet, T18_MAGIC, T18_XIP_OFF, T18_SAVE_LEN, OPT_LV, autoGet, autoGetOld, autoSet, STUB_OFF, installedOrigs, embedded, DEVICES, GUEST_OFF, BIG_OFF };
if (typeof module !== 'undefined') module.exports = root.ParamixEmu;
})(typeof window !== 'undefined' ? window : globalThis);
