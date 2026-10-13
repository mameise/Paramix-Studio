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
 *   0x940000  color Tama firmware, packed in 64 KB blocks
 *
 * Author: mameise
 */
(function (root) {
'use strict';

const XIP_DEC = 0x11000;
const xip = (off) => 0x10000000 + off - XIP_DEC;
const APP_OFF = 0xA0000, STUB_OFF = 0xB0000, SAVE_OFF = 0xC0000, SAVE_END = 0x110000;
const GUEST_OFF = 0x940000, GUEST_END = 0xD40000;
const APP_MAX = 0x10000;
const PMXC = 0x43584D50;
// Placeholders in the prebuilt stub (stub_tpl.bin)
const PH_APP = 0x1234ABCD, PH_ORIG = [0x22222221, 0x33333331, 0x44444441, 0x55555551];
const HOOK_OFF = [0x40, 0x46, 0x4C, 0x52];
const DEVICES = {
  pc:  { kind: 0, size: 0x400000, label: '4 MB', par: 0x3F },
  idl: { kind: 1, size: 0x800000, label: '8 MB', par: 0x00 },
};
// Blocks with many data accesses first: they sit in the cached 1 MB window
const HOT = {
  pc:  [0x25, 0x21, 0x04, 0x02, 0x01, 0x03, 0x05, 0x20, 0x22, 0x23, 0x24, 0x26, 0x27, 0x28, 0x29, 0x2a],
  idl: [0x0b, 0x0e, 0x0d, 0x19, 0x08, 0x09, 0x16, 0x18, 0x0c, 0x17, 0x4f, 0x50, 0x40, 0x15, 0x4c, 0x4d],   // v0.8.17 after measuring (0x08/0x09 added)
};

const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
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
    if (v >= 0x2000000 && v < 0x2800000) return 'idl';
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
  const n = D.size >> 16, used = [];
  for (let i = 0; i < n; i++) if (i === D.par || !isFF(g, i << 16, 0x10000)) used.push(i);
  const order = HOT[dev].filter((b) => used.includes(b)).concat(used.filter((b) => !HOT[dev].includes(b)));
  const map = new Uint8Array(128); map.fill(0xFF);
  const blob = new Uint8Array(order.length << 16);
  order.forEach((b, idx) => { map[b] = idx; blob.set(g.subarray(b << 16, (b + 1) << 16), idx << 16); });
  if (GUEST_OFF + blob.length > GUEST_END) throw new Error('firmware too big: ' + order.length + ' blocks');
  return { map, blob, blocks: order.length };
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
  // Regions must be free (or already used by us)
  if (!prev) {
    if (!isFF(out, APP_OFF, 0x11000)) throw new Error('area 0x0A0000 is in use');
    if (!isFF(out, GUEST_OFF, GUEST_END - GUEST_OFF)) throw new Error('area 0x940000 is in use');
  }
  if (!isFF(out, SAVE_OFF, SAVE_END - SAVE_OFF) && !prev) throw new Error('area 0x0C0000 is in use');
  // Pack and store the guest
  const { map, blob, blocks } = packGuest(guest, dev);
  out.fill(0xFF, GUEST_OFF, GUEST_END);
  out.set(blob, GUEST_OFF);
  // Program with settings
  if (app.length > APP_MAX) throw new Error('emulator program too big');
  const a = new Uint8Array(app);
  let ci = -1; for (let i = 0; i + 4 <= a.length; i += 4) if (u32(a, i) === PMXC) { ci = i; break; }
  if (ci < 0) throw new Error('settings block missing in the program');
  w32(a, ci + 4, 2); w32(a, ci + 8, GUEST_OFF); w32(a, ci + 12, blob.length); w32(a, ci + 16, SAVE_OFF); w32(a, ci + 20, DEVICES[dev].kind);
  a.set(map, ci + 24);
  out.fill(0x00, APP_OFF, APP_OFF + APP_MAX);
  out.set(a, APP_OFF);
  installStub(out, tab, prev, stubTpl);
  // erase the old save (it may belong to a different firmware)
  out.fill(0xFF, SAVE_OFF, SAVE_END);
  return { rom: out, device: dev, blocks };
}

/** Remove the emulator again (home screen back to the original handlers). */
function emuRemove(rom) {
  const out = new Uint8Array(rom);
  const tab = sceneTable(out);
  const prev = installedOrigs(out, tab);
  if (!prev) throw new Error('no emulator built in');
  prev.forEach((v, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, v));
  out.fill(0xFF, APP_OFF, APP_OFF + 0x11000);
  out.fill(0xFF, SAVE_OFF, SAVE_END);
  out.fill(0xFF, GUEST_OFF, GUEST_END);
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
// kept for older callers: the game store
const GAME_BASE = 0x7A0000, GAME_SIZE = 0x8000, GAME_SLOTS = 2;
const isTamago = (g, o) => o + 6 <= g.length && g[o] === 0x54 && g[o + 1] === 0x41 && g[o + 2] === 0x4D && g[o + 3] === 0x41 && g[o + 4] === 0x47 && g[o + 5] === 0x4F;
function dlcName(r) {
  let s = ''; for (let i = 6; i + 1 < 0x34; i += 2) { const c = (r[i] << 8) | r[i + 1]; if (!c) break; s += String.fromCharCode(c); }
  return s.replace(/^DL_[A-Za-z]+_/, '').replace(/\.(jpg|JPG)$/, '').trim();
}
/** All stores with their slots: [{ kind, label, slots: [{ slot, name|null }] }] (8 MB firmware only). */
function dlcStores(g) {
  if (g.length < 0x7B8000) return [];
  return IDL_STORES.map((k) => ({ kind: k.kind, label: k.label, slots: Array.from({ length: k.n }, (_, i) => {
    const o = k.base + i * k.size;
    return { slot: i, name: isTamago(g, o) ? dlcName(g.subarray(o, o + 0x34)) : null };
  }) }));
}
/** Game slots (compatibility with the old single-store view). */
function dlcSlots(g) {
  const st = dlcStores(g).find((k) => k.kind === 7);
  return st ? st.slots : [];
}
/** Store for a record: { kind, label } or null. */
function dlcRoute(r) {
  const t = r[0x4E], c = r[0x50], f = r[0x51];
  const m = IDL_ROUTE.find((x) => x[0] === t && x[1] === c && x[2] === f);
  if (!m) return null;
  return IDL_STORES.find((k) => k.kind === m[3]);
}
/** Extract and check the TAMAGO record of a download file (.jpg or raw). */
function dlcRecord(d) {
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
  const st = dlcRoute(r);
  if (!st) throw new Error('this content fits no store of the 8 MB firmware');
  if (r.length > st.size) throw new Error('content too big for its store');
  return r;
}
function grow(g) { const out = new Uint8Array(Math.max(g.length, 0x800000)); out.fill(0xFF); out.set(g); return out; }
/** Add a record to the first free slot of its store. Returns { guest, store, slot }. */
function dlcAdd(g, rec) {
  if (g.length < 0x7B0000) throw new Error('only for the 8 MB firmware');
  const st = dlcRoute(rec); if (!st) throw new Error('no matching store');
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
  const st = IDL_STORES.find((k) => k.kind === kind); if (!st || slot < 0 || slot >= st.n) throw new Error('no such slot');
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

const EMU_APP_B64 = '__EMU_APP_B64__', EMU_STUB_B64 = '__EMU_STUB_B64__';
function embedded() {
  if (EMU_APP_B64.startsWith('__')) return null;
  return { app: b64(EMU_APP_B64), stub: b64(EMU_STUB_B64) };
}

root.ParamixEmu = { installApp, dlcStores, dlcRoute, dlcAdd, dlcFree, dlcSlots, dlcRecord, dlcPut, dlcName, emuDetect, sceneTable, packGuest, emuPatch, emuRemove, installedOrigs, embedded, DEVICES, GUEST_OFF };
if (typeof module !== 'undefined') module.exports = root.ParamixEmu;
})(typeof window !== 'undefined' ? window : globalThis);
