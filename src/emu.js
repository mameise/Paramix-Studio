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
function installApp(rom, app, stubTpl) {
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB');
  const out = new Uint8Array(rom);
  const tab = sceneTable(out);
  if (!tab.PSID_HOME) throw new Error('home screen not found');
  const prev = installedOrigs(out, tab);
  if (!prev && !isFF(out, APP_OFF, 0x11000)) throw new Error('area 0x0A0000 is in use');
  if (app.length > APP_MAX) throw new Error('program too big');
  freeT18(out);
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
function legacyStubs(stubs) {
  const out = [];
  for (const [off] of stubs) for (const t of STUB_127) out.push([off, b64(t)]);
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
function t7Cfg(rom) {
  if (u32(rom, APP_OFF + 0x140) !== T7_MAGIC) return null;
  const c = APP_OFF + 0x140, n = Math.min(u32(rom, c + 20), T7_FIG_MAX), figs = [];
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
const T18_MAGIC = 0x38584D50, T18_SPARE_MAX = 64, T18_SPARE_MIN = 6, T18_SPARE_WANT = 10, T18_SAVE_LEN = 0x24000;
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
  return { idle, soft, dma };
}
function t18Cfg(rom) {
  const c = APP_OFF + 0x140;
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
/** Free 64 KB blocks for the firmware: empty, behind the character data. */
function t18Free(rom) {
  const from = Math.max(0x830000, (assetEnd(rom) + 0xFFFF) & ~0xFFFF), list = [];
  for (const [a, e] of T18_AREAS) for (let o = Math.max(a, a < 0xF00000 ? from : a); o < e; o += 0x10000) if (isFF(rom, o, 0x10000)) list.push(o >>> 16);
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
  const old = t18Cfg(rom), oldTama = u32(rom, APP_OFF + 0x140) === PMXC, oldT7 = u32(rom, APP_OFF + 0x140) === T7_MAGIC;
  const work = new Uint8Array(rom);
  // what an earlier emulator used becomes free again
  if (old) { for (const b of old.map) if (b !== 0xFFFF) work.fill(0xFF, b << 16, (b + 1) << 16); for (const b of old.spare) work.fill(0xFF, b << 16, (b + 1) << 16); if (old.flags & T18F_HOTWIN) work.fill(0xFF, 0xFD0000, 0xFF0000); }
  if (oldTama || oldT7) { work.fill(0xFF, GUEST_OFF, GUEST_END); if (oldTama && u32(rom, APP_OFF + 0x148) === BIG_OFF) work.fill(0xFF, BIG_OFF, BIG_END); }
  let used = []; for (let i = 0; i < 128; i++) if (!isFF(fw, i << 16, 0x10000)) used.push(i);
  let free = t18Free(work);
  if (free.length < used.length + T18_SPARE_MIN) throw new Error('not enough room: the firmware needs ' + (used.length + T18_SPARE_MIN) + ' blocks of 64 KB, ' + free.length + ' are free');
  // hot window: the whole MB at 0xF00000 free and still room enough without 0xFD/0xFE
  const hot = opts.hotWindow !== false && [...Array(16).keys()].every((i) => free.includes(0xF0 + i)) && free.length - 2 >= used.length + T18_SPARE_MIN;
  if (hot) {
    const rest = free.filter((b) => b < 0xF0);
    free = T18_HOT_SLOTS.concat(rest);
    used = T18_HOT_GUEST.filter((g) => used.includes(g)).concat(used.filter((g) => !T18_HOT_GUEST.includes(g)));
  }
  const out = installApp(work, app, stubTpl);
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
  w32(out, c + 404, ad.idle); w32(out, c + 408, ad.soft); w32(out, c + 412, ad.dma);
  w32(out, c + 416, 0); w32(out, c + 420, 0); w32(out, c + 424, 0);
  // save area: block map, clock note and session belong to this firmware
  // (the stored map sector points at the blocks: same places needed, not only the same firmware)
  const same = !!old && old.map.every((b, i) => b === map[i]) && old.spare.length === spare.length && old.spare.every((b, i) => b === spare[i]) && used.every((g) => rom.subarray(old.map[g] << 16, (old.map[g] + 1) << 16).every((v, k) => v === fw[(g << 16) + k]));
  if (!same) out.fill(0xFF, SAVE_OFF, SAVE_END);
  else out.set(rom.subarray(SAVE_OFF, SAVE_OFF + T18_SAVE_LEN), SAVE_OFF);   // (installApp cleared it)
  w32(out, c + 400, (opts.lang === 'en' ? 1 : 0) | (opts.clock === false ? 0 : 2) | (hot ? T18F_HOTWIN | (T18_HOT_V2 ? T18F_HOT2 : 0) : 0));
  t18PutXip(out, app);
  return { rom: out, blocks: used.length, spares: spare.length, hot };
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
function t18FromRom(rom) {
  const c = t18Cfg(rom); if (!c) return null;
  let map = c.map;
  if (u32(rom, SAVE_OFF) === 0x504D3854) { map = []; for (let i = 0; i < 128; i++) map.push(rom[SAVE_OFF + 4 + 2 * i] | (rom[SAVE_OFF + 5 + 2 * i] << 8)); }
  const fw = new Uint8Array(0x800000).fill(0xFF);
  map.forEach((b, g) => { if (b !== 0xFFFF && b < 0x100) fw.set(rom.subarray(b << 16, (b + 1) << 16), g << 16); });
  return t18Detect(fw) ? fw : null;
}
/*
 * More programs (1.6.0): besides the program at 0x0A0000 (started by A+C or by the menu) the menu can
 * start more programs, each a full 64 KB PRAM image somewhere in the flash (started through the stub
 * entry at +0x0C). For now: T7Emu with its own ROM (Tama-Go or Friends), no figures. Placed from the
 * top of the emulator area (0xD40000) down, each in its own block: image (64 KB), save (24 KB), ROM.
 * The menu lists them (settings block version 5, see feld.js).
 */
const APPX_MAX = 6, T7X_SAVE = 0x6000;
function t7XName(rom) { return t7Kind(rom) === 'friends' ? 'FRIENDS' : 'TAMA-GO'; }
/** Reserved by the program at 0x0A0000 although empty (spares): [from, to) ranges. */
function appReserved(rom) {
  const r = [], m = u32(rom, APP_OFF + 0x140);
  if (m === T18_MAGIC) { const c = t18Cfg(rom); for (const b of c.map.concat(c.spare)) if (b !== 0xFFFF) r.push([b << 16, (b + 1) << 16]); if (c.flags & T18F_HOTWIN) r.push([0xFD0000, 0xFF0000]); }
  if (m === PMXC && u32(rom, APP_OFF + 0x144) >= 3) { const g = u32(rom, APP_OFF + 0x148); r.push([g, Math.max(g, u32(rom, APP_OFF + 0x140 + 152))]); }
  return r;
}
/**
 * Put the extra programs into rom. extras: [{ rom }] (T7 ROMs). app: t7.bin. Returns { rom, list: [{ src, name, off }], low }.
 */
function appsPlace(rom, extras, app, opts = {}) {
  const out = new Uint8Array(rom), list = [], res = appReserved(out);
  if (extras.length > APPX_MAX) throw new Error('at most ' + APPX_MAX + ' more programs');
  let top = GUEST_END;
  for (const x of extras) {
    if (!t7Detect(x.rom)) throw new Error('not a V7 ROM');
    const size = 0x20000 + ((x.rom.length + 0xFFFF) & ~0xFFFF);
    const at = (top - size) & ~0xFFFF;
    if (at < GUEST_OFF || !isFF(out, at, top - at) || res.some(([a, b]) => a < top && b > at)) throw new Error('no room for ' + t7XName(x.rom) + ' (' + (size >> 10) + ' KB)');
    const a = new Uint8Array(0x10000); a.set(app);
    const c = 0x140;
    if (u32(a, c) !== T7_MAGIC) throw new Error('settings block missing in the program');
    w32(a, c + 4, 1); w32(a, c + 8, at + 0x20000); w32(a, c + 12, x.rom.length); w32(a, c + 16, at + 0x10000); w32(a, c + 20, 0);
    a.fill(0, c + 24, c + 24 + T7_FIG_MAX * 24);
    w32(a, c + 24 + T7_FIG_MAX * 24, (opts.lang === 'en' ? 1 : 0) | (opts.clock === false ? 0 : 2));
    out.set(a, at);
    out.set(x.rom, at + 0x20000);
    list.push({ src: xip(at), name: t7XName(x.rom), off: at });
    top = at;
  }
  return { rom: out, list, low: top };
}
/** Free the blocks of the extra programs listed (from appsRead). */
function appsClear(rom, old) {
  const out = new Uint8Array(rom);
  for (const x of old || []) if (u32(out, x.off + 0x140) === T7_MAGIC) out.fill(0xFF, x.off, x.off + 0x20000 + ((x.rom.length + 0xFFFF) & ~0xFFFF));
  return out;
}
/** The extra programs of a ROM, from the menu's list ([{ src, name }]). */
function appsRead(rom, apps) {
  const out = [];
  for (const e of apps || []) {
    const off = e.src - 0x10000000 + XIP_DEC;
    if (off < 0 || off + 0x10000 > rom.length || u32(rom, off + 0x140) !== T7_MAGIC) continue;
    const ro = u32(rom, off + 0x148), rl = u32(rom, off + 0x14C);
    if (ro + rl > rom.length) continue;
    out.push({ rom: rom.slice(ro, ro + rl), name: e.name, off, saveOff: u32(rom, off + 0x150) });
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
function emuClockGet(rom) { const a = emuClockAt(rom); return a < 0 ? null : !!(u32(rom, a) & 2); }
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

root.ParamixEmu = { installApp, dlcStores, dlcRoute, dlcAdd, dlcFree, dlcSlots, dlcRecord, dlcPut, dlcName, emuDetect, sceneTable, packGuest, emuPatch, emuUpgrade, emuRemove, t7Embedded, t7Detect, t7Kind, appsPlace, appsRead, appsClear, appReserved, t7XName, T7X_SAVE, APPX_MAX, t7FigDetect, t7FigData, t7FigName, t7Cfg, t7FromRom, t7Room, t7Patch, t7Upgrade, t7Wall, T7_MAGIC, t18Embedded, t18Detect, t18Addrs, t18Cfg, t18Free, t18Low, t18Patch, t18Upgrade, t18Wall, t18ClockGet, t18ClockSet, emuClockGet, emuClockSet, T18_MAGIC, T18_XIP_OFF, T18_SAVE_LEN, autoGet, autoGetOld, autoSet, STUB_OFF, installedOrigs, embedded, DEVICES, GUEST_OFF, BIG_OFF };
if (typeof module !== 'undefined') module.exports = root.ParamixEmu;
})(typeof window !== 'undefined' ? window : globalThis);
