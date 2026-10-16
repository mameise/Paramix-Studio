/*
 * Paramix Studio: save games in and out of the Paradise.
 *
 * Paradise: the game keeps its progress in the flash after the edition ID page
 * (0xD4A000, Tropical/Glacier 0xD2A000) up to 0xF00000. Exported as a
 * .tamasave (format 3 of the TamaParadise app): every page of that region,
 * the seconds counter of the device and an armed wake alarm, so the app
 * continues instead of starting like a fresh battery. Behind the pages a
 * Paramix trailer ("PMXE", edition, region) that the app ignores and the
 * Studio uses to refuse a save of another edition.
 *
 * TamaEmu: the guest's own save is its flash. Exported as the raw image the
 * TamaEmu app uses (.sav, 4 MB or 8 MB; named after the app's device: plus-color, idl, ps, 4u): packed blocks plus the spare pages
 * the emulator wrote. Importing a .sav works like choosing a firmware.
 *
 * Author: mameise
 */
(function (root) {
'use strict';

const PAGE = 0x1000;
const MAGIC = 'TAMASAVE', VERSION = 3, TRAILER = 0x45584D50;   // "PMXE"
const SET_A = 0xEFE000, SET_B = 0xEFF000, REGION_END = 0xF00000;
const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }

/** Edition 5/6 (Tropical, Glacier) keep their ID page lower. */
function idPage(edition) { return edition === 5 || edition === 6 ? 0xD29000 : 0xD49000; }
function region(edition) { return [idPage(edition) + PAGE, REGION_END]; }
function family(edition) { return edition === 5 || edition === 6 ? 'gen3' : edition === 4 ? 'jade' : 'gen1'; }

/* Settings block: CRC-16/ARC over bytes 4..4095 at 0, its inverse at 2. */
function crc16arc(d, from, to) {
  let c = 0;
  for (let i = from; i < to; i++) { c ^= d[i]; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1; }
  return c;
}
function settingsOk(p) { const c = crc16arc(p, 4, PAGE); return u16(p, 0) === c && u16(p, 2) === (~c & 0xFFFF); }

// ---------------------------------------------------------------- Paradise
/** Pages of the save region from a 16 MB image (all of them, empty ones too). */
function paraFromImage(img, edition) {
  const [a, b] = region(edition), pages = new Map();
  for (let p = a; p < b; p += PAGE) pages.set(p, img.slice(p, p + PAGE));
  return pages;
}
/**
 * .tamasave from pages (Map address -> 4 KB). clock: { counter, ctrl } of the device or null.
 */
function paraEncode(pages, edition, clock, now = Math.floor(Date.now() / 1000)) {
  const counter = clock ? clock.counter >>> 0 : 0;
  const alarm = counter ? counter - 1 : 0, status = counter ? 0x100 : 0;   // armed, as the app does when it puts the console away
  // the always powered system registers as the app stores them (clock and PLL as the firmware sets them)
  const regs = counter ? [[0x000, 0xC051], [0x004, 0xA55A0000], [0x00C, 4], [0x010, 0], [0x018, 0x21], [0x020, 0x42], [0x050, 0],
    [0x100, 0x157EB7], [0x108, 0], [0x10C, 0x1FFFFD], [0x110, 0], [0x200, clock.ctrl & 0x80 ? clock.ctrl >>> 0 : 0xE0], [0x204, 0],
    [0x210, 0], [0x214, 0], [0x218, 0], [0x230, alarm], [0x234, status], [0x300, 0x10]] : [];
  const list = [...pages.keys()].sort((x, y) => x - y);
  const size = 36 + 8 * regs.length + 4 + list.reduce((n, a) => n + 8 + pages.get(a).length, 0) + 16;
  const out = new Uint8Array(size);
  for (let i = 0; i < 8; i++) out[i] = MAGIC.charCodeAt(i);
  w32(out, 8, VERSION);
  w32(out, 12, counter ? now : 0); w32(out, 16, 0);              // u64 time stamp (seconds since 1970)
  w32(out, 20, counter); w32(out, 24, alarm); w32(out, 28, status); w32(out, 32, regs.length);
  let o = 36;
  for (const [k, v] of regs) { w32(out, o, k); w32(out, o + 4, v); o += 8; }
  w32(out, o, list.length); o += 4;
  for (const a of list) { const d = pages.get(a); w32(out, o, a / PAGE); w32(out, o + 4, d.length); out.set(d, o + 8); o += 8 + d.length; }
  w32(out, o, TRAILER); w32(out, o + 4, edition); w32(out, o + 8, region(edition)[0]); w32(out, o + 12, region(edition)[1]);
  return out;
}
/** Read a .tamasave (format 3). Throws on anything else. */
function paraDecode(b) {
  if (b.length < 40 || String.fromCharCode(...b.subarray(0, 8)) !== MAGIC) throw new Error('not a Paradise save (.tamasave)');
  if (u32(b, 8) !== VERSION) throw new Error('unknown save version ' + u32(b, 8));
  const stamp = u32(b, 12) + u32(b, 16) * 4294967296, counter = u32(b, 20), alarm = u32(b, 24), status = u32(b, 28);
  const nr = u32(b, 32); let o = 36;
  const regs = [];
  for (let i = 0; i < nr; i++) { if (o + 8 > b.length) throw new Error('save is cut off'); regs.push([u32(b, o), u32(b, o + 4)]); o += 8; }
  if (o + 4 > b.length) throw new Error('save is cut off');
  const np = u32(b, o); o += 4;
  const pages = new Map();
  for (let i = 0; i < np; i++) {
    if (o + 8 > b.length) throw new Error('save is cut off');
    const a = u32(b, o) * PAGE, n = u32(b, o + 4); o += 8;
    if (o + n > b.length) throw new Error('save is cut off');
    pages.set(a, b.slice(o, o + n)); o += n;
  }
  let edition = 0;
  if (o + 16 <= b.length && u32(b, o) === TRAILER) edition = u32(b, o + 4);
  return { stamp, counter, alarm, status, regs, pages, edition };
}
/**
 * Can this save go onto a device of this edition? Returns { errors, warnings, pages (only the
 * save region, full 4 KB), complete }.
 */
function paraCheck(save, edition) {
  const errors = [], warnings = [], [a, b] = region(edition), pages = new Map();
  if (save.edition && save.edition !== edition) errors.push({ code: 'saveOtherEd', p: { s: save.edition, d: edition } });
  let outside = 0;
  for (const [p, d] of save.pages) {
    if (p < a || p >= b || d.length !== PAGE) { outside++; continue; }
    pages.set(p, d);
  }
  if (outside) warnings.push({ code: 'saveOutside', p: { n: outside } });
  const sa = pages.get(SET_A), sb = pages.get(SET_B);
  const set = sa && settingsOk(sa) ? sa : sb && settingsOk(sb) ? sb : null;
  if (!set) errors.push({ code: 'saveNoSettings' });
  else if ((u16(set, 12) === 0x016D) !== (family(edition) === 'gen3')) errors.push({ code: 'saveFamily' });
  let complete = true;
  for (let p = a; p < b; p += PAGE) if (!pages.has(p)) { complete = false; break; }
  if (!complete) warnings.push({ code: 'saveIncomplete' });
  return { errors, warnings, pages, complete };
}
/** Seconds counter to set on the device: the saved one plus the time since. */
function paraClock(save, now = Math.floor(Date.now() / 1000)) {
  if (!save.counter || !save.stamp) return null;
  return (save.counter + Math.max(0, now - save.stamp)) >>> 0;
}

// ---------------------------------------------------------------- TamaEmu
const APP_CFG = 0xA0140, PMXC = 0x43584D50, PG_MAGIC = 0x50584D50, PG_MAX = 64;
const KINDS = { 0: { dev: 'pc', size: 0x400000, app: 'plus-color' }, 1: { dev: 'idl', size: 0x800000, app: 'idl' },
  2: { dev: 'ps', size: 0x800000, app: 'ps' }, 3: { dev: '4u', size: 0x800000, app: '4u' }, 4: { dev: '4u-plain', size: 0x800000, app: '4u-plain' } };
/** Settings block of the emulator in an image (or the 4 KB sector at 0xA0000). */
function emuCfg(sec, base = 0xA0000) {
  const c = APP_CFG - base;
  if (u32(sec, c) !== PMXC) return null;
  const v = u32(sec, c + 4), kind = u32(sec, c + 20);
  if (!KINDS[kind]) return null;
  return { version: v, guestOff: u32(sec, c + 8), guestSize: u32(sec, c + 12), saveOff: u32(sec, c + 16), kind, map: sec.slice(c + 24, c + 152), spareEnd: v >= 3 ? u32(sec, c + 152) : 0, ...KINDS[kind] };
}
/**
 * Guest flash (the .sav of the TamaEmu app) from the packed firmware and the spare pages.
 * read(addr, len) gives bytes of the Paradise flash (sync or async).
 */
async function emuGuestAsync(read, cfg, progress) {
  const out = new Uint8Array(cfg.size).fill(0xFF), nb = cfg.size >> 16;
  let done = 0; const todo = [...cfg.map.subarray(0, nb)].filter((k) => k !== 0xFF).length;
  for (let b = 0; b < nb; b++) {
    let k = cfg.map[b];
    if (k === 0xFF) continue;
    if (k === 0xFE) k = b;
    out.set(new Uint8Array(await read(cfg.guestOff + (k << 16), 0x10000)), b << 16);
    if (progress) progress('read', ++done, todo);
  }
  // spare pages (8 KB parts the guest wrote into blocks that are empty in the firmware)
  if (cfg.spareEnd) {
    const lo = (cfg.guestOff + cfg.guestSize + 0x1FFF) & ~0x1FFF, hi = cfg.spareEnd & ~0x1FFF;
    if (hi >= lo + 0x4000) {
      const data = lo + 0x2000; let n = (hi - data) >> 13; if (n > PG_MAX) n = PG_MAX;
      const t = new Uint8Array(await read(lo, 16 + 2 * PG_MAX));
      let x = (PG_MAGIC ^ Math.imul(n, 0x9E3779B1)) >>> 0;
      for (let i = 0; i < PG_MAX; i++) x = (((x << 5) | (x >>> 27)) ^ u16(t, 16 + 2 * i)) >>> 0;
      if (u32(t, 0) === PG_MAGIC && u32(t, 4) === n && u32(t, 8) === x) {
        for (let i = 0; i < n; i++) {
          const v = u16(t, 16 + 2 * i);
          if (v === 0xFFFF || (v << 13) >= cfg.size) continue;
          if (cfg.map[(v << 13) >> 16] !== 0xFF) continue;          // only blocks that are empty in the firmware
          out.set(new Uint8Array(await read(data + (i << 13), 0x2000)), v << 13);
        }
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------- T7Emu
/*
 * The pet's EEPROM (4 KB) in the ROM: two slots of 8 KB from 0x0C1000 (header sector:
 * "T7EE", sequence, page mask, checksum; then the pages). The T7Emu app keeps it as
 * save.bin: page mask (2 bytes) and the 16 pages, 4098 bytes. Same content, so it can go back and forth.
 */
const T7_EE = 0xC1000, T7_EE_MAGIC = 0x45453754, T7_SAVE_LEN = 4098;
function t7Sum(b, o) { let s = 0x7E7E; for (let i = 0; i < 4096; i++) s = (Math.imul(s, 31) + b[o + i]) >>> 0; return s; }
function t7Slot(img, at) { if (u32(img, at) !== T7_EE_MAGIC || u32(img, at + 12) !== t7Sum(img, at + 0x1000)) return null; return { seq: u32(img, at + 4), have: u32(img, at + 8) & 0xFFFF, at }; }
function t7Newest(img, ee = T7_EE) { const a = t7Slot(img, ee), b = t7Slot(img, ee + 0x2000); if (a && b) return ((a.seq - b.seq) | 0) > 0 ? a : b; return a || b; }
/** save.bin of the T7Emu app from a 16 MB image (null: the pet has no save yet). base: save area of
 * the T7Emu (0x0C0000, a further one from 1.6.0 its own). */
function t7SaveOut(img, base = 0xC0000) {
  const s = t7Newest(img, base + 0x1000); if (!s) return null;
  const out = new Uint8Array(T7_SAVE_LEN); out[0] = s.have & 255; out[1] = s.have >> 8;
  out.set(img.subarray(s.at + 0x1000, s.at + 0x2000), 2);
  return out;
}
function t7SaveCheck(b) { return b && b.length === T7_SAVE_LEN; }
/** save.bin into the image: into the older slot, as the device does. Returns a new image. */
function t7SaveIn(img, save, base = 0xC0000) {
  if (!t7SaveCheck(save)) throw new Error('not a T7Emu save (save.bin, 4098 bytes)');
  const ee = base + 0x1000, out = new Uint8Array(img), cur = t7Newest(img, ee);
  const at = cur && cur.at === ee ? ee + 0x2000 : ee;
  out.fill(0xFF, at, at + 0x2000);
  out.set(save.subarray(2), at + 0x1000);
  w32(out, at, T7_EE_MAGIC); w32(out, at + 4, cur ? (cur.seq + 1) >>> 0 : 1); w32(out, at + 8, save[0] | (save[1] << 8)); w32(out, at + 12, t7Sum(out, at + 0x1000));
  out.fill(0xFF, base, base + 0x1000);          // the running session belongs to the old pet
  return out;
}

root.ParamixSaves = { t7SaveOut, t7SaveIn, t7SaveCheck, T7_EE, idPage, region, family, settingsOk, crc16arc, paraFromImage, paraEncode, paraDecode, paraCheck, paraClock, emuCfg, emuGuestAsync, KINDS };
if (typeof module !== 'undefined') module.exports = root.ParamixSaves;
})(typeof window !== 'undefined' ? window : globalThis);
