/*
 * Paramix Studio: six fields on Tropical/Glacier.
 *
 * The Tropical/Glacier firmware has three field slots (Tropical, Glacier, Forest;
 * byte 499 of the settings block). It already holds the characters of all six
 * fields, only the field pictures of Land, Water and Sky are missing. This module
 *   - copies those pictures from the user's own Land, Water or Sky ROM into the
 *     archive (8 bit maps, colours reduced to 128 per field, identical tiles shared),
 *   - patches four places of the firmware so that a flag byte in the settings block
 *     (offset 0xF04) switches the three slots to Land, Water, Sky: backgrounds,
 *     child, teen and adult characters,
 *   - writes the helper code and its tables to flash 0x10F000 (source in six/src).
 * The menu (feld.js) then offers six fields; a field of the second three is
 * slot 0..2 plus the flag. Nothing of a Land ROM is shipped: everything comes from
 * the ROM the user opens.
 *
 * Author: mameise
 */
(function (root) {
'use strict';
const C = root.ParamixCore || (typeof require !== 'undefined' ? require('./core.js') : null);

const SIX_OFF = 0x10F000, SIX_XIP = 0x100FE000, SIX_MAGIC = 0x31584953;
const FLAG_OFF = 0xF04;                    // set byte in the settings block
const XIP = (off) => 0x10000000 + off - 0x11000;
const SIX_VERSION = 7;
/* DNA per field (1.7.2, switch): the DNA history and the per slot values of the firmware follow the six
 * fields. An add-on of the version 7 page (code from 0xC00), so a ROM with six fields gets it without the
 * Land ROM. Option bit 3 in the header. */
/* 1.7.4 SIX_FOOD (switch): food per field. Land, Water, Sky food counted for the young stage (the
 * firmware took the Land, Water, Sky characters for its own fields). Part of the DNA part. */
const SIX_FOOD = true;
const SIX_DNA = true, P_DNA = 0xC00, P_DNA_FN = 0xC40, DNA_CHILD = 7, DNA_FSET = 9, DNA_END = 0x1000;
// page layout (six/src/six.s)
const P_FM = 0x800, P_FM_FN = 0x830, P_PMAP = 0x1B0, P_BG = 0x1C0, P_CHILD = 0x1CC, P_FN = 0x1E0, T_BG = 0x200, T_CHILD = 0x340, T_CHILD_O = 0x348, T_SITES = 0x380, T_MORE = 0x500;
const FN_TAB = [0, 1, 5], FN_BG = 2, FN_CHILD = 3, FN_PMAP = 4;
// Region B (0xF00000, unused by the firmware): cells of the rain pictures (Tropical/Glacier). Header
// "SIXB", count, per picture: sprite id and 64 offsets into the data (0xFFFFFFFF = cell in the archive).
const B_OFF = 0xF00000, B_END = 0xFF0000, B_MAGIC = 0x42584953;   // 1.6.13 test13: 0xFF0000 stays free for the rescue start (was 0x1000000)
// Tropical maps reduced to 128 colours to make room (look the same)
const TROP_MAPS = [[1118, 7], [1132, 7], [1146, 7]];
const K_COLORS = 128;
/*
 * The firmwares. bg: function that returns the background composites of a field (replaced by a
 * jump), child: code that picks the child from a list in the code, sites: movw/movt pairs that
 * load a per field table (offset in the firmware window, register, table, name of the
 * alternative table). Offsets are relative to flash 0x11000.
 */
const KINDS = {
  gen3: { id: 0, idPage: 0xD29000, ids: [5, 6], flag: 0x18000BE8 + 0xF00, slots: [0, 1, 2],
    bg: { at: 0xAEAC, orig: '81b08df8', table: 0x10092E06, mode: 0 },
    child: { at: 0xFD62, orig: '40f61500adf8', back: 0xFD7C, sp: 0xC4, list: [2091, 2113, 2069] },
    sites: [
      [0xFED2, 1, 0x10091314, 'teen'], [0xFFBC, 1, 0x1009132C, 'adult'],
      [0x3FBBC, 1, 0x100A012C, 'speed'], [0x3FDD2, 1, 0x100A012C, 'speed'], [0x3FEA0, 1, 0x100A012C, 'speed'],
      [0x432AE, 1, 0x10095790, 'egg'], [0x4366C, 0, 0x10095790, 'egg'], [0x435C0, 1, 0x10093382, 'eggPic'],
      [0x4307C, 1, 0x10095778, 'wagon'],
      [0x560B6, 1, 0x1009472C, 'scene'], [0x56316, 1, 0x1009472C, 'scene'], [0x56624, 1, 0x1009472C, 'scene'],
      [0x7FEA4, 0, 0x1009472C, 'scene'], [0x7FED2, 0, 0x1009472C, 'scene'],
      // special adult per field: two halfwords per movw/movt (Tropical/Glacier: 4112, 4134, 4090)
      [0x10060, 0, 0x0FFA0FFA, 'spec2'], [0x1006A, 0, 0x10261010, 'spec1'],
    ],
    // planet: calls of the edition-to-row function, the field byte, row per field slot, and the
    // two planet stage tables (3 rows of 11 stages, and the same rows loaded one by one)
    planet: { calls: [0x7C7A, 0x7C98, 0x7CB4, 0x575FA], fn: 0x1002B314, field: 0x18000BE8 + 0x1EF, rows: [0, 1, 2],
      sites: [[0x7CC4, 2, 0x100A1020, 'plTab'], [0x57636, 1, 0x10094786, 'pl0'], [0x5768E, 1, 0x100947B2, 'pl1'],
        [0x576E6, 1, 0x1009479C, 'pl2'], [0x5773E, 1, 0x10094786, 'pl0']] },
    // the game's field menu: menu object pointer, field byte, planet level, list builder call,
    // preview loader calls, reads of the list (compare with the field, switch the field),
    // immediates that move the index and count (old, new), offsets list/index/count/flag, entries
    fm: { obj: 0x1801417C, field: 0x18000BE8 + 0x1EF, level: 0x18000BE8 + 0x84, build: [0x265E2, 0x100167D8],
      prev: [0x100856D4, 0x414EC, 0x41818, 0x41B38], cmp: [0x41690, 0x41782, 0x4183A, 0x418EA], set: 0x416B8, read: '90f94c00',
      imm: [[0x265EA, 0x50, 0x48], [0x41496, 0x4F, 0x53], [0x41618, 0x4F, 0x53]], off: [0x4C, 0x48, 0x53, 0x68], ent: [0, 1, 2, 0x80, 0x81, 0x82] },
    // DNA per field (1.7.2, helper page DNA part): settings block, offsets of the DNA count and the
    // field byte, stash and marker (RAM offsets, free in the settings block), byte with one bit per
    // slot, regions kept per slot (offset, length), DNA picture per field (Tropical, Glacier, Forest,
    // Land, Water, Sky), and the firmware places: [XIP, original bytes, how, helper number or bytes]
    dna: { base: 0x18000BE8, cnt: 0x131, field: 0x1EF, stash: 0xF0C, mark: 0xF0B, bits: 133, mask: 7, slots: 7,
      regions: [[256, 6], [350, 6], [422, 72], [500, 12]], icons: [1184, 1185, 1180, 1165, 1166, 1167],
      sites: [[0x10081C68, '80b590b0', 'bw', 0], [0x10081CEC, '80b590b0', 'bw', 1], [0x1000AE44, '81b08df8', 'bw', 2],
        [0x1003BFB2, '0328', 'hex', '0628'], [0x1008211C, 'fff7a4fd', 'bl', 3], [0x10082124, '0228', 'hex', '0528'],
        [0x10013BBE, '90f8ef01', 'bl', 4], [0x10072676, '90f8ef01', 'bl', 4], [0x10013C78, '91f8ef01', 'bl', 5],
        [0x10013C3E, '91f9ef11', 'bl', 6], [0x10013F42, '91f9ef11', 'bl', 6], [0x10068B54, '28f018f981790ca830f81100', 'bl', 8]],
      // food per field: history offset, first food kind per field 0..5, check of the kind, young stage branch
      food: { hist: 0x13E, kinds: [12, 15, 9, 0, 3, 6], sites: [[0x1001027E, '0df055fd', 'bl', 10], [0x10081D70, '80b596b0', 'bw', 11]] } } },
  jade: { id: 1, idPage: 0xD49000, ids: [4], flag: 0x18000BA0 + 0xF00, slots: [1],
    bg: { at: 0x966C, orig: '81b08df8', table: 0x1007F98C, mode: 1 },
    child: { at: 0xE1DA, orig: '40f61500adf8', back: 0xE1F4, sp: 0xBC, list: [2002, 2003, 2069] },
    sites: [
      [0xE3F2, 1, 0x1007DFC4, 'teen'], [0xE536, 1, 0x1007DFDC, 'adult'],
      [0x3AA58, 1, 0x1008BCE8, 'speed'], [0x3AC6E, 1, 0x1008BCE8, 'speed'], [0x3AD3C, 1, 0x1008BCE8, 'speed'],
      [0x3DE22, 1, 0x100820E8, 'egg'], [0x3DBF4, 1, 0x100820D0, 'wagon'],
      [0x4F1FE, 1, 0x10081198, 'scene'], [0x4F45E, 1, 0x10081198, 'scene'], [0x4F76C, 1, 0x10081198, 'scene'],
      [0x7164C, 0, 0x10081198, 'scene'], [0x7167A, 0, 0x10081198, 'scene'],
      [0xE5E2, 0, 0x0FE30FE2, 'spec1'],
    ],
    planet: { calls: [0x670A, 0x6728, 0x6744, 0x50742], fn: 0x10026E10, field: 0x18000BA0 + 0x1B7, rows: [0xFF, 1, 0xFF],
      sites: [[0x6754, 2, 0x1008CBD0, 'plTab'], [0x5077E, 1, 0x100811DA, 'pl0'], [0x507D6, 1, 0x10081206, 'pl1'],
        [0x5082E, 1, 0x100811F0, 'pl2'], [0x50886, 1, 0x100811DA, 'pl0']] },
    fm: { obj: 0x180140B8, field: 0x18000BA0 + 0x1B7, level: 0x18000BA0 + 0x78, build: [0x2262C, 0x10014954],
      prev: [0x1007564C, 0x3C2E0, 0x3C5C2], cmp: [0x3C470, 0x3C53E, 0x3C5EA, 0x3C69A], set: 0x3C498, read: '90f94a00',
      imm: [[0x22634, 0x4E, 0x48], [0x3C29E, 0x4D, 0x4F], [0x3C3F8, 0x4D, 0x4F]], off: [0x4A, 0x48, 0x4F, 0x64], ent: [0, 1, 2, 0x81] },
    // food per field without the DNA part (option bit 64): settings block, field byte, switched slots, history,
    // first food kind per field (Land, Water, Forest, -, Sky in the Water slot, -), check and young stage branch
    food: { ram: 0x18000BA0, field: 0x1B7, slots: 2, hist: 0x132, kinds: [0, 3, 9, 0, 6, 0], sites: [[0x1000E7F6, '0cf035f9', 'bl', 10], [0x10072750, '80b590b0', 'bw', 11]] } },
};
/* 1.7.3 test23 SIX_HUNT: the egg hunt gives 8 entries per field (picture, item, found bit; two groups of 4, one picked at
 * random). It was left on the slot's own row, so Sky in the Forest slot gave Jade Forest ingredients. The rows of Land,
 * Water and Sky as in their firmware (items and found bits), the pictures from this ROM's item table. Jade Forest: Sky in
 * the Water slot keeps the Water slot's found bits. Option bit 4 in the header. */
const SIX_HUNT = true;
const HUNT_ROWS = [
  [[5003, 0], [5004, 1], [5007, 4], [5008, 5], [5005, 2], [5006, 3], [5007, 4], [5008, 5]],          // Land
  [[5009, 6], [5010, 7], [5013, 10], [5014, 11], [5011, 8], [5012, 9], [5013, 10], [5014, 11]],     // Water
  [[5015, 12], [5016, 13], [5017, 14], [5018, 15], [5019, 16], [5020, 17], [5017, 14], [5018, 15]], // Sky
];
const HUNT_SITES = { gen3: [[0x4336E, 1, 0x100A0600, 'hunt'], [0x433AC, 0, 0x100A0600, 'hunt']],
  jade: [[0x3DEE2, 1, 0x1008C1BC, 'hunt'], [0x3DF20, 0, 0x1008C1BC, 'hunt']] };
/* 1.7.3 test24 SIX_COOK: the kitchen's recipe table (12 pairs dish, ingredient; 4 per field, all three fields of the
 * firmware) also follows the set byte: Land, Water and Sky recipes as in their firmware (Jade Forest: Sky in place of
 * Water). Option bit 5 in the header. */
const SIX_COOK = true;
const COOK_ROWS = [[5021, 5004, 5022, 5005, 5023, 5006, 5024, 5008], [5025, 5009, 5026, 5010, 5027, 5012, 5028, 5014], [5029, 5016, 5030, 5018, 5031, 5019, 5032, 5020]];
const COOK_SITES = { gen3: [0x30A72, 0x4B930, 0x4B9A0, 0x4BAB4, 0x4BB7A, 0x6F202].map((o) => [o, 0, 0x10091FBC, 'cook']),
  jade: [0x2C426, 0x45B94, 0x45C04, 0x45D18, 0x45DDE, 0x635CA].map((o) => [o, 0, 0x1007EB5C, 'cook']) };
const T_COOK = 0x740;   // 24 halfwords up to the egg hunt table
/* the add-on tables of six fields: name, sites, place in the helper page, option bit, table for this ROM */
const EXTRAS = [
  { name: 'hunt', on: () => SIX_HUNT, sites: HUNT_SITES, at: 0x770, bit: 16,
    tab: (rom, kind) => kind === 'jade' ? rows(rom, HUNT_SITES.jade[0][2], 24, 3, { 1: huntRow(rom, 2, 6) }) : [0, 1, 2].flatMap((r) => huntRow(rom, r)) },
  { name: 'cook', on: () => SIX_COOK, sites: COOK_SITES, at: T_COOK, bit: 32,
    tab: (rom, kind) => kind === 'jade' ? rows(rom, COOK_SITES.jade[0][2], 8, 3, { 1: COOK_ROWS[2] }) : [].concat(...COOK_ROWS) },
];
/** SIX_HUNT, SIX_COOK for a ROM that has six fields already (no Land ROM needed): table, site entries, jumps */
function sixHunt(rom) {
  let out = rom;
  for (const X of EXTRAS) out = extraAdd(out, X);
  return out;
}
function extraAdd(rom, X) {
  const info = sixInfo(rom);
  if (!X.on() || !info || info.version < 7 || (u32(rom, SIX_OFF + 32) & X.bit) || !extraCan(rom, info.kind, X)) return rom;
  const K = KINDS[info.kind], f = (o) => 0x11000 + o, out = new Uint8Array(rom), P = SIX_OFF, S = X.sites[info.kind], tab = X.tab(rom, info.kind);
  for (let i = P + X.at; i < P + X.at + 2 * tab.length; i++) if (out[i] !== 0xFF && out[i] !== 0) return rom;   // free (the program leaves its tables empty)
  let n = 0; while (u32(out, P + T_SITES + 12 * n) !== 0) { n++; if (12 * (n + S.length + 1) > T_MORE - T_SITES) return rom; }
  tab.forEach((x, i) => w16(out, P + X.at + 2 * i, x));
  const fn = (k) => u32(out, P + P_FN + 4 * k) & ~1;
  S.forEach(([o, r, t], i) => {
    const e = P + T_SITES + 12 * (n + i); w32(out, e, (XIP(f(o)) + 4) | 1); w32(out, e + 4, t); w32(out, e + 8, SIX_XIP + X.at);
    const b = new Uint8Array(8); b.set(branch(XIP(f(o)), fn(FN_TAB[r]), true)); w16(b, 4, 0xBF00); w16(b, 6, 0xBF00); out.set(b, f(o));
  });
  w32(out, P + T_SITES + 12 * (n + S.length), 0);
  w32(out, P + 32, u32(out, P + 32) | X.bit);
  return out;
}
function extraCan(rom, kind, X) { const f = (o) => 0x11000 + o; return X.sites[kind].every(([o, r, t]) => hex(rom, f(o), 8) === hex(movPairBytes(r, t), 0, 8)); }
function huntCan(rom, kind) { return extraCan(rom, kind, EXTRAS[0]); }
function huntRow(rom, r, bitBase) {
  const Fd = root.ParamixFeld || (typeof require !== 'undefined' ? require('./feld.js') : null);
  const cat = Fd ? Fd.itemCatalog(rom) : [], pic = new Map(cat.map((x) => [x.id, x.pic]));
  const out = [];
  for (const [id, bit] of HUNT_ROWS[r]) { if (!pic.has(id)) throw new Error('item ' + id + ' missing'); out.push(pic.get(id), id, bitBase === undefined ? bit : bitBase + (bit % 6)); }
  return out;
}
// v1 and v2 (1.2.0, 1.2.1) of Tropical/Glacier jumped from these places too
const OLD_PATCH = [[0x20ED2, '41f21431c1f20901'], [0x20FBC, '41f22c31c1f20901']];
// Land firmware: background table, adult table and a few code places (to recognise it)
const LAND_BG = 0x1007BAA8, LAND_ADULT = 0x10079AA4, LAND_SPEED = 0x10086918, LAND_PLANET = 0x100877EC;
// planet stages of Sky (Land, Water, Sky ROMs: third row of LAND_PLANET)
const SKY_PLANET = [2, 5, 5, 5, 8, 8, 9, 9, 9, 9, 10];
const LAND_BG_AT = 0x11000 + 0x158F8, LAND_ADULT_AT = 0x11000 + 0xE2D0;
const LAND_FP = [[0x11000 + 0x38AA6, '46f61811c1f20801'], [0x11000 + 0x4D316, '01f65301']];
/*
 * Land, Water, Sky scripts per table column (Land numbers, + field): found through Jade Forest,
 * whose tables have the same layout as Tropical's and whose scripts match Land's one to one.
 * scene: 5 columns, wagon: 2 columns (the other two stay), egg: field eggs.
 */
const LAND_SCENE = [0x853, 1558, 1564, 1561, 1567], LAND_WAGON = [1464, 1467], LAND_EGG = 180;
// Jade Forest keeps the Sky pictures and scripts itself: Sky values in Jade's own numbers
const JADE_SKY = { bg: [773, 774, 775, 775, 776, 777, 777, 778, 779, 779, 780, 781, 781, 780, 781, 781],
  speed: [1371, 1372, 1373], egg: [183], scene: [2273, 1670, 1676, 1673, 1679], wagon: [1576, 1579, 1580, 1581],
  teen: [3013, 3014, 3015, 3016], child: [2002, 2004, 2069],
  adult: [4050, 4051, 4052, 4053, 4060, 4059, 4058, 4061, 4054, 4055, 4056, 4057, 4062, 4063, 4064, 4065] };
// Script ids of op 15 (probably sounds) that moved between the editions (Land -> Tropical)
const OP15 = { 24: 26, 25: 27, 26: 28, 27: 29, 28: 30, 29: 31, 30: 32, 31: 33, 32: 34, 33: 35, 34: 37, 35: 38, 36: 39, 37: 40, 38: 41, 39: 42, 40: 43, 41: 44, 42: 45, 43: 46, 44: 47, 45: 48 };

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w16(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; }
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
const hex = (b, o, n) => Array.from(b.subarray(o, o + n), (x) => x.toString(16).padStart(2, '0')).join('');
function b64(s) {
  if (typeof atob === 'function') { const t = atob(s); const o = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) o[i] = t.charCodeAt(i); return o; }
  return new Uint8Array(Buffer.from(s, 'base64'));
}
function concat(parts) { let n = 0; for (const p of parts) n += p.length; const o = new Uint8Array(n); let k = 0; for (const p of parts) { o.set(p, k); k += p.length; } return o; }

// ------------------------------------------------------------ Thumb-2
function movwDec(b, o) {
  const h1 = u16(b, o), h2 = u16(b, o + 2);
  if ((h1 & 0xFBF0) !== 0xF240 && (h1 & 0xFBF0) !== 0xF2C0) return -1;
  return ((h1 & 15) << 12) | (((h1 >> 10) & 1) << 11) | (((h2 >> 12) & 7) << 8) | (h2 & 255);
}
function movEnc(rd, imm, top) {
  const o = new Uint8Array(4);
  w16(o, 0, (top ? 0xF2C0 : 0xF240) | (((imm >> 11) & 1) << 10) | ((imm >> 12) & 15));
  w16(o, 2, (((imm >> 8) & 7) << 12) | (rd << 8) | (imm & 255));
  return o;
}
function movPair(b, o) { const lo = movwDec(b, o), hi = movwDec(b, o + 4); return lo < 0 || hi < 0 ? -1 : ((hi << 16) | lo) >>> 0; }
function branch(src, dst, link) {
  const off = dst - (src + 4);
  const S = (off >>> 24) & 1, I1 = (off >>> 23) & 1, I2 = (off >>> 22) & 1;
  const h1 = 0xF000 | (S << 10) | ((off >>> 12) & 0x3FF);
  const h2 = (link ? 0xD000 : 0x9000) | (((~I1 ^ S) & 1) << 13) | (((~I2 ^ S) & 1) << 11) | ((off >>> 1) & 0x7FF);
  const o = new Uint8Array(4); w16(o, 0, h1); w16(o, 2, h2); return o;
}

// ------------------------------------------------------------ data pack
// Header: u32 word offsets (in 16 bit units) of 22 tables, then the end.
function dpSplit(d) {
  const n = u32(d, 0) * 2 / 4 - 1, offs = [];
  for (let i = 0; i <= n; i++) offs.push(u32(d, 4 * i) * 2);
  const t = []; for (let i = 0; i < n; i++) t.push(d.slice(offs[i], offs[i + 1]));
  return t;
}
function dpJoin(tables) {
  const n = tables.length, hdr = 4 * (n + 1), h = new Uint8Array(hdr);
  let pos = hdr;
  tables.forEach((t, i) => { w32(h, 4 * i, pos / 2); pos += t.length; });
  w32(h, 4 * n, pos / 2);
  return concat([h, ...tables]);
}
const words = (t) => { const a = []; for (let i = 0; i + 1 < t.length; i += 2) a.push(u16(t, i)); return a; };
const toBytes = (a) => { const b = new Uint8Array(2 * a.length); a.forEach((v, i) => w16(b, 2 * i, v)); return b; };
// composites: table 4 = start of each (16 bit units into table 5)
function comp(tabs, i) { const ix = words(tabs[4]), d = words(tabs[5]); return d.slice(ix[i], i + 1 < ix.length ? ix[i + 1] : d.length); }

// ------------------------------------------------------------ maps
function rgb(v) { return [((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31]; }
function to565(c) { return (Math.round(c[0] * 31 / 255) << 11) | (Math.round(c[1] * 63 / 255) << 5) | Math.round(c[2] * 31 / 255); }
function mapCells(e) {
  const h = C.spriteHeader(e);
  if (h.bpp !== 3 || !(h.flags & 0x20) || h.numSprites !== 64) throw new Error('unexpected map picture');
  const cells = [];
  for (let s = 0; s < h.numSprites; s++) {
    let off = u32(e, h.pixOffset + 8 * s); const ln = u32(e, h.pixOffset + 8 * s + 4); const unc = off & 0x80000000; off &= 0x7fffffff;
    let d = e.slice(h.pixOffset + off, h.pixOffset + off + ln);
    if (h.flags & 0x80) d = d.map((x) => x ^ 0x53);
    if (!unc) d = C.rleDecode(d, false);
    cells.push(d);
  }
  const pal = []; for (let k = 0; k < 256; k++) pal.push(rgb(u16(e, h.palOffset + 2 * k)));
  return { h, cells, pal };
}
/* Reduce the colours of a group of 8 bit maps that share their pixel indices (seasons
 * of one field): k-means over the indices, each picture keeps its own colours per cluster. */
function reduce(group, K) {
  const cnt = new Float64Array(256);
  for (const s of group) for (const c of s.cells) for (const v of c) cnt[v]++;
  const used = []; for (let k = 0; k < 256; k++) if (cnt[k]) used.push(k);
  const X = used.map((k) => group.flatMap((s) => s.pal[k])), w = used.map((k) => cnt[k]), D = X[0].length;
  const d2 = (a, b) => { let s = 0; for (let i = 0; i < D; i++) s += (a[i] - b[i]) ** 2; return s; };
  let cent;
  if (used.length <= K) cent = X.map((x) => x.slice());
  else {
    cent = [X[w.indexOf(Math.max(...w))].slice()];
    while (cent.length < K) {
      let best = -1, bi = 0;
      for (let i = 0; i < X.length; i++) { let d = 1e18; for (const c of cent) { const e = d2(X[i], c); if (e < d) d = e; } d *= Math.sqrt(w[i]); if (d > best) { best = d; bi = i; } }
      cent.push(X[bi].slice());
    }
  }
  const map = new Array(X.length).fill(0);
  for (let it = 0; it < 20; it++) {
    for (let i = 0; i < X.length; i++) { let d = 1e18, b = 0; cent.forEach((c, k) => { const e = d2(X[i], c); if (e < d) { d = e; b = k; } }); map[i] = b; }
    const s = cent.map(() => ({ v: new Array(D).fill(0), w: 0 }));
    for (let i = 0; i < X.length; i++) { const t = s[map[i]]; for (let j = 0; j < D; j++) t.v[j] += X[i][j] * w[i]; t.w += w[i]; }
    cent = s.map((t, k) => (t.w ? t.v.map((x) => x / t.w) : cent[k]));
  }
  const remap = new Uint8Array(256); used.forEach((k, i) => { remap[k] = map[i]; });
  return { remap, pals: group.map((_, g) => cent.map((c) => to565(c.slice(3 * g, 3 * g + 3)))) };
}
const HEAD = 24 + 512 + 8 * 64;          // header, palette, cell table of one 8x8 map
/* Pack entries for a group: all headers first, then the cell data, shared between the
 * pictures where it is the same (offsets always point forward). */
function packGroup(group, K, ext) {
  const { remap, pals } = reduce(group, K);
  ext = ext || [];
  const enc = group.map((s) => s.cells.map((c) => C.rleEncode(c.map((v) => remap[v])).map((x) => x ^ 0x53)));
  // in the pack: every cell a picture inside the pack uses; outside (region B): cells only rain pictures use
  const place = (into, list, r) => { const key = r.join(','); if (into.seen.has(key)) return into.seen.get(key);
    const pad = (4 - into.pos % 4) % 4; if (pad) { list.push(new Uint8Array(pad)); into.pos += pad; }
    const t = [into.pos, r.length]; into.seen.set(key, t); list.push(r); into.pos += r.length; return t; };
  const A = { seen: new Map(), pos: 0 }, Bx = { seen: new Map(), pos: 0 }, dA = [], dB = [];
  group.forEach((_, j) => { if (!ext[j]) enc[j].forEach((r) => place(A, dA, r)); });
  const tables = group.map((_, j) => enc[j].map((r) => {
    const key = r.join(',');
    if (A.seen.has(key)) return { a: A.seen.get(key) };
    return { b: place(Bx, dB, r) };
  }));
  const n = group.length, blob = concat(dA), out = [], outside = [];
  group.forEach((s, j) => {
    const e = new Uint8Array(HEAD + (j === n - 1 ? blob.length : 0));
    e.set(s.h.raw.subarray(0, 24));
    w32(e, 0, HEAD * (n - j) + blob.length); e[4] = 0xA3; e[5] = 3; e[15] = 1; w16(e, 18, 24); w16(e, 20, 536);
    pals[j].forEach((v, k) => w16(e, 24 + 2 * k, v));
    const rel = [];
    tables[j].forEach((t, k) => {
      if (t.a) { w32(e, 536 + 8 * k, HEAD * (n - j) - 536 + t.a[0]); w32(e, 536 + 8 * k + 4, t.a[1]); rel.push(-1); }
      else { w32(e, 536 + 8 * k, 0); w32(e, 536 + 8 * k + 4, t.b[1]); rel.push(t.b[0]); }
    });
    if (j === n - 1) e.set(blob, HEAD);
    out.push(e); outside.push(rel.some((x) => x >= 0) ? rel : null);
  });
  return Object.assign(out, { outside, bdata: concat(dB) });
}
/* One map on its own, colours reduced (for the Tropical maps). */
function reduceOne(e, K) {
  const m = mapCells(e); m.h.raw = e;
  return packGroup([m], K)[0];
}

// ------------------------------------------------------------ scripts (animations)
// Table 13 = start of each script (16 bit, in 4 byte units) in table 14. A script: one word,
// then commands: low byte = command, high byte = which parameters follow (one bit each).
// Command 16 starts another script (first parameter), command 1 shows a sprite, command 15
// refers to a numbered entry that moved between the editions (OP15).
function scripts(tabs) {
  const ix = words(tabs[13]), w = words(tabs[14]);
  return { ix, w, get: (a) => w.slice(2 * ix[a], a + 1 < ix.length ? 2 * ix[a + 1] : w.length) };
}
function parseScript(s) {
  const ops = []; let i = 1;
  while (i < s.length) {
    const op = s[i];
    if (op === 0xFCBD && i === s.length - 1) { i++; break; }
    let n = 0; for (let m = op >> 8; m; m >>= 1) n += m & 1;
    ops.push({ op: op & 255, mask: op >> 8, p: s.slice(i + 1, i + 1 + n) }); i += 1 + n;
  }
  return ops;
}
/** Copies scripts (with the scripts and sprites they use) from one archive into another. */
function importer(src, dst, entries) {
  const S = scripts(src.tabs), D = scripts(dst.tabs);
  const anims = new Map(), sprites = new Map(), have = new Map(), made = [];
  dst.entries.forEach((e, i) => { const k = e.length + ':' + C.crc32(e); if (!have.has(k)) have.set(k, i); });
  const sprite = (i) => {
    if (sprites.has(i)) return sprites.get(i);
    const e = src.entries[i], k = e.length + ':' + C.crc32(e);
    let id = have.get(k);
    if (id === undefined) { id = entries.length; entries.push(e); have.set(k, id); }
    sprites.set(i, id); return id;
  };
  const anim = (a) => {
    if (anims.has(a)) return anims.get(a);
    const id = D.ix.length + made.length; anims.set(a, id); made.push(null);
    const s = S.get(a), out = [s[0]];
    for (const o of parseScript(s)) {
      const p = o.p.slice();
      if (o.mask & 0x80 && p.length) {
        if (o.op === 16) p[0] = anim(p[0]);
        else if (o.op === 1) p[0] = sprite(p[0]);
        else if (o.op === 15 && OP15[p[0]]) p[0] = OP15[p[0]];
      }
      out.push(o.op | (o.mask << 8), ...p);
    }
    if (out.length & 1) out.push(0xFCBD);
    made[id - D.ix.length] = out;
    return id;
  };
  const done = () => {
    const ix = D.ix.slice(), w = D.w.slice();
    for (const out of made) { if (w.length / 2 > 0xFFFF) throw new Error('script table full'); ix.push(w.length / 2); w.push(...out); }
    dst.tabs[13] = toBytes(ix); dst.tabs[14] = toBytes(w);
  };
  return { anim, sprite, done };
}

// ------------------------------------------------------------ lab games per start field (1.7.2, version 7)
/*
 * Land, Water and Sky have other lab games than Tropical and Glacier (Matching Meals, Catching Poop and
 * Cleaning Dust in place of Fish Matching, Fruit Gathering and Curling); Jade Forest has Matching Meals and
 * Catching Poop next to Cloudy Coin. The game code is the same in all firmwares, only the numbers of the
 * pictures differ. With the family byte (0xF05, written by the Paramix menu when it starts a field) the
 * firmware places below take the Land numbers (translated into this ROM, missing pictures are copied from the
 * user's Land, Water or Sky ROM). Sites: XIP address, kind (w: movw/mov.w, a: addw/add.w with rn), rd, rn,
 * original value, resource (c composite, s script, p picture), Land number, families (1 Land, 2 Jade, 3 both).
 * Tables (movw/movt pairs): XIP address of the pair, register, table name, families, flags (1: in every scene,
 * for the PLAY list of the care menu, which runs in the home scene).
 */
const SIX_GAMES = true;
const G_SCENES = [44, 45, 46, 47, 48, 49, 50, 51, 92, 95, 96, 97];   // lab card list (44, 45..51 back from game k), result, games
const G_SEQ = [[0, 1, 2, 3, 6, 5], [0, 1, 2, 3, 4, 5]];
const G_NAV = [[0x1002FFE4, 1], [0x1003003A, 2]];       // ldrh.w r1,[r2,#126] -> six_cnext / six_cprev
const G_NAV_ORIG = 'b2f87e10';
const G_SITES = [
  ['0x10050b1e','w','r0',"",1225,'c',997,3],
  ['0x10050b46','w','r1',"",2626,'s',1967,3],
  ['0x10070760','w','r2',"",1227,'c',999,3],
  ['0x10070778','w','r0',"",1228,'c',1001,3],
  ['0x1007079a','w','r0',"",1232,'c',1005,3],
  ['0x100707ba','w','r0',"",1236,'c',1009,3],
  ['0x10071054','w','r0',"",1253,'c',1026,3],
  ['0x1007107a','w','r1',"",2645,'s',2001,3],
  ['0x10071092','w','r0',"",1250,'c',1023,3],
  ['0x100710b8','w','r1',"",2636,'s',1985,3],
  ['0x100710d0','w','r0',"",1251,'c',1024,3],
  ['0x100710f6','w','r1',"",2643,'s',1993,3],
  ['0x10071110','w','r0',"",1252,'c',1025,3],
  ['0x10071136','w','r1',"",2644,'s',1997,3],
  ['0x100711fc','w','r2',"",1227,'c',999,3],
  ['0x1007122a','w','r0',"",1228,'c',1001,3],
  ['0x1007124c','w','r0',"",1232,'c',1005,3],
  ['0x1007126c','w','r0',"",1236,'c',1009,3],
  ['0x100713aa','a','r2','r2',1231,'c',1004,3],
  ['0x100713e8','a','r2','r2',1231,'c',1004,3],
  ['0x10071418','a','r1','r1',2167,'p',1387,3],
  ['0x10069ef4','a','r1','r1',1231,'c',1004,3],
  ['0x10069f32','a','r1','r1',1231,'c',1004,3],
  ['0x10069f68','a','r1','r1',2167,'p',1387,3],
  ['0x10051e6e','w','r0',"",1285,'c',1060,3],
  ['0x10051e94','w','r1',"",2662,'s',2025,3],
  ['0x10051eac','w','r0',"",1286,'c',1061,3],
  ['0x10051ed2','w','r1',"",2667,'s',2027,3],
  ['0x10051eea','w','r0',"",1287,'c',1062,3],
  ['0x10051f10','w','r1',"",2669,'s',2030,3],
  ['0x10051f2a','w','r0',"",1288,'c',1063,3],
  ['0x10051f50','w','r1',"",2671,'s',2033,3],
  ['0x100843a8','w','r2',"",1256,'c',1029,3],
  ['0x100843c4','w','r2',"",1257,'c',1030,3],
  ['0x10084368','w','r2',"",1262,'c',1035,3],
  ['0x10084304','w','r2',"",1271,'c',1044,3],
  ['0x1007d08a','w','r2',"",1263,'c',1036,3],
  ['0x1007d152','w','r2',"",1262,'c',1035,3],
  ['0x1008f4f4','w','r0',"",1275,'c',1048,3],
  ['0x1006aa1a','w','r0',"",1271,'c',1044,3],
  ['0x1006aa34','a','r0','r0',1271,'c',1044,3],
  ['0x100842ca','w','r2',"",1279,'c',1054,3],
  ['0x1008f768','w','r0',"",1265,'c',1038,3],
  ['0x10021758','w','r2',"",1291,'c',1066,1],
  ['0x1002176e','w','r2',"",1298,'c',1072,1],
  ['0x1005219e','w','r1',"",2675,'s',2034,1],
  ['0x1005236a','w','r0',"",1294,'c',1069,1],
  ['0x100528f8','w','r0',"",1294,'c',1069,1],
  ['0x1006fc84','a','r1','r1',1301,'c',1075,1],
  ['0x1005fd98','w','r0',"",1296,'c',1071,1],
  ['0x1005fda4','w','r0',"",1298,'c',1072,1],
  ['0x100788e8','w','r0',"",1296,'c',1071,1],
  ['0x100788f4','w','r0',"",1298,'c',1072,1],
  ['0x10051696','w','r0',"",1254,'c',1027,3],
  ['0x1005184c','w','r0',"",1284,'c',1059,3],
];
const G_TABS = [
  ['0x100410a4','r3','play',1,1],
  ['0x10041200','r0','play',1,1],
  ['0x1005168a','r0','intro03',3],
  ['0x10051840','r0','intro03',3],
  ['0x10083938','r1','avatar',3],
  ['0x1007d0a6','r0','avatar2',3],
  ['0x1007d16e','r0','avatar2',3],
  ['0x1008f782','r0','avatar2',3],
  ['0x100301de','r1','cardPic',3],
  ['0x100303d8','r1','cardPic',3],
  ['0x100304aa','r1','cardPic',3],
  ['0x100306a6','r1','cardPic',3],
  ['0x100478c4','r1','cardPic',3],
  ['0x10047b8c','r1','cardPic',3],
  ['0x100301fc','r0','cardName',3],
  ['0x100303f6','r0','cardName',3],
  ['0x100304c8','r0','cardName',3],
  ['0x100306c4','r0','cardName',3],
  ['0x100478e2','r0','cardName',3],
  ['0x10047baa','r0','cardName',3],
  ['0x1008f89c','r0','bg03',3],
  ['0x1008f8d0','r0','bg03',3],
  ['0x1008f906','r0','bg03',3],
  ['0x1008f934','r0','bg03',3],
  ['0x1008f96a','r0','bg03',3],
  ['0x1008f9ee','r0','bg03',3],
  ['0x1008fa24','r0','bg03',3],
  ['0x1008fa52','r0','bg03',3],
  ['0x1008fa88','r0','bg03',3],
  ['0x1008fafa','r0','bg03',3],
  ['0x100300b0','r0','cardLevel',3],
  ['0x10047e1e','r0','cardLevel',3],
];
const G_TABLES = {
  avatar: { addr: 0x100A008A, n: 23, w: 2, f1: [[0,'s',1984],[1,'c',1022],[2,'s',1981],[3,'c',1019],[4,'s',1982],[5,'c',1020],[6,'s',1983],[7,'c',1021],[20,'c',1038],[21,'c',1039],[22,'c',1040]], f2: [[0,'s',1984],[1,'c',1022],[2,'s',1981],[3,'c',1019],[4,'s',1982],[5,'c',1020],[6,'s',1983],[7,'c',1021],[20,'c',1038],[21,'c',1039],[22,'c',1040]] },
  avatar2: { addr: 0x100A00B2, n: 3, w: 2, f1: [[0,'c',1038],[1,'c',1039],[2,'c',1040]], f2: [[0,'c',1038],[1,'c',1039],[2,'c',1040]] },
  cardPic: { addr: 0x10093544, n: 7, w: 4, f1: [[0,'p',1335],[1,'p',1336],[2,'p',1337],[3,'p',1338],[6,'p',1339],[5,'p',1340]], f2: [[0,'p',1335],[1,'p',1336],[2,'p',1337],[3,'p',1338],[5,'p',1340]] },
  cardName: { addr: 0x10093560, n: 8, w: 2, f1: [[0,'=',803],[1,'=',804],[2,'=',805],[3,'=',806],[6,'=',807],[5,'=',808]], f2: [[0,'=',803],[1,'=',804],[2,'=',805],[3,'=',806],[5,'=',808]] },
  cardLevel: { addr: 0x10093976, n: 7, w: 2, f1: [[0,'=',1],[1,'=',4],[2,'=',4],[3,'=',4],[6,'=',1],[5,'=',1]], f2: [[0,'=',1],[1,'=',4],[2,'=',4],[3,'=',4],[5,'=',1]] },
  intro03: { addr: 0x10094368, n: 2, w: 2, f1: [[0,'s',2005],[1,'s',2015]], f2: [[0,'s',2005],[1,'s',2015]] },
  bg03: { addr: 0x10091CC4, n: 5, w: 2, f1: [[0,'p',1402],[1,'p',1402],[2,'p',1402],[3,'p',1402],[4,'p',1402]], f2: [[0,'p',1402],[1,'p',1402],[2,'p',1402],[3,'p',1402],[4,'p',1402]] },
  play: { addr: 0x10094140, n: 24, w: 2, f1: [[0,'=',809],[2,'=',101],[12,'=',811],[14,'=',103],[18,'=',812],[20,'=',104]] },
};
const P_GM = 0x980, P_GM_FN = 0x9B0;

/* movw / mov.w / addw / add.w with an immediate: kind w or a, rd, rn, value */
function immDec(b, o) {
  const h1 = u16(b, o), h2 = u16(b, o + 2), i12 = (((h1 >> 10) & 1) << 11) | (((h2 >> 12) & 7) << 8) | (h2 & 255), rd = (h2 >> 8) & 15, rn = h1 & 15;
  if (h2 & 0x8000) return null;
  const expand = (v) => { const x = v & 255; if ((v >> 10) === 0) return [x, (x << 16) | x, ((x << 24) | (x << 8)) >>> 0, (x * 0x01010101) >>> 0][(v >> 8) & 3];
    const u = 0x80 | (v & 0x7F), r = v >> 7; return ((u >>> r) | (u << (32 - r))) >>> 0; };
  if ((h1 & 0xFBF0) === 0xF240) return { kind: 'w', rd, imm: ((h1 & 15) << 12) | i12 };
  if ((h1 & 0xFBFF) === 0xF04F) return { kind: 'w', rd, imm: expand(i12) };
  if ((h1 & 0xFBF0) === 0xF200) return { kind: 'a', rd, rn, imm: i12 };
  if ((h1 & 0xFBF0) === 0xF100) return { kind: 'a', rd, rn, imm: expand(i12) };
  return null;
}
/** Finds Land resources in this ROM (same content) or copies them over. */
function translator(L, tt, entries) {
  const lix = words(L.tabs[4]), lcw = words(L.tabs[5]), lsx = words(L.tabs[13]), lsw = words(L.tabs[14]);
  const tix = words(tt[4]), tcw = words(tt[5]), tsx = words(tt[13]), tsw = words(tt[14]);
  const n0 = { c: tix.length, s: tsx.length };
  const lComp = (i) => lcw.slice(lix[i], i + 1 < lix.length ? lix[i + 1] : lcw.length);
  const tComp = (i) => tcw.slice(tix[i], i + 1 < tix.length ? tix[i + 1] : tcw.length);
  const lScr = (a) => lsw.slice(2 * lsx[a], a + 1 < lsx.length ? 2 * lsx[a + 1] : lsw.length);
  const tScr = (a) => tsw.slice(2 * tsx[a], a + 1 < tsx.length ? 2 * tsx[a + 1] : tsw.length);
  const key = (e) => e.length + ':' + C.crc32(e);
  const tSpr = new Map(); entries.forEach((e, i) => { const k = key(e); if (!tSpr.has(k)) tSpr.set(k, i); });
  const tScrM = new Map(); for (let a = 0; a < n0.s; a++) { const k = tScr(a).join(','); if (!tScrM.has(k)) tScrM.set(k, a); }
  const tCompM = new Map(); for (let i = 0; i < n0.c; i++) { const k = tComp(i).join(','); if (!tCompM.has(k)) tCompM.set(k, i); }
  const shape = (s) => parseScript(s).map((o) => o.op + ':' + o.mask).join(',');
  const fSpr = new Map(), fScr = new Map(), fComp = new Map();
  const findSprite = (i) => { if (!fSpr.has(i)) { const j = i < L.entries.length ? tSpr.get(key(L.entries[i])) : undefined; fSpr.set(i, j === undefined ? null : j); } return fSpr.get(i); };
  const mapScript = (s, sub, spr) => {
    const out = [s[0]];
    for (const o of parseScript(s)) {
      const p = o.p.slice();
      if (o.mask & 0x80 && p.length) {
        if (o.op === 16) { const r = sub(p[0]); if (r === null) return null; p[0] = r; }
        else if (o.op === 1) { const r = spr(p[0]); if (r === null) return null; p[0] = r; }
      }
      out.push(o.op | (o.mask << 8), ...p);
    }
    if (out.length & 1) out.push(0xFCBD);
    return out;
  };
  const findScript = (a, d = 0) => {
    if (fScr.has(a)) return fScr.get(a);
    fScr.set(a, null);
    if (a >= lsx.length || d > 12) return null;
    const out = mapScript(lScr(a), (x) => findScript(x, d + 1), findSprite);
    const r = out && tScrM.has(out.join(',')) ? tScrM.get(out.join(',')) : null;
    fScr.set(a, r); return r;
  };
  const findComp = (i) => {
    if (fComp.has(i)) return fComp.get(i);
    let r = null;
    if (i < lix.length) {
      const w = lComp(i).slice(), t = w[0] >> 12; let ok = true;
      if (t === 1 && w.length > 3) { const x = findSprite(w[3]); if (x === null) ok = false; else w[3] = x; }
      else if (t === 5 && w.length > 3) { const x = findScript(w[3]); if (x === null) ok = false; else w[3] = x; }
      if (ok && tCompM.has(w.join(','))) r = tCompM.get(w.join(','));
    }
    fComp.set(i, r); return r;
  };
  // the same block of Land numbers sits elsewhere in this ROM: offset of the nearest neighbour found
  const near = (find, id) => { for (let d = 1; d < 40; d++) for (const k of [id - d, id + d]) { if (k < 0) continue; const r = find(k); if (r !== null) return r - k; } return null; };
  const looseScript = (a) => {
    const r = findScript(a); if (r !== null) return r;
    const off = near(findScript, a);
    return off !== null && a + off >= 0 && a + off < n0.s && a < lsx.length && shape(tScr(a + off)) === shape(lScr(a)) ? a + off : null;
  };
  const looseComp = (i) => {
    const r = findComp(i); if (r !== null) return r;
    const off = near(findComp, i); if (off === null || i + off >= n0.c || i >= lix.length) return null;
    const w = lComp(i), g = tComp(i + off);
    if (g.length !== w.length || g[0] !== w[0] || g[1] !== w[1] || g[2] !== w[2]) return null;
    if ((w[0] >> 12) === 5 && shape(tScr(g[3])) !== shape(lScr(w[3]))) return null;
    return i + off;
  };
  const gSpr = new Map(), gScr = new Map(), gComp = new Map();
  const sprite = (i) => {
    if (gSpr.has(i)) return gSpr.get(i);
    let j = findSprite(i); if (j === null) { j = entries.length; entries.push(L.entries[i]); tSpr.set(key(L.entries[i]), j); }
    gSpr.set(i, j); return j;
  };
  const script = (a) => {
    if (gScr.has(a)) return gScr.get(a);
    let j = looseScript(a); if (j !== null) { gScr.set(a, j); return j; }
    j = tsx.length; gScr.set(a, j); tsx.push(tsw.length / 2); tsw.push(0, 0);
    const out = [lScr(a)[0]];
    for (const o of parseScript(lScr(a))) {
      const p = o.p.slice();
      if (o.mask & 0x80 && p.length) { if (o.op === 16) p[0] = script(p[0]); else if (o.op === 1) p[0] = sprite(p[0]); else if (o.op === 15 && OP15[p[0]]) p[0] = OP15[p[0]]; }
      out.push(o.op | (o.mask << 8), ...p);
    }
    if (out.length & 1) out.push(0xFCBD);
    // the placeholder was the last script so far: put the real one there (scripts after it are appended later)
    pend.push([j, out]);
    return j;
  };
  const pend = [];
  const comp = (i) => {
    if (gComp.has(i)) return gComp.get(i);
    let j = looseComp(i); if (j !== null) { gComp.set(i, j); return j; }
    const w = lComp(i).slice(), t = w[0] >> 12;
    if (t === 1 && w.length > 3) w[3] = sprite(w[3]); else if (t === 5 && w.length > 3) w[3] = script(w[3]);
    j = tix.length; tix.push(tcw.length); tcw.push(...w); if (tcw.length & 1) tcw.push(0);
    gComp.set(i, j); return j;
  };
  const res = (kind, v) => (kind === 'c' ? comp(v) : kind === 's' ? script(v) : sprite(v));
  const done = () => {
    // scripts: rebuild the added ones in order (their sizes were not known when the numbers were given)
    if (pend.length) {
      pend.sort((a, b) => a[0] - b[0]);
      const ix = tsx.slice(0, pend[0][0]), w = tsw.slice(0, 2 * tsx[pend[0][0]]);
      for (const [j, out] of pend) { if (ix.length !== j) throw new Error('script order'); ix.push(w.length / 2); w.push(...out); }
      if (w.length / 2 > 0xFFFF) throw new Error('script table full');
      tt[13] = toBytes(ix); tt[14] = toBytes(w);
    }
    if (tcw.length > 0xFFFF) throw new Error('composite table full');
    tt[4] = toBytes(tix); tt[5] = toBytes(tcw);
  };
  return { res, done, findSprite };
}
/* Site list and tables of the games (region B, after the rain cells). Returns the bytes; page gets the
   addresses. base: flash offset where the bytes go. */
function gamesBuild(rom, X, base) {
  const f = (x) => x - 0x10000000 + 0x11000, rd16 = (a) => u16(rom, f(a)), rd32 = (a) => u32(rom, f(a));
  const REG = (r) => (r === 'ip' ? 12 : +String(r).slice(1));
  const MAP = (off) => 0x60000000 + off;     // flash, as the firmware reads its archive
  const parts = [], tabAt = {}; let pos = 0;
  const put = (b) => { const at = pos; parts.push(b); pos += b.length; const pad = (4 - pos % 4) % 4; if (pad) { parts.push(new Uint8Array(pad)); pos += pad; } return at; };
  // tables: copy of the original with the family values
  for (const [name, T] of Object.entries(G_TABLES)) {
    for (let fam = 1; fam <= 2; fam++) {
      const sub = T['f' + fam]; if (!sub) continue;
      const b = new Uint8Array(T.n * T.w);
      for (let i = 0; i < T.n; i++) { const v = T.w === 4 ? rd32(T.addr + 4 * i) : rd16(T.addr + 2 * i); if (T.w === 4) w32(b, 4 * i, v); else w16(b, 2 * i, v); }
      for (const [i, kind, lv] of sub) { const v = kind === '=' ? lv : X.res(kind, lv); if (T.w === 4) w32(b, 4 * i, v); else w16(b, 2 * i, v); }
      tabAt[name + fam] = MAP(base + put(b));
    }
  }
  const ent = [];
  const site = (xip, rd, rn, vals, flags = 0) => { const b = new Uint8Array(20); w32(b, 0, (xip + 4) | 1); vals.forEach((v, k) => w32(b, 4 + 4 * k, v)); b[16] = rd; b[17] = rn; b[18] = flags; ent.push(b); };
  const patches = [];
  for (const [a, kind, rd, rn, gv, res, lv, fams] of G_SITES) {
    const xip = parseInt(a, 16), d = immDec(rom, f(xip));
    // the place must still hold the original instruction with the original value
    if (!d || d.kind !== kind || d.imm !== gv || d.rd !== REG(rd) || (kind === 'a' && d.rn !== REG(rn))) throw new Error('game place changed ' + a);
    const nv = X.res(res, lv);
    site(xip, REG(rd), kind === 'a' ? REG(rn) : 0xFF, [gv, fams & 1 ? nv : gv, fams & 2 ? nv : gv]);
    patches.push([xip, 4]);
  }
  for (const [a, rd, name, fams, flags = 0] of G_TABS) {
    const xip = parseInt(a, 16), t = movPair(rom, f(xip));
    if (t !== G_TABLES[name].addr) throw new Error('game table place changed ' + a);
    site(xip, REG(rd), 0xFF, [t, fams & 1 && tabAt[name + 1] ? tabAt[name + 1] : t, fams & 2 && tabAt[name + 2] ? tabAt[name + 2] : t], flags);
    patches.push([xip, 8]);
  }
  ent.push(new Uint8Array(4));
  const listAt = put(concat(ent));
  // original bytes of every place (for taking the games out again): count, then XIP address, length, 8 bytes
  const rest = new Uint8Array(4 + 16 * (patches.length + G_NAV.length)); let n = 0;
  for (const [xip, len] of patches.concat(G_NAV.map(([x]) => [x, 4]))) { const o = 4 + 16 * n++; w32(rest, o, xip); w32(rest, o + 4, len); rest.set(rom.subarray(f(xip), f(xip) + len), o + 8); }
  w32(rest, 0, n);
  const restAt = put(rest);
  return { bytes: concat(parts), patches, list: MAP(base + listAt), rest: base + restAt };
}

// ------------------------------------------------------------ install
function kindOf(rom) {
  if (rom.length !== 0x1000000) return null;
  const a = u32(rom, 0xD29000), b = u32(rom, 0xD49000);
  if (a === 5 || a === 6) return 'gen3';
  if (b === 4) return 'jade';
  return null;
}
function sixInfo(rom) {
  if (rom.length < SIX_OFF + 0x400 || u32(rom, SIX_OFF) !== SIX_MAGIC) return null;
  const version = u32(rom, SIX_OFF + 4);
  return { version, kind: version >= 3 && u32(rom, SIX_OFF + 28) === 1 ? 'jade' : 'gen3', flagOff: u32(rom, SIX_OFF + 12),
    sprites: u32(rom, SIX_OFF + 16), comps: u32(rom, SIX_OFF + 20), anims: version >= 2 ? u32(rom, SIX_OFF + 24) : 0,
    planet: version >= 5 && (u32(rom, SIX_OFF + 32) & 1) === 1, fieldMenu: version >= 6 && (u32(rom, SIX_OFF + 32) & 2) === 2,
    games: version >= 7 && (u32(rom, SIX_OFF + 32) & 4) === 4, dna: version >= 7 && (u32(rom, SIX_OFF + 32) & 8) === 8, hunt: version >= 7 && (u32(rom, SIX_OFF + 32) & 16) === 16, food: version >= 7 && (u32(rom, SIX_OFF + 32) & 64) === 64, gamesRest: version >= 7 ? u32(rom, SIX_OFF + 36) : 0 };
}
const movPairBytes = (r, t) => concat([movEnc(r, t & 0xFFFF), movEnc(r, t >>> 16, true)]);
/** Can this ROM get more fields? (Tropical/Glacier: Land, Water, Sky; Jade Forest: Sky) */
function sixCan(rom) {
  const k = kindOf(rom); if (!k) return false;
  if (sixInfo(rom)) return true;
  const K = KINDS[k], f = (o) => 0x11000 + o;
  return hex(rom, f(K.bg.at), 4) === K.bg.orig && hex(rom, f(K.child.at), 6) === K.child.orig &&
    K.sites.concat(K.planet.sites).every(([o, r, t]) => hex(rom, f(o), 8) === hex(movPairBytes(r, t), 0, 8)) &&
    K.planet.calls.every((o) => hex(rom, f(o), 4) === hex(branch(XIP(f(o)), K.planet.fn, true), 0, 4));
}
/** Is this a Land, Water or Sky ROM that can give its fields? */
function sixSource(land) {
  if (!land || land.length !== 0x1000000) return false;
  const id = u32(land, 0xD49000);
  if (id < 1 || id > 3) return false;
  return movPair(land, LAND_BG_AT) === LAND_BG && movPair(land, LAND_ADULT_AT) === LAND_ADULT && LAND_FP.every(([o, h]) => hex(land, o, h.length / 2) === h);
}
/** Does this ROM need a Land, Water or Sky ROM for its extra fields? (Jade Forest does not) */
function sixNeedsSource(rom) { return kindOf(rom) === 'gen3'; }
/** Takes the extra fields out again (firmware places, helper page, added pictures and scripts). */
function sixStrip(rom) {
  const info = sixInfo(rom);
  if (!info) return rom;
  const out = new Uint8Array(rom), K = KINDS[info.kind], f = (o) => 0x11000 + o;
  const put = (o, h) => { for (let k = 0; k < h.length / 2; k++) out[o + k] = parseInt(h.substr(2 * k, 2), 16); };
  if (info.dna) dnaStrip(out, K, info);
  if (info.food && K.food) foodStrip(out, K);
  put(f(K.bg.at), K.bg.orig); put(f(K.child.at), K.child.orig);
  if (info.kind === 'gen3') for (const [o, h] of OLD_PATCH) put(o, h);
  for (const [o, r, t] of K.sites.concat(K.planet.sites, ...EXTRAS.map((X) => X.sites[info.kind]))) out.set(movPairBytes(r, t), f(o));
  for (const o of K.planet.calls) out.set(branch(XIP(f(o)), K.planet.fn, true), f(o));
  if (info.fieldMenu) fmStrip(out, K);
  if (info.games && info.gamesRest >= B_OFF && info.gamesRest < B_END) {
    const r = info.gamesRest, n = u32(out, r);
    for (let k = 0; k < n && k < 4096; k++) { const o = r + 4 + 16 * k, xip = u32(out, o), len = u32(out, o + 4); out.set(out.slice(o + 8, o + 8 + len), xip - 0x10000000 + 0x11000); }
  }
  out.fill(0xFF, SIX_OFF, SIX_OFF + 0x1000);
  if (u32(out, B_OFF) === B_MAGIC) out.fill(0xFF, B_OFF, 0x1000000);
  if (info.kind === 'jade') return out;
  const T = C.openRom(out), tabs = dpSplit(T.files[0]);
  const ix = words(tabs[4]), cw = words(tabs[5]);
  tabs[4] = toBytes(ix.slice(0, info.comps)); tabs[5] = toBytes(cw.slice(0, ix[info.comps]));
  if (info.version >= 2) { const S = scripts(tabs); tabs[13] = toBytes(S.ix.slice(0, info.anims)); tabs[14] = toBytes(S.w.slice(0, 2 * S.ix[info.anims])); }
  return C.buildRom({ rom: out, entries: T.entries.slice(0, info.sprites), files: [null, null, T.files[2]], limit: T.limit, assetEnd: T.assetEnd }, new Map(), dpJoin(tabs)).rom;
}
/* The game's field menu: are the places as the original firmware has them? */
function fmCan(rom, K) {
  const F = K.fm, f = (o) => 0x11000 + o;
  if (!F) return false;
  if (hex(rom, f(F.build[0]), 4) !== hex(branch(0x10000000 + F.build[0], F.build[1], true), 0, 4)) return false;
  if (!F.prev.slice(1).every((o) => hex(rom, f(o), 4) === hex(branch(0x10000000 + o, F.prev[0], true), 0, 4))) return false;
  if (!F.cmp.concat([F.set]).every((o) => hex(rom, f(o), 4) === F.read)) return false;
  return F.imm.every(([o, old]) => rom[f(o) + 2] === old);
}
function fmStrip(out, K) {
  const F = K.fm, f = (o) => 0x11000 + o, rd = (o) => { for (let k = 0; k < 4; k++) out[f(o) + k] = parseInt(F.read.substr(2 * k, 2), 16); };
  out.set(branch(0x10000000 + F.build[0], F.build[1], true), f(F.build[0]));
  for (const o of F.prev.slice(1)) out.set(branch(0x10000000 + o, F.prev[0], true), f(o));
  F.cmp.concat([F.set]).forEach(rd);
  for (const [o, old] of F.imm) out[f(o) + 2] = old;
}
function fmInstall(out, page, K) {
  const F = K.fm, f = (o) => 0x11000 + o, fn = (k) => u32(page, P_FM_FN + 4 * k) & ~1;
  w32(page, P_FM, F.obj); w32(page, P_FM + 4, F.field); w32(page, P_FM + 8, F.level);
  w32(page, P_FM + 12, F.build[1] | 1); w32(page, P_FM + 16, F.prev[0] | 1);
  F.off.forEach((v, i) => { page[P_FM + 0x14 + i] = v; });
  page[P_FM + 0x18] = F.ent.length; page.fill(0, P_FM + 0x19, P_FM + 0x1C);
  page.fill(0, P_FM + 0x1C, P_FM + 0x24); F.ent.forEach((v, i) => { page[P_FM + 0x1C + i] = v; });
  out.set(branch(0x10000000 + F.build[0], fn(0), true), f(F.build[0]));
  for (const o of F.cmp) out.set(branch(0x10000000 + o, fn(1), true), f(o));
  out.set(branch(0x10000000 + F.set, fn(2), true), f(F.set));
  for (const o of F.prev.slice(1)) out.set(branch(0x10000000 + o, fn(3), true), f(o));
  for (const [o, , nu] of F.imm) out[f(o) + 2] = nu;
}
/* Helper page and firmware jumps. alt: alternative tables by name (rows like the original). */
function install(out, kind, blob, alt, counts, planet, fieldMenu, games) {
  const K = KINDS[kind], f = (o) => 0x11000 + o, sites = (planet ? K.sites.concat(K.planet.sites) : K.sites).concat(...EXTRAS.filter((X) => alt[X.name]).map((X) => X.sites[kind]));
  const page = new Uint8Array(0x1000).fill(0xFF); page.set(blob.subarray(0, Math.min(blob.length, P_DNA)));
  w32(page, 8, K.flag); w32(page, 12, FLAG_OFF);
  w32(page, 16, counts[0]); w32(page, 20, counts[1]); w32(page, 24, counts[2]); w32(page, 28, K.id); w32(page, 32, (planet ? 1 : 0) | (fieldMenu ? 2 : 0) | (games ? 4 : 0) | EXTRAS.reduce((m, X) => m | (alt[X.name] ? X.bit : 0), 0)); w32(page, 36, games ? games.rest : 0);
  // background table and child list
  alt.bg.forEach((v, i) => w16(page, T_BG + 2 * i, v));
  w32(page, P_BG, K.bg.table); w32(page, P_BG + 4, SIX_XIP + T_BG); w32(page, P_BG + 8, K.bg.mode);
  K.child.list.forEach((v, i) => w16(page, T_CHILD_O + 2 * i, v)); alt.child.forEach((v, i) => w16(page, T_CHILD + 2 * i, v));
  w32(page, P_CHILD, SIX_XIP + T_CHILD_O); w32(page, P_CHILD + 4, SIX_XIP + T_CHILD); w32(page, P_CHILD + 8, K.child.sp);
  // other tables, then the site list
  let pos = T_MORE; const at = {};
  for (const name of new Set(sites.map((x) => x[3]))) {
    const v = alt[name]; if (v === undefined) throw new Error('table ' + name + ' missing');
    { const X = EXTRAS.find((y) => y.name === name); if (X) { at[name] = X.at; v.forEach((x, i) => w16(page, X.at + 2 * i, x)); continue; } }   // add-on tables: fixed places (also for the add-on path)
    if (typeof v === 'number') { at[name] = v - SIX_XIP; continue; }   // a value instead of a table
    at[name] = pos; v.forEach((x, i) => w16(page, pos + 2 * i, x)); pos += (2 * v.length + 3) & ~3;
  }
  if (pos > Math.min(0x800, ...EXTRAS.filter((X) => alt[X.name]).map((X) => X.at))) throw new Error('helper page full');
  if (12 * (sites.length + 1) > T_MORE - T_SITES) throw new Error('helper page full');
  sites.forEach(([o, , t, name], i) => { const e = T_SITES + 12 * i; w32(page, e, (XIP(f(o)) + 4) | 1); w32(page, e + 4, t); w32(page, e + 8, SIX_XIP + at[name]); });
  w32(page, T_SITES + 12 * sites.length, 0);
  // planet: original function, field byte, row per field slot
  w32(page, P_PMAP, K.planet.fn | 1); w32(page, P_PMAP + 4, K.planet.field);
  K.planet.rows.forEach((v, i) => { page[P_PMAP + 8 + i] = v; });
  if (fieldMenu) fmInstall(out, page, K);
  out.set(page, SIX_OFF);
  // firmware: background function -> b.w, child -> bl + b, sites -> bl + 2 nop
  const fn = (k) => u32(page, P_FN + 4 * k) & ~1;
  out.set(branch(XIP(f(K.bg.at)), fn(FN_BG), false), f(K.bg.at));
  { const src = XIP(f(K.child.at)), b = new Uint8Array(6); b.set(branch(src, fn(FN_CHILD), true));
    w16(b, 4, 0xE000 | (((XIP(f(K.child.back)) - (src + 8)) >> 1) & 0x7FF)); out.set(b, f(K.child.at)); }
  for (const [o, r] of sites) { const b = new Uint8Array(8); b.set(branch(XIP(f(o)), fn(FN_TAB[r]), true)); w16(b, 4, 0xBF00); w16(b, 6, 0xBF00); out.set(b, f(o)); }
  if (planet) for (const o of K.planet.calls) out.set(branch(XIP(f(o)), fn(FN_PMAP), true), f(o));
  if (games) gamesInstall(out, page, K, games);
  out.set(page, SIX_OFF);
  return out;
}
function gamesInstall(out, page, K, games) {
  const g = (k) => u32(page, P_GM_FN + 4 * k) & ~1, fo = (x) => x - 0x10000000 + 0x11000;
  w32(page, P_GM, games.list); w32(page, P_GM + 4, K.flag + 1); w32(page, P_GM + 8, 0x18001CB4);
  page.fill(0, P_GM + 0x0C, P_GM + 0x18); G_SCENES.forEach((v, i) => { page[P_GM + 0x0C + i] = v; });
  page.fill(0xFF, P_GM + 0x18, P_GM + 0x28); G_SEQ.forEach((q, fam) => q.forEach((v, i) => { page[P_GM + 0x18 + 8 * fam + i] = v; }));
  for (const [xip, len] of games.patches) {
    const b = new Uint8Array(len); b.set(branch(xip, g(0), true)); if (len === 8) { w16(b, 4, 0xBF00); w16(b, 6, 0xBF00); }
    out.set(b, fo(xip));
  }
  for (const [xip, k] of G_NAV) { if (hex(out, fo(xip), 4) !== G_NAV_ORIG) throw new Error('game place changed ' + xip.toString(16)); out.set(branch(xip, g(k), true), fo(xip)); }
}
/* DNA per field: can this ROM (with six fields of version 7 or new) get it? */
function dnaCan(rom, K) {
  const D = K && K.dna, fo = (x) => x - 0x10000000 + 0x11000;
  if (!SIX_DNA || !D) return false;
  return dnaSites(D).every(([x, h]) => hex(rom, fo(x), h.length / 2) === h);
}
/* the firmware places of the DNA part (with the food places when on) */
function dnaSites(D) { return SIX_FOOD && D.food ? D.sites.concat(D.food.sites) : D.sites; }
/* An old DNA part without the food places: put the DNA part in again (the saved values stay).
 * Jade Forest (no DNA part): the food part alone. */
function sixFood(rom, blob) {
  const info = sixInfo(rom), K = info && KINDS[info.kind], fo = (x) => x - 0x10000000 + 0x11000;
  if (!SIX_FOOD || !info || info.version < 7 || !K || !blob || blob.length <= P_DNA_FN + 4 * 11 || u32(blob, P_DNA_FN + 4 * 11) === 0) return rom;
  const F = K.dna ? K.dna.food : K.food;
  if (!F || !F.sites.every(([x, h]) => hex(rom, fo(x), h.length / 2) === h)) return rom;
  const out = new Uint8Array(rom);
  if (K.dna) { if (!info.dna) return rom; dnaStrip(out, K, info); dnaInstall(out, K, blob, info.fieldMenu); }
  else if (!info.dna) foodInstall(out, K, blob);
  return out;
}
/* Food part alone (Jade Forest): the code of the DNA part, only the values the food code reads. */
function foodInstall(out, K, blob) {
  const F = K.food, page = out.subarray(SIX_OFF, SIX_OFF + 0x1000);
  page.fill(0xFF, P_DNA, DNA_END); page.set(blob.subarray(P_DNA), P_DNA); page.fill(0, P_DNA, P_DNA + 0x40);
  w32(page, P_DNA, F.ram); w16(page, P_DNA + 6, F.field); page[P_DNA + 15] = F.slots;
  w16(page, P_DNA + 0x34, F.hist); F.kinds.forEach((v, i) => { page[P_DNA + 0x36 + i] = v; });
  w32(page, 32, u32(page, 32) | 64);
  sitesPut(out, F.sites, (k) => u32(page, P_DNA_FN + 4 * k) & ~1);
}
function foodStrip(out, K) {
  const F = K.food, page = out.subarray(SIX_OFF, SIX_OFF + 0x1000);
  for (const [x, h] of F.sites) for (let k = 0; k < h.length / 2; k++) out[x - 0x10000000 + 0x11000 + k] = parseInt(h.substr(2 * k, 2), 16);
  w32(page, 32, u32(page, 32) & ~64); page.fill(0xFF, P_DNA, DNA_END);
}
function sitesPut(out, sites, fn) {
  for (const [x, h, how, a] of sites) {
    const n = h.length / 2, b = new Uint8Array(n);
    if (how === 'hex') { for (let k = 0; k < n; k++) b[k] = parseInt(a.substr(2 * k, 2), 16); }
    else { b.set(branch(x, fn(a), how === 'bl')); for (let k = 4; k < n; k += 2) w16(b, k, 0xBF00); }
    out.set(b, x - 0x10000000 + 0x11000);
  }
}
/* Puts the DNA part in (page at SIX_OFF of out, its code from blob) and patches the firmware. */
function dnaInstall(out, K, blob, fieldMenu) {
  const D = K.dna, fo = (x) => x - 0x10000000 + 0x11000;
  if (!blob || blob.length <= P_DNA_FN) throw new Error('six fields program missing');
  const page = out.subarray(SIX_OFF, SIX_OFF + 0x1000);
  page.fill(0xFF, P_DNA, DNA_END); page.set(blob.subarray(P_DNA), P_DNA);
  w32(page, P_DNA, D.base); w16(page, P_DNA + 4, D.cnt); w16(page, P_DNA + 6, D.field); w16(page, P_DNA + 8, D.stash); w16(page, P_DNA + 10, D.mark);
  w16(page, P_DNA + 12, D.bits); page[P_DNA + 14] = D.mask; page[P_DNA + 15] = D.slots;
  page.fill(0, P_DNA + 0x10, P_DNA + 0x28); D.regions.forEach(([o, n], i) => { w16(page, P_DNA + 0x10 + 4 * i, o); w16(page, P_DNA + 0x12 + 4 * i, n); });
  D.icons.forEach((v, i) => w16(page, P_DNA + 0x28 + 2 * i, v));
  if (SIX_FOOD && D.food) { w16(page, P_DNA + 0x34, D.food.hist); D.food.kinds.forEach((v, i) => { page[P_DNA + 0x36 + i] = v; }); }
  w32(page, 32, u32(page, 32) | 8);
  const fn = (k) => u32(page, P_DNA_FN + 4 * k) & ~1;
  sitesPut(out, dnaSites(D), fn);
  out.set(branch(XIP(0x11000 + K.child.at), fn(DNA_CHILD), true), 0x11000 + K.child.at);
  if (fieldMenu) out.set(branch(0x10000000 + K.fm.set, fn(DNA_FSET), true), 0x11000 + K.fm.set);
}
/* Takes the DNA part out again (the firmware places as before, the six fields stay). */
function dnaStrip(out, K, info) {
  const D = K.dna, fo = (x) => x - 0x10000000 + 0x11000, page = out.subarray(SIX_OFF, SIX_OFF + 0x1000);
  if (!D) return;
  for (const [x, h] of D.food ? D.sites.concat(D.food.sites) : D.sites) for (let k = 0; k < h.length / 2; k++) out[fo(x) + k] = parseInt(h.substr(2 * k, 2), 16);
  const fn = (k) => u32(page, P_FN + 4 * k) & ~1;
  out.set(branch(XIP(0x11000 + K.child.at), fn(FN_CHILD), true), 0x11000 + K.child.at);
  if (info.fieldMenu) out.set(branch(0x10000000 + K.fm.set, u32(page, P_FM_FN + 8) & ~1, true), 0x11000 + K.fm.set);
  w32(page, 32, u32(page, 32) & ~8); page.fill(0xFF, P_DNA, DNA_END);
}
/**
 * DNA per field on or off for a ROM that has six fields already (no Land ROM needed). Returns the ROM
 * unchanged when there is nothing to do or the ROM cannot take it.
 */
function sixDna(rom, blob, on) {
  const info = sixInfo(rom);
  if (info && info.version >= 7 && on && (info.dna || !KINDS[info.kind].dna)) return sixFood(rom, blob);
  if (info && info.food && !on) { const o = new Uint8Array(rom); foodStrip(o, KINDS[info.kind]); return o; }
  if (!info || info.version < 7 || !!info.dna === !!on) return rom;
  const K = KINDS[info.kind], out = new Uint8Array(rom);
  if (!on) { dnaStrip(out, K, info); return out; }
  if (!SIX_DNA || !K.dna || !blob || u32(blob, P_DNA_FN) === 0 || !dnaCan(rom, K)) return rom;
  dnaInstall(out, K, blob, info.fieldMenu);
  return out;
}
/** Can this ROM get DNA per field? */
function sixDnaCan(rom) { const i = sixInfo(rom), k = i ? i.kind : kindOf(rom); return SIX_DNA && !!k && !!KINDS[k].dna; }
/* rows of an original table with some rows replaced: table at XIP address, per row n values */
function rows(rom, addr, n, nrows, repl) {
  const o = addr - 0x10000000 + 0x11000, v = [];
  for (let r = 0; r < nrows; r++) for (let i = 0; i < n; i++) v.push(repl[r] ? repl[r][i] : u16(rom, o + 2 * (n * r + i)));
  return v;
}
/**
 * More fields: returns a new image. rom: Tropical/Glacier or Jade Forest image without the menu
 * (call feldStrip first). land: the user's Land, Water or Sky image (only for Tropical/Glacier).
 * blob: six.bin.
 */
function sixApply(rom, land, blob, opts = {}) {
  const planet = opts.planet !== false;
  const have = sixInfo(rom);
  let fieldMenu = opts.fieldMenu !== false;
  const wantGames = SIX_GAMES && opts.games !== false && kindOf(rom) === 'gen3';
  const wantDna = SIX_DNA && opts.dna !== false && !!(KINDS[have ? have.kind : kindOf(rom)] || {}).dna;
  const wantFood = SIX_FOOD && opts.dna !== false;

  if (have && have.version >= SIX_VERSION && have.planet === planet && have.fieldMenu === fieldMenu && have.games === wantGames) { let r = sixDna(rom, blob, wantDna); if (wantFood) r = sixFood(r, blob); return sixHunt(r); }
  if (have) rom = sixStrip(rom);
  const kind = kindOf(rom);
  if (kind && fieldMenu && !fmCan(rom, KINDS[kind])) fieldMenu = false;
  if (!kind || !sixCan(rom)) throw new Error('more fields need a Tropical, Glacier or Jade Forest ROM with the original firmware');
  if (!blob || blob.length > 0x1000 || u32(blob, 0) !== SIX_MAGIC || u32(blob, 4) !== SIX_VERSION) throw new Error('six fields program missing');
  const dnaOk = wantDna && dnaCan(rom, KINDS[kind]);
  for (let i = SIX_OFF; i < SIX_OFF + 0x1000; i++) if (rom[i] !== 0xFF) throw new Error('area 0x10F000 is in use');
  if (kind === 'jade') {
    const T = C.openRom(rom), ix = words(dpSplit(T.files[0])[4]);
    if (ix.length <= 781 || comp(dpSplit(T.files[0]), 773)[0] !== 0x1007) throw new Error('unexpected Jade Forest data');
    const J = JADE_SKY, alt = {
      bg: rows(rom, KINDS.jade.bg.table, 16, 3, { 1: J.bg }), child: J.child,
      teen: rows(rom, 0x1007DFC4, 4, 3, { 1: J.teen }), adult: rows(rom, 0x1007DFDC, 16, 3, { 1: J.adult }),
      speed: rows(rom, 0x1008BCE8, 3, 3, { 1: J.speed }), egg: rows(rom, 0x100820E8, 1, 3, { 1: J.egg }),
      wagon: rows(rom, 0x100820D0, 4, 3, { 1: J.wagon }), scene: rows(rom, 0x10081198, 5, 3, { 1: J.scene }),
      spec1: 0x0FE40FE2 };   // Land 4066, Sky 4068 (Yayacorntchi) in the Water slot
    // planet: Sky stages in the Water row (the field slot of Sky)
    alt.plTab = rows(rom, 0x1008CBD0, 11, 3, { 1: SKY_PLANET });
    alt.pl0 = rows(rom, 0x100811DA, 11, 1, {}); alt.pl1 = SKY_PLANET; alt.pl2 = rows(rom, 0x100811F0, 11, 1, {});
    for (const X of EXTRAS) if (X.on() && opts[X.name] !== false && extraCan(rom, 'jade', X)) alt[X.name] = X.tab(rom, 'jade');
    const outJ = install(new Uint8Array(rom), 'jade', blob, alt, [0, 0, 0], planet, fieldMenu);
    if (dnaOk) dnaInstall(outJ, KINDS.jade, blob, fieldMenu);
    else if (wantFood) return sixFood(outJ, blob);
    return outJ;
  }
  if (!sixSource(land)) throw new Error('the second ROM must be a Land, Water or Sky ROM');
  const trop = rom;
  const T = C.openRom(trop), L = C.openRom(land);
  const tt = dpSplit(T.files[0]), lt = dpSplit(L.files[0]);
  const lw = (off) => u16(land, off), lfw = (x) => 0x11000 + x - 0x10000000;
  // Land backgrounds: 3 fields x 10 composites (5 times of day x clear/rain), base before them
  const lbg = []; for (let i = 0; i < 30; i++) lbg.push(lw(lfw(LAND_BG) + 2 * i));
  const adult = []; for (let i = 0; i < 48; i++) adult.push(lw(lfw(LAND_ADULT) + 2 * i));
  const entries = T.entries.slice();
  const nspr0 = entries.length, ix = words(tt[4]), cw = words(tt[5]), ncomp0 = ix.length, nanim0 = words(tt[13]).length;
  const compNew = new Map(), sprNew = new Map();
  const fields = [0, 1, 2].map((fi) => {
    const row = lbg.slice(10 * fi, 10 * fi + 10), base = row[0] - 1;
    const clear = [base, row[0], row[2], row[4], row[6], row[8]], rain = [row[1], row[3], row[5], row[7], row[9]];
    const sprs = [], ext = [];
    for (const c of clear.concat(rain)) {
      const w = comp(lt, c);
      if (w[0] !== 0x1007 || w.length < 4) throw new Error('unexpected Land picture ' + c);
      if (!sprs.includes(w[3])) { sprs.push(w[3]); ext.push(!clear.includes(c)); }
    }
    return { row, base, sprs, ext };
  });
  const bList = [], bData = []; let bPos = 0;
  for (const fd of fields) {
    const group = fd.sprs.map((i) => { const m = mapCells(L.entries[i]); m.h.raw = L.entries[i]; return m; });
    const packed = packGroup(group, K_COLORS, fd.ext);
    fd.sprs.forEach((i, j) => {
      sprNew.set(i, entries.length);
      if (packed.outside[j]) bList.push({ id: entries.length, rel: packed.outside[j].map((x) => (x < 0 ? -1 : x + bPos)) });
      entries.push(packed[j]);
    });
    bData.push(packed.bdata); bPos += packed.bdata.length; const pad = (4 - bPos % 4) % 4; if (pad) { bData.push(new Uint8Array(pad)); bPos += pad; }
  }
  const bHead = 8 + bList.length * 260;
  if (B_OFF + bHead + bPos > B_END) throw new Error('rain pictures too big');
  const addComp = (c) => {
    if (compNew.has(c)) return compNew.get(c);
    const w = comp(lt, c).slice(); w[3] = sprNew.get(w[3]);
    const id = ix.length; ix.push(cw.length); cw.push(...w); if (cw.length & 1) cw.push(0);
    compNew.set(c, id); return id;
  };
  const tBg = [];
  for (const fd of fields) {
    const half = [addComp(fd.base)];
    for (let s = 0; s < 5; s++) { const c = addComp(fd.row[2 * s]), r = addComp(fd.row[2 * s + 1]); half.push(c, r, r); }
    tBg.push(...half, ...half);
  }
  if (cw.length > 0xFFFF) throw new Error('composite table full');
  tt[4] = toBytes(ix); tt[5] = toBytes(cw);
  // scenes: travel backgrounds, field eggs, scene animations, wagon
  const imp = importer({ tabs: lt, entries: L.entries }, { tabs: tt, entries: T.entries }, entries);
  const speed = []; for (let i = 0; i < 9; i++) speed.push(imp.anim(lw(lfw(LAND_SPEED) + 2 * i)));
  const firstPic = (a, d = 0) => {
    for (const o of parseScript(scripts(lt).get(a))) {
      if (o.mask & 0x80 && o.p.length && o.op === 1) return o.p[0];
      if (o.mask & 0x80 && o.p.length && o.op === 16 && d < 4) { const r = firstPic(o.p[0], d + 1); if (r >= 0) return r; }
    }
    return -1;
  };
  const egg = [0, 1, 2].map((fi) => imp.anim(LAND_EGG + fi));
  const eggPic = [0, 1, 2].map((fi) => { const p = firstPic(LAND_EGG + fi); if (p < 0) throw new Error('unexpected Land picture ' + (LAND_EGG + fi)); return imp.sprite(p); });
  const scene = [], wagon = [], tw = rows(trop, 0x10095778, 4, 3, {});
  for (let fi = 0; fi < 3; fi++) {
    for (const c of LAND_SCENE) scene.push(imp.anim(c + fi));
    wagon.push(imp.anim(LAND_WAGON[0] + fi), imp.anim(LAND_WAGON[1] + fi), tw[4 * fi + 2], tw[4 * fi + 3]);
  }
  imp.done();
  // lab games per start field: Land pictures go in with the rest, the site list after the rain cells
  let games = null;
  const gBase = (B_OFF + bHead + bPos + 0xFFF) & ~0xFFF;
  if (SIX_GAMES && opts.games !== false) {
    const X = translator({ tabs: lt, entries: L.entries }, tt, entries);
    games = gamesBuild(trop, X, gBase);
    X.done();
    if (gBase + games.bytes.length > B_END) throw new Error('rain pictures too big');
  }
  for (const [a, n] of TROP_MAPS) for (let i = a; i < a + n; i++) entries[i] = reduceOne(entries[i], K_COLORS);
  const out = C.buildRom({ rom: trop, entries, files: [null, null, T.files[2]], limit: T.limit, assetEnd: T.assetEnd }, new Map(), dpJoin(tt)).rom;
  const alt = { bg: tBg, child: [2002, 2003, 2004], teen: Array.from({ length: 12 }, (_, i) => 3005 + i), adult, speed, egg, eggPic, scene, wagon,
    spec1: 0x0FE30FE2, spec2: 0x0FE40FE4 };   // specials Chodracotchi 4066, Mermarintchi 4067, Yayacorntchi 4068
  // planet stages of Land, Water, Sky (from the Land ROM, the same in all three)
  alt.plTab = []; for (let i = 0; i < 33; i++) alt.plTab.push(lw(lfw(LAND_PLANET) + 2 * i));
  if (alt.plTab[0] !== 2 || alt.plTab[11] !== 2 || alt.plTab[22] !== 2) throw new Error('unexpected Land planet table');
  alt.pl0 = alt.plTab.slice(0, 11); alt.pl1 = alt.plTab.slice(11, 22); alt.pl2 = alt.plTab.slice(22, 33);
  for (const X of EXTRAS) if (X.on() && opts[X.name] !== false && extraCan(trop, 'gen3', X)) alt[X.name] = X.tab(trop, 'gen3');
  install(out, 'gen3', blob, alt, [nspr0, ncomp0, nanim0], planet, fieldMenu, games);
  if (dnaOk) dnaInstall(out, KINDS.gen3, blob, fieldMenu);
  // region B: rain cells
  for (let i = B_OFF; i < B_END; i++) if (trop[i] !== 0xFF) throw new Error('area 0xF00000 is in use');
  const bh = new Uint8Array(bHead); w32(bh, 0, B_MAGIC); w32(bh, 4, bList.length);
  bList.forEach((x, k) => { const o = 8 + k * 260; w32(bh, o, x.id); x.rel.forEach((r, t) => w32(bh, o + 4 + 4 * t, r < 0 ? 0xFFFFFFFF : r)); });
  out.set(bh, B_OFF); out.set(concat(bData), B_OFF + bHead);
  if (games) out.set(games.bytes, gBase);
  return sixFix(out);
}
/** Points the rain pictures at their cells in region B again (call after every archive rebuild). */
function sixFix(rom) {
  if (!sixInfo(rom) || u32(rom, B_OFF) !== B_MAGIC) return rom;
  const out = new Uint8Array(rom), n = u32(out, B_OFF + 4), data = B_OFF + 8 + n * 260;
  const pack = 0x111000 + u32(out, 0x111000 + 16 + 16 + 4);
  for (let k = 0; k < n; k++) {
    const o = B_OFF + 8 + k * 260, id = u32(out, o), h = pack + u32(out, pack + 4 * id), pix = h + u16(out, h + 20);
    for (let t = 0; t < 64; t++) { const r = u32(out, o + 4 + 4 * t); if (r !== 0xFFFFFFFF) w32(out, pix + 8 * t, data + r - pix); }
  }
  w32(out, 0x111004, C.arc2Checksum(out, 0x111000, u32(out, 0x111008)));   // archive checksum
  return out;
}

const SIX_B64 = '__SIX_B64__';
function embedded() { return SIX_B64.startsWith('__') ? null : b64(SIX_B64); }

root.ParamixSix = { VERSION: SIX_VERSION, DNA: SIX_DNA, HUNT: SIX_HUNT, sixHunt, sixDna, sixDnaCan, sixApply, sixStrip, sixFix, sixCan, sixSource, sixInfo, sixNeedsSource, kindOf, embedded, FLAG_OFF, SIX_OFF, packGroup, reduceOne, mapCells };
if (typeof module !== 'undefined') module.exports = root.ParamixSix;
})(typeof window !== 'undefined' ? window : globalThis);
