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
const SIX_VERSION = 6;
// page layout (six/src/six.s)
const P_FM = 0x800, P_FM_FN = 0x830, P_PMAP = 0x1B0, P_BG = 0x1C0, P_CHILD = 0x1CC, P_FN = 0x1E0, T_BG = 0x200, T_CHILD = 0x340, T_CHILD_O = 0x348, T_SITES = 0x380, T_MORE = 0x500;
const FN_TAB = [0, 1, 5], FN_BG = 2, FN_CHILD = 3, FN_PMAP = 4;
// Region B (0xF00000, unused by the firmware): cells of the rain pictures (Tropical/Glacier). Header
// "SIXB", count, per picture: sprite id and 64 offsets into the data (0xFFFFFFFF = cell in the archive).
const B_OFF = 0xF00000, B_END = 0x1000000, B_MAGIC = 0x42584953;
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
      imm: [[0x265EA, 0x50, 0x48], [0x41496, 0x4F, 0x53], [0x41618, 0x4F, 0x53]], off: [0x4C, 0x48, 0x53, 0x68], ent: [0, 1, 2, 0x80, 0x81, 0x82] } },
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
      imm: [[0x22634, 0x4E, 0x48], [0x3C29E, 0x4D, 0x4F], [0x3C3F8, 0x4D, 0x4F]], off: [0x4A, 0x48, 0x4F, 0x64], ent: [0, 1, 2, 0x81] } },
};
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
    planet: version >= 5 && (u32(rom, SIX_OFF + 32) & 1) === 1, fieldMenu: version >= 6 && (u32(rom, SIX_OFF + 32) & 2) === 2 };
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
  put(f(K.bg.at), K.bg.orig); put(f(K.child.at), K.child.orig);
  if (info.kind === 'gen3') for (const [o, h] of OLD_PATCH) put(o, h);
  for (const [o, r, t] of K.sites.concat(K.planet.sites)) out.set(movPairBytes(r, t), f(o));
  for (const o of K.planet.calls) out.set(branch(XIP(f(o)), K.planet.fn, true), f(o));
  if (info.fieldMenu) fmStrip(out, K);
  out.fill(0xFF, SIX_OFF, SIX_OFF + 0x1000);
  if (u32(out, B_OFF) === B_MAGIC) out.fill(0xFF, B_OFF, B_END);
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
function install(out, kind, blob, alt, counts, planet, fieldMenu) {
  const K = KINDS[kind], f = (o) => 0x11000 + o, sites = planet ? K.sites.concat(K.planet.sites) : K.sites;
  const page = new Uint8Array(0x1000).fill(0xFF); page.set(blob);
  w32(page, 8, K.flag); w32(page, 12, FLAG_OFF);
  w32(page, 16, counts[0]); w32(page, 20, counts[1]); w32(page, 24, counts[2]); w32(page, 28, K.id); w32(page, 32, (planet ? 1 : 0) | (fieldMenu ? 2 : 0));
  // background table and child list
  alt.bg.forEach((v, i) => w16(page, T_BG + 2 * i, v));
  w32(page, P_BG, K.bg.table); w32(page, P_BG + 4, SIX_XIP + T_BG); w32(page, P_BG + 8, K.bg.mode);
  K.child.list.forEach((v, i) => w16(page, T_CHILD_O + 2 * i, v)); alt.child.forEach((v, i) => w16(page, T_CHILD + 2 * i, v));
  w32(page, P_CHILD, SIX_XIP + T_CHILD_O); w32(page, P_CHILD + 4, SIX_XIP + T_CHILD); w32(page, P_CHILD + 8, K.child.sp);
  // other tables, then the site list
  let pos = T_MORE; const at = {};
  for (const name of new Set(sites.map((x) => x[3]))) {
    const v = alt[name]; if (v === undefined) throw new Error('table ' + name + ' missing');
    if (typeof v === 'number') { at[name] = v - SIX_XIP; continue; }   // a value instead of a table
    at[name] = pos; v.forEach((x, i) => w16(page, pos + 2 * i, x)); pos += (2 * v.length + 3) & ~3;
  }
  if (pos > 0x800) throw new Error('helper page full');
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
  return out;
}
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
  if (have && have.version >= SIX_VERSION && have.planet === planet && have.fieldMenu === fieldMenu) return rom;
  if (have) rom = sixStrip(rom);
  const kind = kindOf(rom);
  if (kind && fieldMenu && !fmCan(rom, KINDS[kind])) fieldMenu = false;
  if (!kind || !sixCan(rom)) throw new Error('more fields need a Tropical, Glacier or Jade Forest ROM with the original firmware');
  if (!blob || blob.length > 0x1000 || u32(blob, 0) !== SIX_MAGIC || u32(blob, 4) !== SIX_VERSION) throw new Error('six fields program missing');
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
    return install(new Uint8Array(rom), 'jade', blob, alt, [0, 0, 0], planet, fieldMenu);
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
  for (const [a, n] of TROP_MAPS) for (let i = a; i < a + n; i++) entries[i] = reduceOne(entries[i], K_COLORS);
  const out = C.buildRom({ rom: trop, entries, files: [null, null, T.files[2]], limit: T.limit, assetEnd: T.assetEnd }, new Map(), dpJoin(tt)).rom;
  const alt = { bg: tBg, child: [2002, 2003, 2004], teen: Array.from({ length: 12 }, (_, i) => 3005 + i), adult, speed, egg, eggPic, scene, wagon,
    spec1: 0x0FE30FE2, spec2: 0x0FE40FE4 };   // specials Chodracotchi 4066, Mermarintchi 4067, Yayacorntchi 4068
  // planet stages of Land, Water, Sky (from the Land ROM, the same in all three)
  alt.plTab = []; for (let i = 0; i < 33; i++) alt.plTab.push(lw(lfw(LAND_PLANET) + 2 * i));
  if (alt.plTab[0] !== 2 || alt.plTab[11] !== 2 || alt.plTab[22] !== 2) throw new Error('unexpected Land planet table');
  alt.pl0 = alt.plTab.slice(0, 11); alt.pl1 = alt.plTab.slice(11, 22); alt.pl2 = alt.plTab.slice(22, 33);
  install(out, 'gen3', blob, alt, [nspr0, ncomp0, nanim0], planet, fieldMenu);
  // region B: rain cells
  for (let i = B_OFF; i < B_END; i++) if (trop[i] !== 0xFF) throw new Error('area 0xF00000 is in use');
  const bh = new Uint8Array(bHead); w32(bh, 0, B_MAGIC); w32(bh, 4, bList.length);
  bList.forEach((x, k) => { const o = 8 + k * 260; w32(bh, o, x.id); x.rel.forEach((r, t) => w32(bh, o + 4 + 4 * t, r < 0 ? 0xFFFFFFFF : r)); });
  out.set(bh, B_OFF); out.set(concat(bData), B_OFF + bHead);
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

root.ParamixSix = { VERSION: SIX_VERSION, sixApply, sixStrip, sixFix, sixCan, sixSource, sixInfo, sixNeedsSource, kindOf, embedded, FLAG_OFF, SIX_OFF, packGroup, reduceOne, mapCells };
if (typeof module !== 'undefined') module.exports = root.ParamixSix;
})(typeof window !== 'undefined' ? window : globalThis);
