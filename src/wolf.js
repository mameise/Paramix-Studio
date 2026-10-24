/*
 * Paramix Studio: Wolfenstein 3D for the Paradise, the data pack.
 * The user brings the game files (shareware .WL1 or full version .WL6); they never leave the computer.
 * The Studio only reads them in the browser and puts what the program needs into one pack for the flash.
 *
 * Pack "W3DP" (little endian, offsets from the start of the pack):
 *   0  'W3DP'  4 version (1)  8 flags (bit 0 shareware)  12 total length
 *  16  pages: count, first sprite, table offset   (table: offset u32, length u32; walls 64x64 by columns, sprites as in VSWAP)
 *  28  maps:  count, RLEW tag, table offset       (table: offset u32 or 0; map header 38 bytes as in GAMEMAPS, plane starts
 *                                                  relative to the pack, planes Carmack + RLEW packed as in GAMEMAPS)
 *  40  sounds: count, table offset                (PC speaker sounds: offset u32, length u32; data = u32 length, u16 priority, bytes)
 *  48  pics:  count, table offset                 (table: offset u32, width u16, height u16; 8-bit, rows from the top)
 *  56  reserved                                  60 episodes
 * Pics: the range L_COLONPIC .. GETPSYCHEDPIC of the game (91 pics, same order in all versions; TITLEPIC is pic 43).
 *
 * Author: mameise
 */
(function (root) {
'use strict';

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const s32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
const u32 = (b, o) => s32(b, o) >>> 0;
const u24 = (b, o) => b[o] | (b[o + 1] << 8) | (b[o + 2] << 16);
function w16(b, o, v) { b[o] = v & 255; b[o + 1] = (v >> 8) & 255; }
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }

const NUMSOUNDS = 87;          // audiowl6.h LASTSOUND (shareware and full version)
const PIC_FIRST = { 150: 44, 157: 56 };   // chunk of L_COLONPIC by number of chunks (Activision/GT 1.4, Apogee 1.4)
const PIC_COUNT = 91;          // L_COLONPIC .. GETPSYCHEDPIC
// used by the program: STATUSBAR (42, its colour), TITLE (43), weapons, keys, digits, faces, PAUSED, GETPSYCHED (47..90)
const PIC_USED = (k) => k === 42 || k === 43 || k >= 47;

const NEED = ['VSWAP', 'MAPHEAD', 'GAMEMAPS', 'AUDIOHED', 'AUDIOT', 'VGAHEAD', 'VGADICT', 'VGAGRAPH'];

/** files: { 'VSWAP.WL1': Uint8Array, ... } (any case). Returns { ext, files } of one complete set or throws. */
function pickSet(files) {
  const by = {};
  for (const n of Object.keys(files)) {
    const m = /^([A-Za-z]+)\.(WL1|WL6)$/i.exec(n.replace(/^.*[\\/]/, ''));
    if (m) (by[m[2].toUpperCase()] = by[m[2].toUpperCase()] || {})[m[1].toUpperCase()] = files[n];
  }
  for (const ext of ['WL6', 'WL1']) {
    const s = by[ext]; if (!s) continue;
    const miss = NEED.filter((k) => !s[k]);
    if (!miss.length) return { ext, files: s };
  }
  const have = Object.keys(by);
  if (!have.length) throw new Error('no Wolfenstein 3D files (.WL1 or .WL6)');
  const ext = have.includes('WL6') ? 'WL6' : 'WL1';
  throw new Error('missing: ' + NEED.filter((k) => !by[ext][k]).map((k) => k + '.' + ext).join(', '));
}

function huffExpand(src, so, len, nodes) {
  const out = new Uint8Array(len); let o = 0, n = 254, i = so, val = src[i++], mask = 1;
  while (o < len) {
    const v = (val & mask) ? nodes[2 * n + 1] : nodes[2 * n];
    if (mask === 0x80) { val = src[i++]; mask = 1; } else mask <<= 1;
    if (v < 256) { out[o++] = v; n = 254; } else n = v - 256;
  }
  return out;
}

function readGraph(head, dict, graph) {
  const nchunks = Math.floor(head.length / 3);
  const nodes = new Uint16Array(255 * 2); for (let i = 0; i < 510; i++) nodes[i] = u16(dict, 2 * i);
  const pos = (c) => { const v = u24(head, 3 * c); return v === 0xFFFFFF ? -1 : v; };
  const chunk = (c, explicit) => {
    const p = pos(c); if (p < 0) return null;
    let q = c + 1; while (q < nchunks && pos(q) < 0) q++;
    const len = explicit !== undefined ? explicit : s32(graph, p);
    return huffExpand(graph, explicit !== undefined ? p : p + 4, len, nodes);
  };
  return { nchunks, chunk };
}

function deplane(d, w, h) {
  const out = new Uint8Array(w * h), pw = w >> 2; let s = 0;
  for (let p = 0; p < 4; p++) for (let y = 0; y < h; y++) for (let x = 0; x < pw; x++) out[y * w + (x << 2) + p] = d[s++];
  return out;
}

/** Build the pack. files: complete set (see pickSet). */
function build(files) {
  const { ext, files: f } = pickSet(files);
  const parts = []; let size = 64;
  const add = (b) => { const o = size; parts.push([o, b]); size += b.length; size = (size + 3) & ~3; return o; };
  const hdr = new Uint8Array(64);
  // pages (walls and sprites, no digitized sounds)
  const vs = f.VSWAP, nch = u16(vs, 0), spr = u16(vs, 2), snd = u16(vs, 4);
  // walls are 64 x 64 (4096 bytes); modified games (MacenWolf: 128 x 128) do not run with this program
  for (let i = 0; i < spr; i++) { const len = u16(vs, 6 + 4 * nch + 2 * i); if (len && len !== 4096) throw new Error('VSWAP.' + ext + ': walls not 64 x 64 (a modified game?), version 1.4 needed'); }
  const npages = snd, ptab = new Uint8Array(npages * 8);
  for (let i = 0; i < npages; i++) {
    const off = u32(vs, 6 + 4 * i), len = u16(vs, 6 + 4 * nch + 2 * i);
    if (!off || !len || off + len > vs.length) continue;
    w32(ptab, 8 * i, add(vs.subarray(off, off + len))); w32(ptab, 8 * i + 4, len);
  }
  w32(hdr, 16, npages); w32(hdr, 20, spr); w32(hdr, 24, add(ptab));
  // maps
  const mh = f.MAPHEAD, gm = f.GAMEMAPS, tag = u16(mh, 0);
  let nmaps = 0; const moff = [];
  for (let i = 0; 2 + 4 * i + 4 <= mh.length && i < 100; i++) { const o = s32(mh, 2 + 4 * i); moff.push(o > 0 && o + 38 <= gm.length ? o : 0); if (moff[i]) nmaps = i + 1; }
  const mtab = new Uint8Array(nmaps * 4);
  for (let i = 0; i < nmaps; i++) {
    const o = moff[i]; if (!o) continue;
    const h = new Uint8Array(gm.subarray(o, o + 38));
    const at = add(h);
    for (let p = 0; p < 3; p++) {
      const ps = s32(gm, o + 4 * p), pl = u16(gm, o + 12 + 2 * p);
      w32(h, 4 * p, ps > 0 && pl ? add(gm.subarray(ps, ps + pl)) : 0);
    }
    w32(mtab, 4 * i, at);
  }
  w32(hdr, 28, nmaps); w32(hdr, 32, tag); w32(hdr, 36, add(mtab));
  // PC speaker sounds
  const ah = f.AUDIOHED, at = f.AUDIOT, stab = new Uint8Array(NUMSOUNDS * 8);
  for (let i = 0; i < NUMSOUNDS && 4 * i + 8 <= ah.length; i++) {
    const a = u32(ah, 4 * i), b = u32(ah, 4 * i + 4);
    if (b > a && b <= at.length) { w32(stab, 8 * i, add(at.subarray(a, b))); w32(stab, 8 * i + 4, b - a); }
  }
  w32(hdr, 40, NUMSOUNDS); w32(hdr, 44, add(stab));
  // pictures
  const g = readGraph(f.VGAHEAD, f.VGADICT, f.VGAGRAPH);
  const first = PIC_FIRST[g.nchunks];
  if (first === undefined) throw new Error('unknown version of VGAHEAD (' + g.nchunks + ' chunks)');
  const st = g.chunk(0);                       // STRUCTPIC: width, height of every pic from chunk 3
  const pics = []; for (let k = 0; k < PIC_COUNT; k++) pics.push(first + k);   // TITLEPIC is pic 43
  const xtab = new Uint8Array(pics.length * 8);
  pics.forEach((c, k) => {
    if (!PIC_USED(k)) return;   // pictures the program never shows stay out (flash)
    const w = s16v(st, 4 * (c - 3)), h = s16v(st, 4 * (c - 3) + 2), d = g.chunk(c);
    if (!d || w <= 0 || h <= 0 || (w & 3) || d.length < w * h) return;
    w32(xtab, 8 * k, add(deplane(d, w, h))); w16(xtab, 8 * k + 4, w); w16(xtab, 8 * k + 6, h);
  });
  w32(hdr, 48, pics.length); w32(hdr, 52, add(xtab));
  w32(hdr, 60, Math.max(1, Math.floor(nmaps / 10)));
  hdr.set([0x57, 0x33, 0x44, 0x50]); w32(hdr, 4, 1); w32(hdr, 8, ext === 'WL1' ? 1 : 0); w32(hdr, 12, size);
  const out = new Uint8Array(size); out.set(hdr);
  for (const [o, b] of parts) out.set(b, o);
  return { pack: out, ext, maps: nmaps, episodes: Math.max(1, Math.floor(nmaps / 10)) };
}
function s16v(b, o) { const v = u16(b, o); return v > 32767 ? v - 65536 : v; }
root.ParamixWolf = { pickSet, build, readGraph, NEED };
if (typeof module !== 'undefined') module.exports = root.ParamixWolf;
})(typeof window !== 'undefined' ? window : globalThis);
