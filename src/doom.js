/*
 * Paramix Studio: install DOOM into the Paradise ROM.
 * The user supplies the WAD (DOOM.WAD); it never leaves the computer.
 * The studio only contains the program (GPL, derived from next-hack nRF52840Doom / GBADoom / PrBoom)
 * and the WAD converter (MCUDoomWadUtil, GPL) as WebAssembly. No game graphics, no WAD.
 *
 * Flow: partial WAD (one episode) -> extra lumps from the same WAD -> converter -> into the ROM.
 * Layout in the Paradise flash:
 *   0x0A0000  DOOM program (PRAM part, loaded by the stub)
 *   0x0B0000  stub and home screen hook (hold A+C), same as the emulator
 *   0x0B1000  DOOM program (part in the flash window)
 *   0x10E000  settings and save (2 x 4 KB)
 *   0x830000  converted WAD (preceded by 4-byte length), 0xCA0000 region for precomputed data
 *
 * Author: mameise
 */
(function (root) {
'use strict';

const CODE_OFF = 0xB1000, SAVE_OFF = 0x10E000, SAVE_END = 0x110000;
const WAD_OFF = 0x830000, REGION_OFF = 0xCA0000, REGION_END = 0xD48000;
const PW = 128, PH = 128;

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const s16 = (b, o) => { const v = u16(b, o); return v > 32767 ? v - 65536 : v; };
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w16(b, o, v) { b[o] = v & 255; b[o + 1] = (v >> 8) & 255; }
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
const name8 = (b, o) => { let e = o + 8; while (e > o && b[e - 1] === 0) e--; let s = ''; for (let i = o; i < e; i++) s += String.fromCharCode(b[i]); return s; };
function b64(s) {
  if (typeof atob === 'function') { const t = atob(s); const o = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) o[i] = t.charCodeAt(i); return o; }
  return new Uint8Array(Buffer.from(s, 'base64'));
}

// ------------------------------------------------------------------ WAD
function readWad(d) {
  const id = String.fromCharCode(d[0], d[1], d[2], d[3]);
  if (id !== 'IWAD' && id !== 'PWAD') throw new Error('not a WAD file');
  const n = u32(d, 4), off = u32(d, 8), L = [];
  for (let i = 0; i < n; i++) {
    const o = u32(d, off + 16 * i), s = u32(d, off + 16 * i + 4);
    L.push([name8(d, off + 16 * i + 8), d.subarray(o, o + s)]);
  }
  return L;
}
/** Like wadsub.py: header, data in one piece, directory at the end. */
function writeWad(L, ident) {
  let size = 12; for (const [, d] of L) size += d.length;
  const out = new Uint8Array(size + 16 * L.length);
  out.set([...ident].map(c => c.charCodeAt(0)), 0); w32(out, 4, L.length); w32(out, 8, size);
  let p = 12, q = size;
  for (const [nm, d] of L) {
    out.set(d, p); w32(out, q, p); w32(out, q + 4, d.length);
    for (let k = 0; k < 8 && k < nm.length; k++) out[q + 8 + k] = nm.charCodeAt(k) & 255;
    p += d.length; q += 16;
  }
  return out;
}
/** Which episodes does the WAD have? E.g. ['1'] (Shareware), ['1','2','3'] (DOOM), or also '4' (Ultimate). */
function wadEpisodes(d) {
  const eps = new Set();
  for (const [nm] of readWad(d)) { const m = /^E(\d)M\d$/.exec(nm); if (m) eps.add(m[1]); }
  if (!eps.size) throw new Error('no DOOM maps in the WAD (DOOM 2 is not supported)');
  return [...eps].sort();
}

// ------------------------------------------------------------ partial WAD (wadsub.py)
const MAPL = new Set(['THINGS', 'LINEDEFS', 'SIDEDEFS', 'VERTEXES', 'SEGS', 'SSECTORS', 'NODES', 'SECTORS', 'REJECT', 'BLOCKMAP']);
const ANIMS = [['NUKAGE3', 'NUKAGE1'], ['FWATER4', 'FWATER1'], ['SWATER4', 'SWATER1'], ['LAVA4', 'LAVA1'], ['BLOOD3', 'BLOOD1'], ['RROCK08', 'RROCK05'], ['SLIME04', 'SLIME01'], ['SLIME08', 'SLIME05'], ['SLIME12', 'SLIME09']];
const TANIMS = [['BLODGR4', 'BLODGR1'], ['SLADRIP3', 'SLADRIP1'], ['BLODRIP4', 'BLODRIP1'], ['FIREWALL', 'FIREWALA'], ['GSTFONT3', 'GSTFONT1'], ['FIRELAVA', 'FIRELAV3'], ['FIREMAG3', 'FIREMAG1'], ['FIREBLU2', 'FIREBLU1'], ['ROCKRED3', 'ROCKRED1'], ['BFALL4', 'BFALL1'], ['SFALL4', 'SFALL1'], ['WFALL4', 'WFALL1'], ['DBRAIN4', 'DBRAIN1']];
const MON = { 3004: ['POSS'], 9: ['SPOS'], 3001: ['TROO', 'BAL1'], 3002: ['SARG'], 58: ['SARG'], 3003: ['BOSS', 'BAL7'], 3005: ['HEAD', 'BAL2'], 3006: ['SKUL'], 16: ['CYBR'], 7: ['SPID'] };
const FULL = new Set(['TITLEPIC', 'HELP1', 'HELP2', 'CREDIT', 'VICTORY2', 'PFUB1', 'PFUB2', 'INTERPIC']);
const upName = (b, o) => name8(b, o).toUpperCase();

function readTex(dd) {
  const cnt = u32(dd, 0), res = [];
  for (let i = 0; i < cnt; i++) { const o = u32(dd, 4 + 4 * i); const pc = u16(dd, o + 20); res.push([upName(dd, o), dd.subarray(o, o + 22 + 10 * pc)]); }
  return res;
}
function patchDecode(dd) {
  const w = s16(dd, 0), h = s16(dd, 2);
  if (w <= 0 || h <= 0) throw new Error('no picture');
  const img = new Uint8Array(w * h);
  for (let x = 0; x < w; x++) {
    let p = u32(dd, 8 + 4 * x);
    while (dd[p] !== 0xFF) {
      if (p >= dd.length) throw new Error('broken');
      const top = dd[p], ln = dd[p + 1];
      for (let k = 0; k < ln; k++) { const y = top + k; if (y < h) img[y * w + x] = dd[p + 3 + k]; }
      p += ln + 4;
    }
  }
  return { w, h, img };
}
function fullEncode(img, w, h) {
  const colLen = 3 + h + 2, out = new Uint8Array(8 + 4 * w + colLen * w);
  w16(out, 0, w); w16(out, 2, h);
  for (let x = 0; x < w; x++) {
    const o = 8 + 4 * w + colLen * x; w32(out, 8 + 4 * x, o);
    out[o] = 0; out[o + 1] = h; out[o + 2] = 0;
    for (let y = 0; y < h; y++) out[o + 3 + y] = img[y * w + x];
    out[o + 3 + h] = 0; out[o + 4 + h] = 0xFF;
  }
  return out;
}
/** Average per target cell, then nearest palette color (same as wadsub.py). */
function shrink(img, w, h, pal) {
  const near = new Map(), res = new Uint8Array(PW * PH);
  const nearest = (r, g, b) => {
    const key = (r << 16) | (g << 8) | b; let v = near.get(key);
    if (v === undefined) {
      let best = 1 << 30; v = 0;
      for (let i = 0; i < 256; i++) { const dr = pal[3 * i] - r, dg = pal[3 * i + 1] - g, db = pal[3 * i + 2] - b, e = dr * dr + dg * dg + db * db; if (e < best) { best = e; v = i; } }
      near.set(key, v);
    }
    return v;
  };
  for (let ty = 0; ty < PH; ty++) {
    const y0 = Math.floor(ty * h / PH), y1 = Math.max(y0 + 1, Math.floor((ty + 1) * h / PH));
    for (let tx = 0; tx < PW; tx++) {
      const x0 = Math.floor(tx * w / PW), x1 = Math.max(x0 + 1, Math.floor((tx + 1) * w / PW));
      let sr = 0, sg = 0, sb = 0, n = 0;
      for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) { const c = img[y * w + x]; sr += pal[3 * c]; sg += pal[3 * c + 1]; sb += pal[3 * c + 2]; n++; }
      res[ty * PW + tx] = nearest(Math.floor((sr + (n >> 1)) / n), Math.floor((sg + (n >> 1)) / n), Math.floor((sb + (n >> 1)) / n));
    }
  }
  return res;
}

/** Partial WAD: only episode eps (e.g. '1'), only used textures, flats and monsters, full-screen pictures 128x128. */
function wadSub(src, eps, music) {
  const L = readWad(src).map(([n, d]) => [n, d]);
  let out = []; const usedtex = new Set(), usedflat = new Set(), usedthings = new Set();
  let i = 0;
  const musRe = new RegExp('^D_E[' + eps + ']M\\d$');
  while (i < L.length) {
    const [nm] = L[i];
    const m = /^E(\d)M(\d)$/.exec(nm);
    if (m) {
      const keep = eps.includes(m[1]);
      let j = i + 1; while (j < L.length && MAPL.has(L[j][0])) j++;
      if (keep) {
        for (let k = i; k < j; k++) {
          out.push(L[k]);
          const [n2, dd] = L[k];
          if (n2 === 'SIDEDEFS') for (let q = 0; q + 30 <= dd.length; q += 30) [4, 12, 20].forEach(o => usedtex.add(upName(dd, q + o)));
          if (n2 === 'THINGS') for (let q = 0; q + 10 <= dd.length; q += 10) usedthings.add(u16(dd, q + 6));
          if (n2 === 'SECTORS') for (let q = 0; q + 26 <= dd.length; q += 26) [4, 12].forEach(o => usedflat.add(upName(dd, q + o)));
        }
      }
      i = j; continue;
    }
    if (nm.startsWith('D_') && music && (musRe.test(nm) || ['D_INTER', 'D_INTRO', 'D_INTROA', 'D_VICTOR'].includes(nm))) { out.push(L[i]); i++; continue; }
    if (nm.startsWith('DS') || nm.startsWith('D_') || ['ENDOOM', 'GENMIDI', 'DMXGUS'].includes(nm) || /^DEMO\d$/.test(nm)) { i++; continue; }
    out.push(L[i]); i++;
  }
  for (const e of eps) usedtex.add('SKY' + e);
  for (const t of [...usedtex]) { if (t.startsWith('SW1')) usedtex.add('SW2' + t.slice(3)); if (t.startsWith('SW2')) usedtex.add('SW1' + t.slice(3)); }
  let names = out.map(x => x[0]);
  let fs = names.indexOf('F_START'), fe = names.indexOf('F_END');
  const flats = names.slice(fs + 1, fe);
  for (const [a, b] of ANIMS) {
    if (flats.includes(a) && flats.includes(b)) {
      const rng = flats.slice(flats.indexOf(b), flats.indexOf(a) + 1);
      if (rng.some(x => usedflat.has(x))) rng.forEach(x => usedflat.add(x));
    }
  }
  const need = new Set(), allmon = new Set();
  for (const [t, sp] of Object.entries(MON)) { sp.forEach(x => allmon.add(x)); if (usedthings.has(+t)) sp.forEach(x => need.add(x)); }
  const drop = new Set([...allmon].filter(x => !need.has(x)));
  const ss = names.indexOf('S_START'), se = names.indexOf('S_END');
  out = out.filter((x, j) => !(ss < j && j < se && drop.has(x[0].slice(0, 4))));
  names = out.map(x => x[0]); fs = names.indexOf('F_START'); fe = names.indexOf('F_END');
  const alltex = [];
  for (const k of ['TEXTURE1', 'TEXTURE2']) if (names.includes(k)) readTex(out[names.indexOf(k)][1]).forEach(t => alltex.push(t[0]));
  for (const [a, b] of TANIMS) {
    if (alltex.includes(a) && alltex.includes(b)) {
      const rng = alltex.slice(alltex.indexOf(b), alltex.indexOf(a) + 1);
      if (rng.some(x => usedtex.has(x))) rng.forEach(x => usedtex.add(x));
    }
  }
  for (const k of ['TEXTURE1', 'TEXTURE2']) {
    if (!names.includes(k)) continue;
    const idx = names.indexOf(k);
    const keep = readTex(out[idx][1]).filter((t, j) => (k === 'TEXTURE1' && j === 0) || usedtex.has(t[0]));
    let size = 4 + 4 * keep.length; for (const [, raw] of keep) size += raw.length;
    const d = new Uint8Array(size); w32(d, 0, keep.length);
    let pos = 4 + 4 * keep.length;
    keep.forEach(([, raw], j) => { w32(d, 4 + 4 * j, pos); d.set(raw, pos); pos += raw.length; });
    out[idx] = [k, d];
  }
  out = out.filter((x, j) => !(fs < j && j < fe && !usedflat.has(x[0]) && !x[0].endsWith('_START') && !x[0].endsWith('_END')));
  const dropfull = new Set(['WIMAP0', 'WIMAP1', 'WIMAP2', 'VICTORY2', 'PFUB1', 'PFUB2']);
  for (const e of eps) { dropfull.delete('WIMAP' + (+e - 1)); if (e === '2') dropfull.delete('VICTORY2'); if (e === '3') { dropfull.delete('PFUB1'); dropfull.delete('PFUB2'); } }
  out = out.filter(x => !dropfull.has(x[0]));
  names = out.map(x => x[0]);
  const pal = out[names.indexOf('PLAYPAL')][1].subarray(0, 768);
  out.forEach((x, k) => {
    const nm = x[0];
    if (!(FULL.has(nm) || nm.startsWith('WIMAP') || (nm.startsWith('END') && nm.length === 4))) return;
    let p; try { p = patchDecode(x[1]); } catch (e) { return; }
    if (p.w !== 320 || p.h !== 200) return;
    out[k] = [nm, fullEncode(shrink(p.img, p.w, p.h, pal), PW, PH)];
  });
  return writeWad(out, 'IWAD');
}

// ------------------------------------------------- extra lumps (gbaextras.py)
function patchRead(b) {
  const w = s16(b, 0), h = s16(b, 2), lo = s16(b, 4), to = s16(b, 6);
  const px = []; for (let x = 0; x < w; x++) px.push(new Array(h).fill(null));
  for (let x = 0; x < w; x++) {
    let c = u32(b, 8 + 4 * x);
    while (b[c] !== 255) { const y0 = b[c], ln = b[c + 1]; for (let t = 0; t < ln; t++) if (y0 + t < h) px[x][y0 + t] = b[c + 3 + t]; c += ln + 4; }
  }
  return { w, h, lo, to, px };
}
function patchWrite(w, h, lo, to, px) {
  const cols = [];
  for (let x = 0; x < w; x++) {
    const col = []; let y = 0;
    while (y < h) {
      if (px[x][y] === null) { y++; continue; }
      const y0 = y, run = [];
      while (y < h && px[x][y] !== null && run.length < 128) { run.push(px[x][y]); y++; }
      col.push(y0, run.length, run[0], ...run, run[run.length - 1]);
    }
    col.push(255); cols.push(col);
  }
  let size = 8 + 4 * w; cols.forEach(c => size += c.length);
  const out = new Uint8Array(size); w16(out, 0, w); w16(out, 2, h); w16(out, 4, lo); w16(out, 6, to);
  let off = 8 + 4 * w;
  cols.forEach((c, x) => { w32(out, 8 + 4 * x, off); out.set(c, off); off += c.length; });
  return out;
}
const squeeze = (px, w, nw) => nw === w ? px : Array.from({ length: nw }, (_, x) => px[Math.min(w - 1, Math.floor((x * w + (w >> 1)) / nw))]);
function textPatch(L, txt, scale = 2) {
  const glyphs = [...txt].map(ch => ch === ' ' ? null : patchRead(L.get('STCFN' + String(ch.charCodeAt(0)).padStart(3, '0'))));
  const H = Math.max(...glyphs.filter(Boolean).map(g => g.h)) * scale, cols = [];
  const empty = () => new Array(H).fill(null);
  for (const g of glyphs) {
    if (!g) { for (let k = 0; k < 4 * scale; k++) cols.push(empty()); continue; }
    for (let x = 0; x < g.w; x++) {
      const col = empty();
      for (let y = 0; y < g.h; y++) { const v = g.px[x][y]; if (v !== null) for (let sy = 0; sy < scale; sy++) col[(H - g.h * scale) + y * scale + sy] = v; }
      for (let k = 0; k < scale; k++) cols.push(col);
    }
    for (let k = 0; k < scale; k++) cols.push(empty());
  }
  return patchWrite(cols.length, H, 0, 0, cols);
}
const SEGS = [[0, 48, 0, 34], [48, 104, 34, 78], [104, 143, 78, 107], [143, 178, 107, 135], [178, 236, 135, 177], [236, 249, 177, 187], [249, 320, 187, 240]];
function stbarGba(L) {
  const B = patchRead(L.get('STBAR')), A = patchRead(L.get('STARMS'));
  const bar = B.px, W = B.w;
  for (let x = 0; x < A.w; x++) for (let y = 0; y < A.h; y++) if (A.px[x][y] !== null && 104 + x < W) bar[104 + x][y] = A.px[x][y];
  const out = []; for (let x = 0; x < 240; x++) out.push(new Array(32).fill(0));
  for (const [s0, s1, d0, d1] of SEGS) for (let x = d0; x < d1; x++) {
    const sx = s0 + Math.floor(((x - d0) * (s1 - s0) + ((s1 - s0) >> 1)) / (d1 - d0));
    for (let y = 0; y < 32; y++) { const v = bar[Math.min(W - 1, sx)][y]; out[x][y] = v !== null && v !== undefined ? v : 0; }
  }
  const P = patchRead(L.get('STTPRCNT')), pc = squeeze(P.px, P.w, 10);
  for (const px0 of [68, 168]) for (let x = 0; x < 10; x++) for (let y = 0; y < P.h; y++) { const v = pc[x][y]; if (v !== null && px0 + x < 240) out[px0 + x][6 + y] = v; }
  const r = new Uint8Array(240 * 32); for (let y = 0; y < 32; y++) for (let x = 0; x < 240; x++) r[y * 240 + x] = out[x][y];
  return r;
}
/** Replacement for gbadoom.wad, built only from the user's own WAD. */
function gbaExtras(wad) {
  const L = new Map(readWad(wad));
  const lumps = [['CREDITS', patchWrite(1, 1, 0, 0, [[null]])], ['M_ARUN', textPatch(L, 'ALWAYS RUN')], ['M_GAMMA', textPatch(L, 'GAMMA')]];
  for (let i = 0; i < 10; i++) { const p = patchRead(L.get('STTNUM' + i)); lumps.push(['STGANUM' + i, patchWrite(10, p.h, p.lo, p.to, squeeze(p.px, p.w, 10))]); }
  for (let i = 1; i <= 5; i++) lumps.push(['PLAYPAL' + i, L.get('PLAYPAL')]);
  lumps.push(['STBARGBA', stbarGba(L)]);
  return writeWad(lumps, 'PWAD');
}

// --------------------------------------------------------- converter (WASM)
async function convert(sub, extras, wasm) {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const X = instance.exports;
  X.reset();
  const pw = X.alloc(sub.length + 8); new Uint8Array(X.memory.buffer).set(sub, pw);
  const pg = X.alloc(extras.length + 8); new Uint8Array(X.memory.buffer).set(extras, pg);
  const po = X.convert(pw, sub.length, pg, extras.length), n = X.out_len();
  if (!po || !n) throw new Error('WAD conversion failed');
  return new Uint8Array(X.memory.buffer, po, n).slice();
}

// ------------------------------------------------------------- into the ROM
/**
 * Install DOOM. rom: 16 MB Paradise image, mcu: converted WAD, emb: { app, xip, stub } (embedded),
 * E: ParamixEmu (scene table, stub). Returns { rom, free }.
 */
function doomPatch(rom, mcu, emb, E) {
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB');
  const assetEnd = 0x111000 + 16 + u32(rom, 0x111000 + 8);
  if (assetEnd > WAD_OFF) throw new Error('the character data reaches 0x' + assetEnd.toString(16) + ', DOOM needs 0x830000 onwards');
  if (CODE_OFF + emb.xip.length > SAVE_OFF) throw new Error('DOOM program too big');
  const free = REGION_OFF - WAD_OFF - 4 - mcu.length;
  if (free < 0) throw new Error('WAD too big: ' + (mcu.length >> 10) + ' KB, room ' + ((REGION_OFF - WAD_OFF - 4) >> 10) + ' KB (try without music)');
  // Program and stub as for the emulator (shared stub, A+C), but without color Tama firmware
  const out = E.installApp(rom, emb.app, emb.stub);
  out.fill(0xFF, CODE_OFF, SAVE_END);
  out.set(emb.xip, CODE_OFF);
  out.fill(0xFF, WAD_OFF, REGION_END);
  w32(out, WAD_OFF, mcu.length);
  out.set(mcu, WAD_OFF + 4);
  return { rom: out, free };
}
// ------------------------------------------------------------- buttons
/*
 * Button mapping in the program's PDMC settings block (from DOOM v0.4.0, version 2):
 * button masks A 1, B 2, C 4, wheel press 8. Fixed: A+C menu, B+C map,
 * wheel for 2 s returns to the Paradise.
 */
const KEYS_DEFAULT = { fire: 1, fwd: 2, back: 4, use: 8, mod: 4, run: true, invert: true };
function pdmcAt(rom) {
  for (let i = 0xA0000; i < 0xA0400; i += 4) if (u32(rom, i) === 0x434D4450) return i;
  return -1;
}
/** Button mapping of a ROM with DOOM, or null (no DOOM, or an old version without mapping). */
function doomKeysGet(rom) {
  const i = pdmcAt(rom);
  if (i < 0 || u32(rom, i + 4) < 2) return null;
  return { fire: u32(rom, i + 48), fwd: u32(rom, i + 52), back: u32(rom, i + 56), use: u32(rom, i + 60), mod: u32(rom, i + 64), run: !!u32(rom, i + 68), invert: !!u32(rom, i + 44) };
}
/** Check a mapping: returns an error text or ''. */
function doomKeysCheck(k) {
  if (k.fire === k.fwd || k.fire === k.back || k.fwd === k.back) return 'fire, forward and back need three different buttons';
  if (k.use !== 8 && [k.fire, k.fwd, k.back].includes(k.use)) return 'use needs its own button or the wheel';
  return '';
}
/** Write a mapping into a ROM with DOOM (new image). */
function doomKeysSet(rom, k) {
  const out = new Uint8Array(rom);
  const i = pdmcAt(out);
  if (i < 0 || u32(out, i + 4) < 2) throw new Error('this DOOM has no key layout of its own yet');
  const err = doomKeysCheck(k); if (err) throw new Error(err);
  w32(out, i + 44, k.invert ? 1 : 0);
  w32(out, i + 48, k.fire); w32(out, i + 52, k.fwd); w32(out, i + 56, k.back); w32(out, i + 60, k.use); w32(out, i + 64, k.mod); w32(out, i + 68, k.run ? 1 : 0);
  return out;
}

/** Full path from the user's own WAD to the ROM. */
async function doomBuild(rom, wad, opts, emb, E) {
  const eps = opts.episode || '1';
  const sub = wadSub(wad, eps, opts.music !== false);
  const mcu = await convert(sub, gbaExtras(sub), emb.wasm);
  const r = doomPatch(rom, mcu, emb, E);
  if (opts.keys) r.rom = doomKeysSet(r.rom, opts.keys);
  return r;
}

const DOOM_APP_B64 = '__DOOM_APP_B64__', DOOM_XIP_B64 = '__DOOM_XIP_B64__', DOOM_WASM_B64 = '__DOOM_WASM_B64__';
function embedded() {
  if (DOOM_APP_B64.startsWith('__')) return null;
  return { app: b64(DOOM_APP_B64), xip: b64(DOOM_XIP_B64), wasm: b64(DOOM_WASM_B64) };
}

root.ParamixDoom = { KEYS_DEFAULT, doomKeysGet, doomKeysSet, doomKeysCheck, readWad, writeWad, wadEpisodes, wadSub, gbaExtras, convert, doomPatch, doomBuild, embedded, WAD_OFF, REGION_OFF };
if (typeof module !== 'undefined') module.exports = root.ParamixDoom;
})(typeof window !== 'undefined' ? window : globalThis);
