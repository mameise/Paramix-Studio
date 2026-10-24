/*
 * Paramix Studio: Paramix menu in the ROM (field switch, items, Tamapoints).
 *
 * A small program in the ROM, launched like the emulator and DOOM through the stub
 * (hold A and C on the home screen). It can
 *   - switch the field (Land/Water/Sky or Tropical/Glacier), each field with
 *     its own save, which is packed into a slot when switching,
 *   - put items into the bag and set Tamapoints (as in the TamaParadise app),
 *   - launch an installed program (DOOM, TamaEmu).
 * Anything that depends on the edition lives in the program's settings block and is
 * set here (profiles below, measured on the factory images).
 *
 * Location in flash:
 *   without DOOM: program and catalog from 0x0B1000, slots behind the character data
 *   with DOOM:    program, catalog and slots in the free end of the DOOM program region
 */
(function (root) {
'use strict';

const XIP_DEC = 0x11000;
const xip = (off) => 0x10000000 + off - XIP_DEC;
const APP_OFF = 0xA0000, STUB_OFF = 0xB0000, FELD_OFF = 0xB1000, FELD_END = 0xC0000;
const DOOM_CODE = 0xB1000, DOOM_SAVE = 0x10E000;
const SLOT_BASE = 0x830000, SLOT_SIZE = 0x58000, GUEST_OFF = 0x940000;
const FELD_MAGIC = 0x444C4546, SLOT_MAGIC = 0x544C5346, CAT_MAGIC = 0x54414350, PMXC = 0x43584D50, PDMC = 0x434D4450;
const PH_APP = 0x1234ABCD, PH_FELD = 0x6789EF01, PH_AUTO = 0x66666661, PH_ORIG = [0x22222221, 0x33333331, 0x44444441, 0x55555551];
const HOOK_OFF = [0x40, 0x46, 0x4C, 0x52];
const F_FIELDS = 1, F_BAG = 2, F_TAMA = 4, F_SAVES = 8, F_LAUNCH = 16;
/*
 * Launcher ROM (1.6.8): the handlers of every scene after the setup point to the stub's launch entry
 * (+0x20), so the menu comes up instead of the game. The setup scenes below stay. The original handlers
 * are kept in the stub page at +0x400 ("PLNC", count, then per scene: table offset, four handlers).
 */
const LAUNCH_ENTRY = 0x20, LREC_OFF = 0x400, LREC_MAGIC = 0x434E4C50;
// Only the game's own scenes (home, care, events, lab, shop, space); the setup and everything not clearly part of the
// game stay (also SETTING_*: the clock after "continue" may use it). Measured in the Paradise emulator (1.6.8): the four
// handlers 8 bytes behind a scene's name belong to the NEXT scene of the table (the setup runs, in this naming,
// OSEWAGAME_03, STARTUP, CORRECTTIME, LANGUAGE, DATETIME, BIRTHDAY, i.e. really STARTUP, INITIAL_LOGO, LANGUAGE,
// DATETIME, BIRTHDAY, PLANETNAME). So scene N is redirected in the entry of the scene before it.
const LAUNCH_SCENE = /^PSID_(HOME|HOME_\w+|OSEWA_\w+|CELL_STATUS|LABOMENU\w*|LBMENU_\w+|INFO_\w+|SHOP_\w+|BOOK_\w+|HELP_\w+|EVENT_\w+|LABOGAME_\w+|OSEWAGAME_\w+|TAMASPACE_\w+)$/;
const isHandler = (v) => v >= 0x10000000 && v < 0x10100000 && (v & 1) === 1;
/*
 * Per redirected scene a block of 52 bytes in the trampoline pages (behind the menu program): table offset, the four
 * original handlers, then four trampolines "ldr.w r12, <original>; b.w <stub + 0x2C>". The stub starts the menu only
 * once the Paradise has a save, otherwise it jumps to r12 (the original), so a setup after a reset finishes normally.
 * The stub page holds at +0x400: "PLNC", count, offset of the trampoline pages.
 */
const LCHECK_ENTRY = 0x2C, TBLK = 52;
function launchScenes(tab) {
  const ent = Object.entries(tab).sort((x, y) => x[1] - y[1]), r = [];
  for (let i = 1; i < ent.length; i++) if (LAUNCH_SCENE.test(ent[i][0])) r.push(ent[i - 1][1]);
  return r;
}
function launchSize(out, tab) { return page(TBLK * launchScenes(tab).filter((o) => [0, 1, 2, 3].every((k) => isHandler(u32(out, o + 12 + 4 * k)))).length); }
/** Thumb-2 b.w from address `from` to `to` (both in the XIP window): two halfwords */
function bw(from, to) {
  const off = to - (from + 4), S = off < 0 ? 1 : 0, imm = off >>> 1;
  const imm11 = imm & 0x7FF, imm10 = (imm >>> 11) & 0x3FF, I2 = (imm >>> 21) & 1, I1 = (imm >>> 22) & 1;
  const J1 = (~I1 ^ S) & 1, J2 = (~I2 ^ S) & 1;
  return [0xF000 | (S << 10) | imm10, 0x9000 | (J1 << 13) | (J2 << 11) | imm11];
}
function launchRedirect(out, tab, stubOff, tOff) {
  const list = launchScenes(tab).filter((o) => [0, 1, 2, 3].every((k) => isHandler(u32(out, o + 12 + 4 * k))));
  if (list.length > 200) throw new Error('too many scenes');
  out.fill(0xFF, tOff, tOff + page(TBLK * list.length));
  list.forEach((o, i) => {
    const b = tOff + TBLK * i;
    w32(out, b, o);
    for (let k = 0; k < 4; k++) {
      w32(out, b + 4 + 4 * k, u32(out, o + 12 + 4 * k));
      const t = b + 20 + 8 * k, lit = b + 4 + 4 * k, imm = ((t + 4) & ~3) - lit;   // literal behind: ldr.w r12, [pc, #-imm]
      w16(out, t, 0xF85F); w16(out, t + 2, 0xC000 | imm);
      const [h1, h2] = bw(xip(t + 4), xip(stubOff) + LCHECK_ENTRY); w16(out, t + 4, h1); w16(out, t + 6, h2);
      w32(out, o + 12 + 4 * k, xip(t) | 1);
    }
  });
  const at = stubOff + LREC_OFF;
  out.fill(0xFF, at, stubOff + 0x1000);
  w32(out, at, LREC_MAGIC); w32(out, at + 4, list.length); w32(out, at + 8, tOff);
}
/** Undo launchRedirect (the record in the stub page and the trampoline pages). */
function launchRestore(out, stubOff) {
  const at = stubOff + LREC_OFF;
  if (u32(out, at) !== LREC_MAGIC) return false;
  const n = Math.min(u32(out, at + 4), 200), tOff = u32(out, at + 8);
  if (tOff < 0x90000 || tOff + TBLK * n > 0x110000) return false;
  for (let i = 0; i < n; i++) { const b = tOff + TBLK * i, o = u32(out, b); for (let k = 0; k < 4; k++) w32(out, o + 12 + 4 * k, u32(out, b + 4 + 4 * k)); }
  out.fill(0xFF, tOff, tOff + page(TBLK * n));
  out.fill(0xFF, at, at + 12);
  return true;
}
/** Is the ROM a launcher ROM (menu with the launch flag)? */
function isLaunch(rom, tpl) { const f = feldFind(rom, tpl); return !!f && f.version >= 2 && (f.flags & F_LAUNCH) !== 0; }
// Six fields (six.js): helper page at 0x10F000, set byte in the settings block
const SIX_OFF = 0x10F000, SIX_MAGIC = 0x31584953, SIX_FLAG = 0xF04, CODE_END = 0x10F000;
const isSix = (rom) => rom.length > SIX_OFF + 4 && u32(rom, SIX_OFF) === SIX_MAGIC;

const rgb = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
/*
 * Editions. ID page: word 0 = ID, footer u16 h0, u16 h1, C minus ID,
 * inverse. Factory settings (0xEFE000/0xEFF000): zeros and these bytes, plus the
 * checksum, which the program computes itself. Bag and money as in the app.
 */
const PROFILES = [
  { name: 'gen1', idPage: 0xD49000, edIds: [1, 2, 3], ids: [1, 2, 3], names: ['LAND', 'WATER', 'SKY'], h: [1, 4], c: 0x70169E2C,
    cols: [rgb(80, 200, 60), rgb(40, 120, 255), rgb(120, 210, 255)], fieldByte: 439,
    common: [[8, 0x11], [16, 0xc0], [17, 0xa8], [168, 1], [576, 0x3f], [577, 0x1f], [1856, 1]],
    field: [[[4, 0x0c], [170, 2], [439, 0]], [[4, 4], [170, 1], [439, 1]], [[4, 4], [170, 1], [439, 2]]],
    bag: [0xb4, 0x240, 0x740], tama: 0x104, level: 0x7C },
  // Jade Forest: one ID (4), three fields in the save (byte 443): LAND, WATER, FOREST (texts 14 to 16 in the ROM).
  // 1.6.9: the block has 4 bytes more from about 0xf8 on (the firmware's offsets from 0x114 are 4 higher than Land's):
  // the bag list is at 0x244 and its amounts at 0x744 (count getter LDR [r0,#0x740] from block + 4), the money stays at 0xb4
  { name: 'jade', idPage: 0xD49000, edIds: [4], ids: [0, 0, 0], names: ['LAND', 'WATER', 'FOREST'], h: [2, 0], c: 0x849633DB,
    cols: [rgb(80, 200, 60), rgb(40, 120, 255), rgb(60, 190, 110)], fieldByte: 443,
    common: [[4, 4], [8, 17], [16, 192], [17, 168], [168, 1], [170, 3], [580, 63], [581, 31], [1860, 1]],
    field: [[[443, 0]], [[443, 1]], [[443, 2]]], bag: [0xb4, 0x244, 0x744], tama: 0x108, level: 0x7C },
  // Tropical and Glacier: same firmware, three fields (byte 499): TROPICAL, ICE (Glacier), FOREST
  { name: 'gen3', idPage: 0xD29000, edIds: [5, 6], ids: [5, 6, 0], names: ['TROPICAL', 'GLACIER', 'FOREST'], h: [3, 1], c: 0x96CB83B8,
    cols: [rgb(255, 150, 60), rgb(150, 220, 255), rgb(60, 190, 110)], fieldByte: 499,
    common: [[4, 4], [8, 17], [12, 109], [13, 1], [16, 192], [17, 168], [180, 1], [182, 1], [624, 62], [625, 31], [1904, 1]],
    field: [[[499, 0]], [[499, 1]], [[499, 2]]], bag: [0xc0, 0x270, 0x770], tama: 0x114, level: 0x88 },
];
const NAMES = { 1: 'Land', 2: 'Water', 3: 'Sky', 4: 'Jade Forest', 5: 'Tropical', 6: 'Glacier' };
/* Tropical/Glacier with six fields: the second three are slots 0..2 with the set byte 1. */
function profileOf(rom, p) {
  if (!isSix(rom)) return p;
  const g = PROFILES[0];
  if (p.name === 'gen3') return Object.assign({}, p, {
    ids: p.ids.concat(p.ids), names: p.names.concat(g.names), cols: p.cols.concat(g.cols),
    field: p.field.concat(p.field.map((f) => f.concat([[SIX_FLAG, 1]]))), six: SIX_FLAG, sixMap: [0, 1, 2, 255, 3, 4, 5, 255],
  });
  // Jade Forest: Sky is slot 1 (Water) with the set byte
  if (p.name === 'jade') return Object.assign({}, p, {
    ids: p.ids.concat([0]), names: p.names.concat(['SKY']), cols: p.cols.concat([g.cols[2]]),
    field: p.field.concat([[[p.fieldByte, 1], [SIX_FLAG, 1]]]), six: SIX_FLAG, sixMap: [0, 1, 2, 255, 0, 3, 2, 255],
  });
  return p;
}
const cap = (s) => s.charAt(0) + s.slice(1).toLowerCase();

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w16(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; }
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
function isFF(b, a, n) { for (let i = a; i < a + n; i++) if (b[i] !== 0xFF) return false; return true; }
const page = (n) => (n + 0xFFF) & ~0xFFF;
function b64(s) {
  if (typeof atob === 'function') { const t = atob(s); const o = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) o[i] = t.charCodeAt(i); return o; }
  return new Uint8Array(Buffer.from(s, 'base64'));
}
function findWord(b, v, from = 0, to = b.length) { for (let i = from; i + 4 <= to; i += 2) if (u32(b, i) === v) return i; return -1; }

/** Edition of the ROM: { profile, id } or null. */
function edition(rom) {
  for (const p of PROFILES) {
    const id = u32(rom, p.idPage);
    if (p.edIds.includes(id)) return { profile: p, id };
  }
  return null;
}
/** Family of an edition ID (same firmware, only the active field differs): 'gen1' (Land, Water, Sky), 'jade', 'gen3'. */
function familyOf(id) { const p = PROFILES.find((q) => q.edIds.includes(id)); return p ? p.name : null; }
/** Edition ID (1 Land ... 6 Glacier) or 0. */
function feldId(rom) { const e = edition(rom); return e ? e.id : 0; }
/** Does this ROM support the field switch? (every recognized edition has three fields) */
function canFields(rom) { return !!edition(rom); }
/** The fields of this ROM (names). */
function fieldNames(rom) { const e = edition(rom); return e ? profileOf(rom, e.profile).names.map(cap) : []; }
/** Active field (name), read from the save. */
function fieldNow(rom) {
  const e = edition(rom); if (!e) return '';
  const P = profileOf(rom, e.profile);
  let f = rom[0xEFE000 + P.fieldByte];
  if (P.six && f < 4) f = P.sixMap[(rom[0xEFE000 + P.six] === 1 ? 4 : 0) + f];
  return f < P.names.length ? cap(P.names[f]) : NAMES[e.id];
}

/** Program in the ROM: 0 none, 1 DOOM, 2 emulator (TamaEmu, T7Emu or T18Emu, see emuType). */
function appKind(rom) {
  if (u32(rom, APP_OFF + 0x140) === 0x37584D50) return 2;   // "PMX7": T7Emu
  if (u32(rom, APP_OFF + 0x140) === 0x38584D50) return 2;   // "PMX8": T18Emu
  if (u32(rom, APP_OFF + 0x140) === 0x31584D50) return 2;   // "PMX1": V1Emu (1.6.8)
  if (u32(rom, APP_OFF + 0x140) !== PMXC) return 0;
  for (let i = 0x140; i < 0x400; i += 4) if (u32(rom, APP_OFF + i) === PDMC) return 1;
  return 2;
}

/*
 * Where is the stub? Normally at 0x0B0000 (shared with the emulator and DOOM).
 * In editions whose firmware code still occupies that spot (Tropical, Glacier),
 * right after the code in the first free region below 0x110000.
 */
function freeStubOff(rom) {
  if (isFF(rom, STUB_OFF, 0x2000)) return STUB_OFF;
  let p = 0x10F000;
  while (p > 0x90000 && isFF(rom, p - 0x1000, 0x1000)) p -= 0x1000;
  return p;
}
function feldAt(rom, stubOff, ph) {
  const v = u32(rom, stubOff + ph);
  if (v < 0x10000000 || v >= 0x11000000) return null;
  const off = v - 0x10000000 + XIP_DEC;
  const c = off + 0x140;
  if (off + 0x200 > rom.length || u32(rom, c) !== FELD_MAGIC) return null;
  const version = u32(rom, c + 4);
  const f = { off, stubOff, version, slotOff: u32(rom, c + 8), slotSize: u32(rom, c + 12), nslots: u32(rom, c + 16), flags: F_FIELDS, catOff: 0, catN: 0 };
  if (version >= 2) { f.flags = u32(rom, c + 36); f.catOff = u32(rom, c + 272); f.catN = u32(rom, c + 276); }
  f.slotOff2 = version >= 4 ? u32(rom, c + 364) : 0; f.slotN1 = version >= 4 ? u32(rom, c + 368) : 0;
  return f;
}
/** Flash address of slot i (from menu version 4 the slots can lie in two regions). */
function slotAddr(f, i) { return f.slotOff2 && f.slotN1 && i >= f.slotN1 ? f.slotOff2 + (i - f.slotN1) * f.slotSize : f.slotOff + i * f.slotSize; }
/** Is the menu installed? Returns { off, stubOff, version, slotOff, slotSize, nslots, flags, catOff, catN } or null. */
function feldFind(rom, tpl) {
  const ph0 = findWord(tpl, PH_FELD);
  if (ph0 < 0) return null;
  // older stubs: menu address at 0xF0 (up to 1.2.6), 0x118 (1.2.7) and 0x148 (1.6.0 to 1.6.7)
  for (const ph of [...new Set([ph0, 0xF0, 0x118, 0x148])]) {
    const f = feldAt(rom, STUB_OFF, ph);
    if (f) return f;
    for (let p = 0x90000; p < 0x110000; p += 0x1000) { const g = p !== STUB_OFF && feldAt(rom, p, ph); if (g) return g; }
  }
  return null;
}

/** More programs of the menu (version 5): { name0, apps: [{ src, name }] } (empty without). */
function feldApps(rom, tpl) {
  const f = feldFind(rom, tpl), r = { name0: '', apps: [] };
  if (!f || f.version < 5) return r;
  const c = f.off + 0x140, str = (o) => { let t = ''; for (let k = 0; k < 12 && rom[o + k]; k++) t += String.fromCharCode(rom[o + k]); return t; };
  r.name0 = str(c + 380);
  const n = Math.min(u32(rom, c + 392), f.version >= 8 ? 12 : 6);
  for (let i = 0; i < n; i++) { const o = i < 6 ? c + 396 + 16 * i : c + 504 + 16 * (i - 6); r.apps.push({ src: u32(rom, o), name: str(o + 4) }); }
  // 1.6.9 test13 (version 9): main program of the start device out of the list, start when switched on
  if (f.version >= 9) { r.v9 = true; r.mainSrc = u32(rom, c + 600); r.auto = (u32(rom, c + 604) & 1) === 1; }
  return r;
}
/** Saved slots (raw bytes) of a ROM with the menu, for moving them over. */
/*
 * Saved fields and the shared pool (1.2.12). The header page of field i sits at the start
 * of its slot. Older menus put the packed data right after it; from 1.2.12 the header says
 * "POOL" (word 5) and lists the pool pages (word 6 = count, u16 list at 2032). The pool is
 * every slot page except the header pages, numbered slot by slot.
 */
const POOL_MAGIC = 0x4C4F4F50, DPG_OFF = 2032, DPG_MAX = 1024;
function poolPages(f) { return f.slotSize / 0x1000 - 1; }
function poolAddr(f, k) { const n = poolPages(f); return slotAddr(f, Math.floor(k / n)) + (k % n + 1) * 0x1000; }
/** Read slot i with read(addr, len) (sync, or async with slotReadAsync). { head, field, bytes, stream } or null. */
function slotParse(f, i, head) {
  if (u32(head, 0) !== SLOT_MAGIC) return null;
  const field = u32(head, 4), bytes = u32(head, 12);
  if (field < 1 || field > 6 || u32(head, 8) > 500) return null;
  const pool = u32(head, 20) === POOL_MAGIC, nd = pool ? u32(head, 24) : 0, np = f.nslots * poolPages(f), list = [];
  if (pool) {
    if (nd > DPG_MAX || bytes > nd * 0x1000) return null;
    for (let k = 0; k < nd; k++) { const x = head[DPG_OFF + 2 * k] | (head[DPG_OFF + 2 * k + 1] << 8); if (x >= np) return null; list.push(poolAddr(f, x)); }
  } else if (bytes > f.slotSize - 0x1000) return null;
  return { head, field, bytes, pool, list, b: slotAddr(f, i) };
}
function slotFill(p, chunk, k) { p.stream.set(chunk.subarray(0, Math.min(chunk.length, p.bytes - k * 0x1000)), k * 0x1000); }
function slotRead(rom, f, i) {
  const p = slotParse(f, i, rom.subarray(slotAddr(f, i), slotAddr(f, i) + 0x1000));
  if (!p) return null;
  p.stream = new Uint8Array(p.bytes);
  if (p.pool) p.list.forEach((a, k) => slotFill(p, rom.subarray(a, a + 0x1000), k));
  else p.stream.set(rom.subarray(p.b + 0x1000, p.b + 0x1000 + p.bytes));
  return p;
}
async function slotReadAsync(read, f, i) {
  const p = slotParse(f, i, new Uint8Array(await read(slotAddr(f, i), 0x1000)));
  if (!p) return null;
  p.stream = new Uint8Array(p.bytes);
  if (p.pool) for (let k = 0; k < p.list.length; k++) slotFill(p, await read(p.list[k], 0x1000), k);
  else p.stream.set((await read(p.b + 0x1000, (p.bytes + 0xFFF) & ~0xFFF)).subarray(0, p.bytes));
  return p;
}
/**
 * Write a saved field into slot i of image out (pool format). used: Set of pool pages taken
 * by fields already placed. Returns the written addresses; throws if the pool is full.
 */
function slotPlace(out, f, i, s, used) {
  const n = f.nslots * poolPages(f), nd = Math.ceil(s.bytes / 0x1000), pages = [], own = i * poolPages(f);
  for (let r = 0; r < n && pages.length < nd; r++) { const x = (own + r) % n; if (!used.has(x)) { used.add(x); pages.push(x); } }
  if (pages.length < nd || nd > DPG_MAX) throw new Error('the stored saves do not fit into the room for the fields');
  const h = new Uint8Array(0x1000).fill(0xFF);
  h.set(s.head.subarray(0, DPG_OFF));
  w32(h, 4, i + 1); w32(h, 20, POOL_MAGIC); w32(h, 24, nd); w32(h, 28, 0);
  pages.forEach((x, k) => { h[DPG_OFF + 2 * k] = x & 255; h[DPG_OFF + 2 * k + 1] = x >> 8; });
  const b = slotAddr(f, i), written = [b];
  out.set(h, b);
  pages.forEach((x, k) => { const a = poolAddr(f, x), pg = new Uint8Array(0x1000).fill(0xFF);
    pg.set(s.stream.subarray(k * 0x1000, Math.min(s.bytes, (k + 1) * 0x1000))); out.set(pg, a); written.push(a); });
  return written;
}

// ---------------------------------------------------------------- one stored save as pages (1.6.8)
/* The menu packs every page with LZSS: flag byte for 8 entries (bit = reference), reference = 2 bytes
 * ((distance-1)<<4 | (length-3)), length field 15: one more byte added to 18 (up to 273). Same as feld/main.c. */
function lzUnpack(inp, n) {
  const out = new Uint8Array(0x1000); let i = 0, o = 0;
  while (o < 0x1000) {
    if (i >= n) throw new Error('stored save damaged');
    const f = inp[i++];
    for (let b = 0; b < 8 && o < 0x1000; b++) {
      if (f & (1 << b)) {
        if (i + 2 > n) throw new Error('stored save damaged');
        const v = inp[i] | (inp[i + 1] << 8); i += 2;
        const off = (v >> 4) + 1; let len = (v & 15) + 3;
        if ((v & 15) === 15) { if (i >= n) throw new Error('stored save damaged'); len += inp[i++]; }
        if (off > o || o + len > 0x1000) throw new Error('stored save damaged');
        for (let k = 0; k < len; k++, o++) out[o] = out[o - off];
      } else { if (i >= n) throw new Error('stored save damaged'); out[o++] = inp[i++]; }
    }
  }
  if (i !== n) throw new Error('stored save damaged');
  return out;
}
function lzPack(p) {
  const out = [], head = new Int32Array(4096).fill(-1), prev = new Int32Array(4096).fill(-1);
  const h = (i) => ((p[i] * 1089 + p[i + 1] * 33 + p[i + 2]) & 4095);
  let i = 0, fpos = 0, bit = 8;
  while (i < 0x1000) {
    if (bit === 8) { fpos = out.length; out.push(0); bit = 0; }
    let best = 0, boff = 0;
    if (i + 3 <= 0x1000) {
      let j = head[h(i)], tries = 0;
      while (j >= 0 && tries < 48) {
        let l = 0; while (l < 273 && i + l < 0x1000 && p[j + l] === p[i + l]) l++;
        if (l > best) { best = l; boff = i - j; if (l === 273) break; }
        j = prev[j]; tries++;
      }
    }
    const step = best >= 3 ? best : 1;
    if (best >= 3) {
      const ln = best - 3; out[fpos] |= 1 << bit;
      if (ln < 15) { const v = ((boff - 1) << 4) | ln; out.push(v & 255, v >> 8); }
      else { const v = ((boff - 1) << 4) | 15; out.push(v & 255, v >> 8, ln - 15); }
    } else out.push(p[i]);
    bit++;
    for (let k = 0; k < step; k++, i++) if (i + 3 <= 0x1000) { const x = h(i); prev[i] = head[x]; head[x] = i; }
  }
  return Uint8Array.from(out);
}
/** Pages of a stored save (from slotRead/slotReadAsync): Map address -> 4 KB. */
function slotPages(s) {
  const np = u32(s.head, 8), pages = new Map(); let pos = 0, sum = 0;
  for (let k = 0; k < s.bytes; k++) sum = (sum + s.stream[k]) >>> 0;
  if (np > 500 || sum !== u32(s.head, 16)) throw new Error('stored save damaged');
  for (let k = 0; k < np; k++) {
    const pg = u16(s.head, 32 + 4 * k), n = u16(s.head, 34 + 4 * k);
    if (pos + n > s.bytes) throw new Error('stored save damaged');
    pages.set(pg * 0x1000, lzUnpack(s.stream.subarray(pos, pos + n), n)); pos += n;
  }
  return pages;
}
/** A save (Map address -> 4 KB, only pages from..to that are not empty) packed like the menu does, for slotPlace. */
function slotBuild(pages, i, from, to) {
  const head = new Uint8Array(0x1000).fill(0xFF), parts = []; let np = 0, bytes = 0, sum = 0;
  for (const a of [...pages.keys()].sort((x, y) => x - y)) {
    const d = pages.get(a);
    if (a < from || a >= to || d.length !== 0x1000 || d.every((v) => v === 0xFF)) continue;
    if (np >= 500) throw new Error('save too big for a slot');
    const c = lzPack(d); parts.push(c);
    w16(head, 32 + 4 * np, a >>> 12); w16(head, 34 + 4 * np, c.length); np++;
    bytes += c.length; for (const v of c) sum = (sum + v) >>> 0;
  }
  const stream = new Uint8Array(bytes); let o = 0; for (const c of parts) { stream.set(c, o); o += c.length; }
  w32(head, 0, SLOT_MAGIC); w32(head, 4, i + 1); w32(head, 8, np); w32(head, 12, bytes); w32(head, 16, sum);
  return { head, field: i + 1, bytes, stream };
}
/** Pool pages used by the stored saves (heads: slot index -> header page or null), except slot skip. */
function poolUsed(f, heads, skip) {
  const used = new Set();
  heads.forEach((h, j) => {
    if (j === skip || !h) return;
    const p = slotParse(f, j, h); if (!p) return;
    if (p.pool) for (let k = 0; k < u32(h, 24); k++) used.add(h[DPG_OFF + 2 * k] | (h[DPG_OFF + 2 * k + 1] << 8));
    else for (let k = 0; k < Math.ceil(p.bytes / 0x1000); k++) used.add(j * poolPages(f) + k);
  });
  return used;
}
/** Field number a settings block (4 KB) stands for, -1 unknown. */
function fieldOfSettings(rom, p) {
  const e = edition(rom); if (!e || !p) return -1;
  const P = profileOf(rom, e.profile);
  let f = p[P.fieldByte];
  if (P.six && f < 4) f = P.sixMap[(p[P.six] === 1 ? 4 : 0) + f];
  return f < P.names.length ? f : -1;
}
/**
 * The save slots of the menu for the Studio (1.6.8): { f, saves (numbered saves or one per field), live (slot of the
 * save now in use, it is not stored in a slot), list: [{ i, used }] } or null without slots.
 */
function saveSlots(rom, tpl) {
  const f = feldFind(rom, tpl);
  if (!f || !(f.flags & F_FIELDS) || f.nslots < 1) return null;
  const saves = f.version >= 6 && (f.flags & F_SAVES) !== 0 && f.nslots >= 2, list = [];
  for (let i = 0; i < f.nslots; i++) list.push({ i, used: !!slotParse(f, i, rom.subarray(slotAddr(f, i), slotAddr(f, i) + 0x1000)) });
  let live = -1;
  if (saves) {
    const v = rom[0xEFE000 + 0xF06];
    if (v >= 1 && v <= f.nslots) live = v - 1; else { const fr = list.find((x) => !x.used); live = fr ? fr.i : 0; }
  } else live = fieldOfSettings(rom, rom.subarray(0xEFE000, 0xEFF000));
  return { f, saves, live, list };
}
function feldSlots(rom, tpl) {
  const f = feldFind(rom, tpl);
  if (!f || !(f.flags & F_FIELDS)) return [];
  const out = [];
  for (let i = 0; i < f.nslots; i++) {
    const s = slotRead(rom, f, i);
    if (!s) continue;
    // Slot ID: from menu version 3 the field number + 1, before that (v0.9.4/v0.9.5) the edition ID
    out.push(Object.assign(s, { index: f.version >= 3 ? s.field - 1 : i }));
  }
  return out;
}

// ---------------------------------------------------------------- catalog
/*
 * Items from the user's own image, as in the TamaParadise app (Bilder.kt):
 * picture table found by its shape, item table (46-byte records), English
 * text table found by its marker words. Name = text number in the record plus one.
 */
const SATZ = 46, CH_A = 318;
const SONDER = { 305: ' ', 317: ' ', 375: '&', 376: "'", 381: '.', 370: '!', 388: '?', 382: '/' };
for (let i = 0; i < 10; i++) SONDER[306 + i] = String(i);   // 1.6.8: digits (names changed in the Studio)
const WORTE = ['FOOD', 'SNACKS', 'OUTSIDE TOY', 'LAB LOG', 'MILK'];
/*
 * The image table of the ROM. Only searched inside the asset archive (from 0x111000 to
 * its end): programs built in by the Studio (DOOM, TamaEmu) contain tables that look
 * alike, and DOOM sits before the archive's image table.
 */
function bildTabelle(d) {
  const end = Math.min(d.length, assetEnd(d));
  for (let o = 0x111000; o + 4 < end; o += 4) {
    const e0 = u32(d, o);
    if (e0 < 0x1000 || e0 > 0x8000 || e0 % 4) continue;
    let prev = e0, ok = true;
    for (let i = 1; i < 60; i++) { const w = u32(d, o + 4 * i); if (w <= prev || w > 0x1000000) { ok = false; break; } prev = w; }
    if (!ok) continue;
    const k = o + e0;
    if (k + 24 >= d.length) continue;
    const g = u32(d, k), p = u32(d, k + 20);
    if (p > 24 && p < g && g >= 1 && g <= 0x20000 && p % 8 === 0 && d[k + 8] > 0 && d[k + 9] > 0) return o;
  }
  return -1;
}
function itemTabelle(d, nb) {
  let best = -1, bn = 0;
  for (let o = 0; o + SATZ < d.length; o += 2) {
    const k0 = u16(d, o);
    if (k0 < 5001 || k0 > 9999) continue;
    const seen = new Set(); let n = 0;
    while (n < 400 && o + n * SATZ + 5 < d.length) {
      const k = u16(d, o + n * SATZ), b = u16(d, o + n * SATZ + 4);
      if (k < 5001 || k > 9999 || b >= nb || seen.has(k)) break;
      seen.add(k); n++;
    }
    if (n > bn) { bn = n; best = o; }
    if (n > 8) o += n * SATZ - 2;
  }
  return [best, bn];
}
function zeichenkette(d, t, n) {
  const ab = u16(d, t + 2 * n);
  if (ab <= 0 || ab > 0x8000) return '';
  let o = t + 2 * ab, s = '';
  while (s.length < 40) {
    const c = u16(d, o);
    if (c === 0) break;
    if (c >= CH_A && c < CH_A + 26) s += String.fromCharCode(65 + c - CH_A);
    else if (SONDER[c] !== undefined) s += SONDER[c];
    else return '';
    o += 2;
  }
  return s.trim();
}
function wort3(d, t, n) {
  const ab = u16(d, t + 2 * n);
  if (ab <= 0 || ab > 0x8000) return false;
  for (let k = 0; k < 3; k++) { const c = u16(d, t + 2 * ab + 2 * k); if (c < CH_A || c >= CH_A + 26) return false; }
  return true;
}
function textTabelle(d) {
  let best = -1, bp = 0;
  for (let o = 0; o < d.length - 8; o += 2) {
    const n = u16(d, o);
    if (n < 100 || n > 4000 || !(u16(d, o + 2) > n && u16(d, o + 4) > u16(d, o + 2) && u16(d, o + 6) > u16(d, o + 4)) || !wort3(d, o, 1)) continue;
    let prev = n, ok = true;
    for (let i = 1; i < Math.min(n, 200); i++) { const w = u16(d, o + 2 * i); if (w < prev || w > 0x8000) { ok = false; break; } prev = w; }
    if (!ok) continue;
    let p = 0; for (let k = 1; k < 120; k++) if (WORTE.includes(zeichenkette(d, o, k))) p++;
    if (p > bp) { bp = p; best = o; }
  }
  return bp >= 2 ? best : -1;
}
/*
 * Byte sum of an item picture entry, as the menu computes it after copying the entry
 * to RAM (header, sub-image table, palette, pixel data of all sub-images). 0 = unknown.
 */
function picSum(d, P, pic) { const i = picInfo(d, P, pic); return i ? i.sum : 0; }
/* Where the picture entry lies in flash, its length, byte sum and first word (or null). */
function picInfo(d, P, pic) {
  const n = u32(d, P) / 4; if (pic >= n) return null;
  const e = P + u32(d, P + 4 * pic); if (e & 3) return null;
  const flags = d[e + 4], bpp = d[e + 5], sw = d[e + 8], sh = d[e + 9];
  if (bpp > 3 || !sw || !sh) return null;
  const iw = d[e + 12] || 1, ih = d[e + 13] || 1, cnt = iw * ih, bits = 1 << bpp, mask = (1 << bits) - 1;
  const need = (sw * sh * bits + 7) >> 3, po = u16(d, e + 18), px = u16(d, e + 20);
  let end = px + 8 * cnt;
  for (let c = 0; c < cnt; c++) {
    let x;
    if (flags & 0x60) { const w = u32(d, e + px + 8 * c), off = w & 0x7FFFFFFF, ln = u32(d, e + px + 8 * c + 4); x = px + off + ((w & 0x80000000) ? need : ln); }
    else x = px + (c + 1) * need;
    if (x > end) end = x;
  }
  end = Math.max(end, po + 2 * (mask + 1));
  if (end > 12288 || e + end > d.length) return null;
  let sum = 0; for (let i = 0; i < end; i++) sum += d[e + i];
  return { addr: e, len: end, sum: (sum & 0xFFFF) || 1, head: u32(d, e) };
}
/** Catalog: [{ id, fixed, pic, sum, name }] ascending, only groups 5 to 8. */
function itemCatalog(d) {
  const bt = bildTabelle(d);
  if (bt < 0) return [];
  const [it, n] = itemTabelle(d, u32(d, bt) / 4);
  if (it < 0 || n < 100) return [];
  const tt = textTabelle(d);
  const out = [];
  for (let i = 0; i < n; i++) {
    const o = it + i * SATZ, id = u16(d, o);
    if (id < 5001 || id > 8999) continue;
    const pic = u16(d, o + 4);
    const pi = picInfo(d, bt, pic);
    out.push({ id, fixed: u16(d, o + 24) === 0xFFFF, pic, sum: pi ? pi.sum : 0, at: pi, text: u16(d, o + 2) + 1, name: tt >= 0 ? zeichenkette(d, tt, u16(d, o + 2) + 1) : '' });
  }
  out.sort((a, b) => a.id - b.id);
  out.pack = bt;
  return out;
}
/*
 * Characters for the TAMA menu (1.6.8): kid, young and adult of the character table with their
 * English names. fixed 2, stage, field = index + 1 in the menu's field list (P.names), 0 = none.
 */
const FIELD_DEFAULT = new Set(['LAND', 'WATER', 'SKY', 'FOREST', 'TROPICAL', 'ICE', 'GLACIER']);
/** Text numbers of the field names (1.6.8): the three fields at 14..16, the further ones of six fields at 64..66 */
const FIELD_TEXT = [14, 15, 16, 64, 65, 66];
const TYPE_NAMES = { 1: 'LAND', 2: 'WATER', 3: 'SKY', 4: 'FOREST', 5: 'TROPICAL', 6: 'GLACIER' };
function charCatalog(d, P) {
  const C = typeof ParamixCore !== 'undefined' ? ParamixCore : (typeof require !== 'undefined' ? require('./core.js') : null);
  if (!C) return [];
  let ctx; try { ctx = C.openRom(d); } catch (e) { return []; }
  const tt = textTabelle(d), out = [];
  for (const c of C.charTable(ctx.files[0])) {
    if (c.id < 2000 || c.id > 4999 || c.stage < 3 || c.stage > 5 || !TYPE_NAMES[c.field]) continue;
    const nm = (tt >= 0 ? zeichenkette(d, tt, c.name) : '') || ('NO. ' + c.id);
    const fi = P.names.indexOf(TYPE_NAMES[c.field]);
    out.push({ id: c.id, fixed: 2, pic: 0, sum: (c.stage & 255) | ((fi + 1) << 8), at: null, name: nm.replace(/\s+/g, ' ') });
  }
  return out;
}
/** Characters the TAMA menu offers (for the Studio's save editor too): [{ id, stage, field, name }] */
function characters(d) {
  const e = edition(d); if (!e) return [];
  const P = profileOf(d, e.profile);
  return charCatalog(d, P).map((c) => ({ id: c.id, stage: c.sum & 255, field: (c.sum >> 8) - 1, name: c.name }));
}
/*
 * Catalog: 8 byte header, 28 bytes per entry, then "PEXT" and 12 bytes per entry:
 * flash address of the picture entry, its length, byte sum, first word. The menu
 * reads the picture straight from there (no lookup in the ROM's image table).
 */
const EXT_MAGIC = 0x54584550;
function catalogBlob(cat) {
  const x = 8 + 28 * cat.length;
  const b = new Uint8Array(x + 4 + 12 * cat.length);
  w32(b, x, EXT_MAGIC);
  cat.forEach((c, i) => { const o = x + 4 + 12 * i; if (c.at) { w32(b, o, c.at.addr); w16(b, o + 4, c.at.len); w16(b, o + 6, c.at.sum); w32(b, o + 8, c.at.head); } });
  w32(b, 0, CAT_MAGIC); w32(b, 4, cat.length);
  cat.forEach((c, i) => {
    const o = 8 + 28 * i, nm = c.name.slice(0, 20);
    w16(b, o, c.id); b[o + 2] = c.fixed === 2 ? 2 : c.fixed ? 1 : 0; b[o + 3] = nm.length; w16(b, o + 4, c.pic); w16(b, o + 6, c.sum || 0);
    for (let k = 0; k < nm.length; k++) b[o + 8 + k] = nm.charCodeAt(k);
  });
  return b;
}

// ---------------------------------------------------------------- install and remove
function doomCodeEnd(rom) {
  let e = DOOM_SAVE;
  while (e > DOOM_CODE && rom[e - 1] === 0xFF) e--;
  return page(e);
}
function assetEnd(rom) { return 0x111000 + 16 + u32(rom, 0x111000 + 8); }

/** Remove the menu (program, catalog, slots, stub). Call feldSlots() first if the saves should be kept. */
function feldStrip(rom, emb, E) {
  const out = new Uint8Array(rom);
  const f = feldFind(out, emb.stub);
  if (!f) return out;
  const tab = E.sceneTable(out);
  launchRestore(out, f.stubOff);   // start device: scene handlers back, trampolines gone (before the menu area is erased)
  const prev = f.stubOff === STUB_OFF ? E.installedOrigs(out, tab) : [0, 1, 2, 3].map((k) => u32(out, f.stubOff + 0x94 + 4 * k));
  // Program and catalog
  let end = f.off + (f.version >= 2 ? 0x8000 : 0x4000);
  if (f.catOff > f.off && u32(rom, f.catOff) === CAT_MAGIC) {
    let ce = f.catOff + 8 + (f.version >= 3 ? 28 : 24) * f.catN;
    if (u32(rom, ce) === EXT_MAGIC) ce += 4 + 12 * f.catN;
    end = Math.max(end, page(ce));
  }
  if (f.slotOff > f.off && f.slotOff < end) end = f.slotOff;
  end = Math.min(end, f.stubOff === STUB_OFF && f.off < FELD_END ? FELD_END : (isSix(out) ? CODE_END : 0x110000));
  out.fill(0xFF, f.off, end);
  // Slots: never erase into the character data
  if (f.nslots && f.slotSize) {
    if (f.slotOff2 && f.slotN1) {
      for (let i = 0; i < f.nslots; i++) { const b = slotAddr(f, i); if (b >= page(assetEnd(out)) || b < 0x111000) out.fill(0xFF, b, b + f.slotSize); }
    } else {
      const from = f.slotOff < 0x800000 ? f.slotOff : Math.max(f.slotOff, page(assetEnd(out)));
      out.fill(0xFF, from, f.slotOff + f.nslots * f.slotSize);
    }
  }
  // the flash app of the start device (version 7)
  if (f.version >= 7) { const d = u32(out, f.off + 0x140 + 500); if (d >= 0x10000000 && d < 0x11000000) { const o = d - 0x10000000 + XIP_DEC; if (o >= 0x111000 && o + 0x10000 <= out.length) out.fill(0xFF, o, o + 0x10000); } }
  if (prev) {
    if (f.stubOff === STUB_OFF && appKind(out) && emb.oldStub) return E.installApp(out, out.slice(APP_OFF, APP_OFF + 0x10000), emb.oldStub, { keep: true });   // 1.6.8: before, this freed the blocks of T18Emu
    prev.forEach((v, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, v));
    out.fill(0xFF, f.stubOff, f.stubOff + 0x1000);
  }
  return out;
}

/**
 * Install the menu into an image without a menu (after emulator/DOOM).
 * opts: { fields, bag }, saved: feldSlots() of the source ROM,
 * emb: { feld, stub } (embedded), E: ParamixEmu. Returns a new image.
 */
function feldApply(rom, opts, saved, emb, E) {
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB');
  const out = new Uint8Array(rom);
  if (feldFind(out, emb.stub)) throw new Error('the menu is already built in');
  const ed = edition(out);
  if (!ed) throw new Error('edition not recognised');
  const P = profileOf(out, ed.profile);
  const v6 = u32(emb.feld, findWord(emb.feld, FELD_MAGIC) + 4) >= 6;
  const launch = !!opts.launch;
  const fields = !!opts.fields && !launch, bag = !!opts.bag && !launch, tama = !!opts.tama && v6 && !launch, saves = fields && v6 && opts.saves !== false;
  if (!fields && !bag && !tama && !launch) throw new Error('nothing chosen');
  if (launch && (u32(emb.stub, LAUNCH_ENTRY) !== 0x4C01B672 || (u32(emb.stub, LCHECK_ENTRY) & 0xF800) !== 0xF000)) throw new Error('launcher template does not fit');   // cpsid i; ldr r4, feld_src / b.w lcheck
  const tab = E.sceneTable(out);
  if (!tab.PSID_HOME) throw new Error('home screen not found');
  const prev = E.installedOrigs(out, tab);
  const kind = appKind(out);
  const stubOff = prev || kind ? STUB_OFF : freeStubOff(out);
  if (!prev && !isFF(out, stubOff, 0x1000)) throw new Error('no room for the launcher');
  const catList = bag ? itemCatalog(out) : [];
  if (tama) { const pack = catList.pack; catList.push(...charCatalog(out, P)); catList.sort((a, b) => a.id - b.id); catList.pack = pack; }
  const cat = bag || tama ? catalogBlob(catList) : new Uint8Array(0);
  const fsz = page(emb.feld.length), csz = page(cat.length);
  const doom = kind === 1;
  let prog = doom ? doomCodeEnd(out) : stubOff + 0x1000;
  // DOOM with T18Emu as a further program (start device, 1.6.8): its flash part at 0x0F0000 lies in DOOM's free end
  const tsz = launch ? launchSize(out, E.sceneTable(out)) : 0;   // start device: trampoline pages behind the menu
  if (doom) while (prog + fsz + csz + tsz <= DOOM_SAVE && !isFF(out, prog, fsz + csz + tsz)) prog += 0x1000;
  const tOff = page(prog + fsz + csz);
  const catOff = cat.length ? prog + fsz : 0;

  const nslots = fields ? P.ids.length : 0;
  let slotOff = 0, slotSize = 0, slotOff2 = 0, slotN1 = 0;
  if (doom) {
    if (prog + fsz + csz + tsz > DOOM_SAVE) throw new Error('no room for the menu next to DOOM');
    if (fields) {
      slotOff = prog + fsz + csz;
      slotSize = Math.floor((DOOM_SAVE - slotOff) / nslots / 0x1000) * 0x1000;
      if (slotSize < 0x8000) throw new Error('no room for the saves next to DOOM');
    }
  } else {
    if (prog + fsz + csz + tsz > (stubOff === STUB_OFF ? FELD_END : CODE_END)) throw new Error('menu program too big');
    if (!isFF(out, prog, fsz + csz + tsz)) throw new Error('the area for the menu is in use');
    if (fields && P.six) {
      // six fields: slots behind the menu (up to the helper page) and behind the character data
      const a1 = page(prog + fsz + csz), r1 = CODE_END - a1, a2 = page(assetEnd(out)), r2 = P.idPage - 0x1000 - a2;
      for (let s = SLOT_SIZE; s >= 0x10000; s -= 0x1000) {
        const n1 = Math.max(0, Math.floor(r1 / s)), n2 = Math.max(0, Math.floor(r2 / s));
        if (n1 + n2 >= nslots) { slotSize = s; slotN1 = Math.min(n1, nslots); slotOff = slotN1 ? a1 : a2; slotOff2 = slotN1 && slotN1 < nslots ? a2 : 0; if (!slotN1) slotN1 = 0; break; }
      }
      if (!slotSize) throw new Error('no room for the saves');
    } else if (fields) {
      slotOff = Math.max(SLOT_BASE, page(assetEnd(out)));
      // the emulator's firmware starts at 0x940000, a big one (P's, 4U+) already at 0x840000 (1.3.2)
      // T18Emu (1.5.1) puts its firmware from the top down: the saves get the room below its lowest block
      const T18 = 0x38584D50, t18low = u32(out, 0xA0140) === T18 && E && E.t18Low ? E.t18Low(out) : 0;
      const limit0 = kind === 2 ? (u32(out, 0xA0140) === PMXC ? Math.min(u32(out, 0xA0148), GUEST_OFF) : t18low ? Math.min(t18low, P.idPage - 0x1000) : GUEST_OFF) : P.idPage - 0x1000;
      const limit = opts.appsLow ? Math.min(limit0, opts.appsLow) : limit0;
      if (limit - slotOff < nslots * 0x10000) throw new Error('no room for the field saves next to this emulator firmware');
      // all the room up to the emulator or the device's save area (1.2.11; before at most 0x58000 each)
      slotSize = Math.floor((limit - slotOff) / nslots / 0x1000) * 0x1000;
      if (slotSize < 0x10000) throw new Error('no room for the saves');
    }
  }
  const fs = { slotOff, slotSize, slotOff2, slotN1 };
  if (fields) for (let i = 0; i < nslots; i++) if (!isFF(out, slotAddr(fs, i), slotSize)) throw new Error('the area for the saves is in use');
  // Program with settings
  const f = new Uint8Array(emb.feld);
  const ci = findWord(f, FELD_MAGIC);
  if (ci < 0 || u32(f, ci + 4) < 3) throw new Error('settings block of the menu missing');
  w32(f, ci + 8, slotOff); w32(f, ci + 12, slotSize); w32(f, ci + 16, nslots);
  w32(f, ci + 20, P.idPage + 0x1000); w32(f, ci + 24, P.six ? 0xF00000 : 0x1000000);   // six fields: rain pictures from 0xF00000
  w32(f, ci + 28, P.idPage);   // ID page (1.2.2 to 1.2.11 lost this line: Tropical/Glacier wrote the ID to 0xD49000)
  w32(f, ci + 32, stubOff); w32(f, ci + 36, (fields ? F_FIELDS : 0) | (bag ? F_BAG : 0) | (tama ? F_TAMA : 0) | (saves ? F_SAVES : 0) | (launch ? F_LAUNCH : 0));
  w32(f, ci + 40, P.c); w16(f, ci + 44, P.h[0]); w16(f, ci + 46, P.h[1]);
  // 1.6.8: a field renamed in the Studio (text table) shows its new name in the menu too
  const shownName = (i) => { const x = (opts.fieldNames || [])[i]; return (x && !FIELD_DEFAULT.has(x) ? x.slice(0, 11) : P.names[i]) || ''; };
  w32(f, ci + 48, P.ids.length);
  for (let i = 0; i < 4; i++) {
    w32(f, ci + 52 + 4 * i, P.ids[i] || 0);
    const nm = shownName(i);
    for (let k = 0; k < 12; k++) f[ci + 68 + 12 * i + k] = k < nm.length ? nm.charCodeAt(k) : 0;
    w32(f, ci + 116 + 4 * i, P.cols[i] || 0xFFFF);
  }
  for (let k = 0; k < 16; k++) { const p = P.common[k]; w16(f, ci + 132 + 4 * k, p ? p[0] : 0); f[ci + 134 + 4 * k] = p ? p[1] : 0; f[ci + 135 + 4 * k] = 0; }
  for (let i = 0; i < 4; i++) for (let k = 0; k < 4; k++) {
    const p = (P.field[i] || [])[k], o = ci + 196 + 16 * i + 4 * k;
    w16(f, o, p ? p[0] : 0); f[o + 2] = p ? p[1] : 0; f[o + 3] = 0;
  }
  w32(f, ci + 260, P.bag[0]); w32(f, ci + 264, P.bag[1]); w32(f, ci + 268, P.bag[2]);
  w32(f, ci + 272, catOff); w32(f, ci + 276, cat.length ? u32(cat, 4) : 0);
  w32(f, ci + 280, P.fieldByte); w32(f, ci + 284, bag && catList.pack > 0 ? catList.pack : 0);
  if (P.six) {
    if (u32(f, ci + 4) < 4) throw new Error('menu program too old for six fields');
    w32(f, ci + 288, P.six);
    for (let i = 4; i < 6; i++) {
      w32(f, ci + 292 + 4 * (i - 4), P.ids[i] || 0);
      const nm = shownName(i);
      for (let k = 0; k < 12; k++) f[ci + 300 + 12 * (i - 4) + k] = k < nm.length ? nm.charCodeAt(k) : 0;
      w32(f, ci + 324 + 4 * (i - 4), P.cols[i] || 0xFFFF);
      for (let k = 0; k < 4; k++) { const p = (P.field[i] || [])[k], o = ci + 332 + 16 * (i - 4) + 4 * k; w16(f, o, p ? p[0] : 0); f[o + 2] = p ? p[1] : 0; f[o + 3] = 0; }
    }
    w32(f, ci + 364, slotOff2); w32(f, ci + 368, slotOff2 ? slotN1 : 0);
    P.sixMap.forEach((v, k) => { f[ci + 372 + k] = v; });
  }
  // more programs (1.6.0, version 5): name of the stub's program and the list the menu can start
  if (u32(f, ci + 4) >= 5) {
    const nm = (opts.name0 || '').slice(0, 12);
    for (let k = 0; k < 12; k++) f[ci + 380 + k] = k < nm.length ? nm.charCodeAt(k) : 0;
    const max = u32(f, ci + 4) >= 8 ? 12 : 6, apps = (opts.apps || []).slice(0, max);   // 1.6.8 (version 8): 12
    if ((opts.apps || []).length > max) throw new Error('at most ' + max + ' more programs');
    w32(f, ci + 392, apps.length);
    for (let i = 0; i < max; i++) {
      const a = apps[i], o = i < 6 ? ci + 396 + 16 * i : ci + 504 + 16 * (i - 6);
      w32(f, o, a ? a.src : 0);
      for (let k = 0; k < 12; k++) f[o + 4 + k] = a && k < a.name.length ? a.name.charCodeAt(k) : 0;
    }
  } else if (opts.apps && opts.apps.length) throw new Error('menu program too old for more programs');
  // TAMA menu (1.6.8, version 6): where the pet and the planet level lie in the settings block
  if (u32(f, ci + 4) >= 6) { w32(f, ci + 492, tama ? P.tama : 0); w32(f, ci + 496, tama ? P.level : 0); }
  // DOWNLOAD in the start device (1.6.8, version 7): the flash app placed by appsPlace
  if (u32(f, ci + 4) >= 7) w32(f, ci + 500, launch && opts.dl ? opts.dl : 0);
  else if (launch && opts.dl) throw new Error('menu program too old for the start device');
  // 1.6.9 test13 (version 9): the main program of the start device (one of the list, shown first) and its autostart
  if (u32(f, ci + 4) >= 9) { w32(f, ci + 600, launch && opts.mainSrc ? opts.mainSrc : 0); w32(f, ci + 604, launch && opts.auto ? 1 : 0); }
  out.set(f, prog);
  if (cat.length) out.set(cat, catOff);
  // carry over saved slots (the active field lives in the save itself)
  if (fields) { const used = new Set(), g = Object.assign({ nslots }, fs);
    for (const s of saved || []) {
      const i = s.index;
      if (i < 0 || i >= nslots) continue;
      try { slotPlace(out, g, i, s, used); } catch (e) { throw new Error('the stored save of ' + cap(P.names[i]) + ' does not fit here'); }
    } }
  // Stub
  const origs = prev || [0, 1, 2, 3].map((k) => u32(out, tab.PSID_HOME + 12 + 4 * k));
  const st = new Uint8Array(emb.stub);
  const put = (ph, v) => { let n = 0; for (let i = 0; i + 4 <= st.length; i += 2) if (u32(st, i) === ph) { w32(st, i, v); n++; } if (n !== 1) throw new Error('launcher template does not fit'); };
  put(PH_APP, kind ? xip(APP_OFF) : 0);
  put(PH_FELD, xip(prog));
  PH_ORIG.forEach((ph, k) => put(ph, origs[k]));
  { const ia = findWord(st, PH_AUTO); if (ia >= 0) w32(st, ia, 0); }   // autostart off, the Studio sets it at the end
  out.fill(0xFF, stubOff, stubOff + 0x1000);
  out.set(st, stubOff);
  HOOK_OFF.forEach((h, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, (xip(stubOff) + h) | 1));
  if (launch) launchRedirect(out, tab, stubOff, tOff);
  return out;
}

const FELD_B64 = '__FELD_B64__', FELD_STUB_B64 = '__FELD_STUB_B64__';
function embedded() {
  if (FELD_B64.startsWith('__')) return null;
  return { feld: b64(FELD_B64), stub: b64(FELD_STUB_B64) };
}

function emuType(rom) { const m = u32(rom, APP_OFF + 0x140); return m === 0x37584D50 ? 't7' : m === 0x38584D50 ? 't18' : m === 0x31584D50 ? 'v1' : m === PMXC && appKind(rom) === 2 ? 'tama' : ''; }
root.ParamixFeld = { saveSlots, fieldOfSettings, lzPack, lzUnpack, slotPages, slotBuild, poolUsed, slotParse, FIELD_TEXT, characters, F_TAMA, F_SAVES, F_LAUNCH, isLaunch, launchRestore, emuType, familyOf, isSix, slotAddr, slotReadAsync, slotPlace, poolAddr, textTabelle, zeichenkette, edition, feldId, canFields, fieldNames, fieldNow, feldFind, feldSlots, feldStrip, feldApply, feldApps, appKind, itemCatalog, embedded, NAMES, F_FIELDS, F_BAG };
if (typeof module !== 'undefined') module.exports = root.ParamixFeld;
})(typeof window !== 'undefined' ? window : globalThis);
