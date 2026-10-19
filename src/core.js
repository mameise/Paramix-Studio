/*
 * Paramix Studio core: reads and writes the asset archive of Paradise ROM dumps.
 * Pure functions on Uint8Array, no DOM. Runs in the browser and in Node (tests).
 *
 * Format references: github.com/GMMan/tama-para-research (formats/archive.md,
 * formats/sprites.md). Everything else measured on the six editions
 * (Land, Water, Sky, Jade Forest, Tropical, Glacier).
 *
 * Author: mameise
 */
(function (root) {
'use strict';

const ASSET_BASE = 0x111000;          // ARC2 root in flash (all editions)
const ARC2_MAGIC = 0x32435241;        // "ARC2"
const EDITIONS = { 1: 'Land', 2: 'Water', 3: 'Sky', 4: 'Jade Forest', 5: 'Tropical', 6: 'Glacier' };

// ------------------------------------------------------------------ helpers
const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const s8 = (v) => (v & 0x80 ? v - 256 : v);
function w16(b, o, v) { b[o] = v & 255; b[o + 1] = (v >> 8) & 255; }
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
function concat(parts) {
  let n = 0; for (const p of parts) n += p.length;
  const out = new Uint8Array(n); let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}
function rgb565ToRgb(v) { return [((v >> 11) & 31) * 255 / 31 | 0, ((v >> 5) & 63) * 255 / 63 | 0, (v & 31) * 255 / 31 | 0]; }
function rgbTo565(r, g, b) { return (((r * 31 + 127) / 255 | 0) << 11) | (((g * 63 + 127) / 255 | 0) << 5) | ((b * 31 + 127) / 255 | 0); }

// ------------------------------------------------------------------- ARC2
function arc2Parse(buf, off) {
  if (u32(buf, off) !== ARC2_MAGIC) throw new Error('no ARC2 archive at 0x' + off.toString(16));
  const chk = u32(buf, off + 4), length = u32(buf, off + 8), n = u32(buf, off + 12);
  const ents = [];
  for (let i = 0; i < n; i++) {
    const e = off + 16 + 16 * i;
    ents.push({ flags: u32(buf, e), offset: u32(buf, e + 4), clen: u32(buf, e + 8), ulen: u32(buf, e + 12) });
  }
  return { chk, length, ents };
}
function arc2Checksum(buf, off, length) {
  let s = 0; const end = off + 16 + length;
  for (let i = off + 8; i < end; i++) s = (s + buf[i]) >>> 0;
  return s;
}
function arc2Files(buf, off) {
  return arc2Parse(buf, off).ents.map(e => buf.slice(off + e.offset, off + e.offset + e.ulen));
}
function arc2Build(files) {
  const n = files.length;
  let pos = 16 + 16 * n;
  const ents = [], body = [];
  for (const f of files) {
    const pad = (4 - pos % 4) % 4; if (pad) { body.push(new Uint8Array(pad)); pos += pad; }
    const aligned = f.length + ((4 - f.length % 4) % 4);
    ents.push([0, pos, aligned, f.length]);
    body.push(f); if (aligned > f.length) body.push(new Uint8Array(aligned - f.length));
    pos += aligned;
  }
  const out = new Uint8Array(pos);
  w32(out, 0, ARC2_MAGIC); w32(out, 8, pos - 16); w32(out, 12, n);
  ents.forEach((e, i) => e.forEach((v, k) => w32(out, 16 + 16 * i + 4 * k, v)));
  out.set(concat(body), 16 + 16 * n);
  w32(out, 4, arc2Checksum(out, 0, pos - 16));
  return out;
}

// ------------------------------------------------------------ sprite pack
function packSplit(pack) {
  const first = u32(pack, 0), n = first / 4;
  const offs = []; for (let i = 0; i < n; i++) offs.push(u32(pack, 4 * i));
  return offs.map((o, i) => pack.slice(o, i + 1 < n ? offs[i + 1] : pack.length));
}
function packJoin(entries) {
  const n = entries.length; let pos = 4 * n;
  const offs = [], parts = [];
  for (const e of entries) {
    const pad = (4 - pos % 4) % 4; if (pad) { parts.push(new Uint8Array(pad)); pos += pad; }
    offs.push(pos); parts.push(e); pos += e.length;
  }
  const head = new Uint8Array(4 * n); offs.forEach((o, i) => w32(head, 4 * i, o));
  return concat([head, ...parts]);
}

// ----------------------------------------------------------- sprite data
function spriteHeader(e) {
  return {
    dataLength: u32(e, 0), flags: e[4], bpp: e[5], numSprites: u16(e, 6), sw: e[8], sh: e[9],
    ox: s8(e[10]), oy: s8(e[11]), iw: e[12], ih: e[13], palBpp: e[14], numPal: e[15],
    transp: u16(e, 16), palOffset: u16(e, 18), pixOffset: u16(e, 20),
  };
}
const F_TRANSP = 0x04, F_RLE_BYTE = 0x20, F_RLE_WORD = 0x40, F_XOR = 0x80;
const BITS = { 0: 1, 1: 2, 2: 4, 3: 8 };

function rleDecode(d, word) {
  const out = []; let i = 0;
  if (!word) {
    while (i < d.length) {
      const c = d[i++]; if (c === 0) break;
      if (c & 0x80) { const n = c & 0x7f; for (let k = 0; k < n; k++) out.push(d[i + k]); i += n; }
      else { const v = d[i++]; for (let k = 0; k < c; k++) out.push(v); }
    }
  } else {
    while (i + 4 <= d.length) {
      const c = u32(d, i); i += 4; if (c === 0) break;
      if (c & 0x80000000) { const n = (c & 0x0fffffff) * 4; for (let k = 0; k < n; k++) out.push(d[i + k]); i += n; }
      else { for (let r = 0; r < c; r++) for (let k = 0; k < 4; k++) out.push(d[i + k]); i += 4; }
    }
  }
  return Uint8Array.from(out);
}
function rleEncode(data) {
  const out = []; let lit = []; let i = 0;
  const flush = () => { while (lit.length) { const c = lit.splice(0, 127); out.push(0x80 | c.length, ...c); } };
  while (i < data.length) {
    let j = i; while (j < data.length && data[j] === data[i] && j - i < 127) j++;
    const run = j - i;
    if (run >= 3) { flush(); out.push(run, data[i]); } else { for (let k = i; k < j; k++) lit.push(data[k]); }
    i = j;
  }
  flush(); out.push(0);
  return Uint8Array.from(out);
}

/** Decode one sprite entry. Returns header, palettes, raw index cells and
 *  composed subimages as RGBA (palette set `palSet`). */
function spriteDecode(e, palSet = 0) {
  const h = spriteHeader(e);
  const n = h.numSprites, w = h.sw, hh = h.sh, npx = w * hh;
  const iw = Math.max(1, h.iw), ih = Math.max(1, h.ih);
  let cells = [], palettes = null, direct = h.bpp >= 0x10;
  if (direct) {
    for (let s = 0; s < n; s++) {
      const c = new Uint16Array(npx);
      for (let k = 0; k < npx; k++) c[k] = u16(e, h.pixOffset + 2 * (s * npx + k));
      cells.push(c);
    }
  } else {
    const bits = BITS[h.bpp]; if (!bits) throw new Error('unknown bpp ' + h.bpp);
    const ncol = 1 << bits;
    palettes = [];
    for (let p = 0; p < Math.max(1, h.numPal); p++) {
      const pal = []; for (let k = 0; k < ncol; k++) pal.push(u16(e, h.palOffset + 2 * (p * ncol + k)));
      palettes.push(pal);
    }
    const comp = h.flags & (F_RLE_BYTE | F_RLE_WORD), enc = h.flags & F_XOR;
    const bp = h.pixOffset, raws = [];
    if (comp) {
      for (let s = 0; s < n; s++) {
        let off = u32(e, bp + 8 * s); const ln = u32(e, bp + 8 * s + 4);
        const unc = off & 0x80000000; off &= 0x7fffffff;
        let d = e.slice(bp + off, bp + off + ln);
        if (enc) d = d.map(x => x ^ 0x53);
        if (!unc) d = rleDecode(d, h.flags & F_RLE_WORD);
        raws.push(d);
      }
    } else {
      const sz = Math.ceil(npx * bits / 8);
      for (let s = 0; s < n; s++) { let d = e.slice(bp + s * sz, bp + (s + 1) * sz); if (enc) d = d.map(x => x ^ 0x53); raws.push(d); }
    }
    const mask = ncol - 1;
    for (const d of raws) {
      const c = new Uint8Array(npx);
      for (let k = 0; k < npx; k++) {
        const bit = k * bits, byte = (bit >> 3) < d.length ? d[bit >> 3] : 0;
        c[k] = (byte >> (bit & 7)) & mask;
      }
      cells.push(c);
    }
  }
  const nsub = Math.floor(n / (iw * ih));
  const W = w * iw, H = hh * ih, subs = [];
  const pal = palettes ? palettes[Math.min(palSet, palettes.length - 1)].map(rgb565ToRgb) : null;
  const transp = !!(h.flags & F_TRANSP);
  for (let si = 0; si < nsub; si++) {
    const px = new Uint8ClampedArray(W * H * 4);
    for (let gy = 0; gy < ih; gy++) for (let gx = 0; gx < iw; gx++) {
      const c = cells[si * iw * ih + gy * iw + gx];
      for (let k = 0; k < npx; k++) {
        const v = c[k], x = gx * w + (k % w), y = gy * hh + ((k / w) | 0), o = (y * W + x) * 4;
        if (transp && v === h.transp) { px[o + 3] = 0; continue; }
        const rgb = pal ? pal[v] : rgb565ToRgb(v);
        px[o] = rgb[0]; px[o + 1] = rgb[1]; px[o + 2] = rgb[2]; px[o + 3] = 255;
      }
    }
    subs.push({ w: W, h: H, data: px });
  }
  return { header: h, palettes, cells, subs, width: W, height: H };
}

// ------------------------------------------------------------ quantizing
/** Median cut with k-means refinement. pixels: array of [r,g,b]. */
function quantize(pixels, ncolors) {
  const uniq = new Map();
  for (const p of pixels) { const k = (p[0] << 16) | (p[1] << 8) | p[2]; uniq.set(k, (uniq.get(k) || 0) + 1); }
  let cols = [...uniq.entries()].map(([k, n]) => [(k >> 16) & 255, (k >> 8) & 255, k & 255, n]);
  if (cols.length <= ncolors) return cols.map(c => c.slice(0, 3));
  let boxes = [cols];
  while (boxes.length < ncolors) {
    let bi = -1, best = -1, ch = 0;
    boxes.forEach((b, i) => {
      if (b.length < 2) return;
      for (let c = 0; c < 3; c++) {
        let lo = 255, hi = 0; for (const p of b) { if (p[c] < lo) lo = p[c]; if (p[c] > hi) hi = p[c]; }
        const score = (hi - lo) * Math.log2(1 + b.reduce((s, p) => s + p[3], 0));
        if (score > best) { best = score; bi = i; ch = c; }
      }
    });
    if (bi < 0) break;
    const b = boxes[bi].sort((a, c) => a[ch] - c[ch]);
    const total = b.reduce((s, p) => s + p[3], 0); let acc = 0, cut = 1;
    for (let i = 0; i < b.length - 1; i++) { acc += b[i][3]; if (acc >= total / 2) { cut = i + 1; break; } }
    boxes.splice(bi, 1, b.slice(0, cut), b.slice(cut));
  }
  let centers = boxes.map(b => { const t = b.reduce((s, p) => s + p[3], 0); return [0, 1, 2].map(c => b.reduce((s, p) => s + p[c] * p[3], 0) / t); });
  for (let it = 0; it < 20; it++) {                      // k-means on unique colors, weighted
    const sum = centers.map(() => [0, 0, 0, 0]);
    for (const p of cols) {
      let bi = 0, bd = 1e18;
      for (let i = 0; i < centers.length; i++) { const c = centers[i]; const d = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2; if (d < bd) { bd = d; bi = i; } }
      const s = sum[bi]; s[0] += p[0] * p[3]; s[1] += p[1] * p[3]; s[2] += p[2] * p[3]; s[3] += p[3];
    }
    let moved = 0;
    centers = centers.map((c, i) => { const s = sum[i]; if (!s[3]) return c; const n = [s[0] / s[3], s[1] / s[3], s[2] / s[3]]; moved += Math.abs(n[0] - c[0]) + Math.abs(n[1] - c[1]) + Math.abs(n[2] - c[2]); return n; });
    if (moved < 0.5) break;
  }
  return centers.map(c => c.map(v => Math.round(v)));
}
function nearestIndex(pal, r, g, b, skip) {
  let bi = 0, bd = 1e18;
  for (let i = 0; i < pal.length; i++) {
    if (i === skip) continue;
    const c = pal[i]; const d = (r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2;
    if (d < bd) { bd = d; bi = i; }
  }
  return bi;
}

// -------------------------------------------------------------- encoding
/**
 * Re-encode a sprite from RGBA subimages, keeping cell size, grid, count
 * and anchor of the original entry.
 *   opts.keepPalette: map onto the original palettes (forced for sprites
 *                     with more than one palette set: colour variants)
 *   opts.allowMoreColors: palette sprites may go up to 8 bpp
 */
function spriteEncode(orig, subs, opts = {}) {
  const h = spriteHeader(orig);
  const n = h.numSprites, w = h.sw, hh = h.sh, npx = w * hh;
  const iw = Math.max(1, h.iw), ih = Math.max(1, h.ih);
  const W = w * iw, H = hh * ih;
  if (subs.length * iw * ih !== n) throw new Error('subimage count does not match');
  for (const s of subs) if (s.w !== W || s.h !== H) throw new Error('subimage size does not match');
  const cellRGBA = (si, gx, gy, k) => { const x = gx * w + (k % w), y = gy * hh + ((k / w) | 0), o = (y * W + x) * 4; return subs[si].data.subarray(o, o + 4); };
  const forEachCell = (fn) => { for (let si = 0; si < subs.length; si++) for (let gy = 0; gy < ih; gy++) for (let gx = 0; gx < iw; gx++) fn(si, gx, gy); };

  // direct colour: raw RGB565, uncompressed, same layout as original
  if (h.bpp >= 0x10) {
    const tr = h.transp, hasT = !!(h.flags & F_TRANSP);
    const out = new Uint8Array(24 + n * npx * 2);
    let s = 0;
    forEachCell((si, gx, gy) => {
      for (let k = 0; k < npx; k++) {
        const p = cellRGBA(si, gx, gy, k);
        let v = (hasT && p[3] < 128) ? tr : rgbTo565(p[0], p[1], p[2]);
        if (hasT && p[3] >= 128 && v === tr) v ^= 1;     // keep visible pixels visible
        w16(out, 24 + 2 * (s * npx + k), v);
      }
      s++;
    });
    writeHeader(out, { dataLength: out.length, flags: h.flags & ~(F_RLE_BYTE | F_RLE_WORD | F_XOR), bpp: 0x11, numSprites: n, sw: w, sh: hh, ox: h.ox, oy: h.oy, iw: h.iw, ih: h.ih, palBpp: 0x11, numPal: 0, transp: tr, palOffset: 24, pixOffset: 24 });
    return out;
  }

  let keep = opts.keepPalette || h.numPal > 1;
  let bpp = h.bpp, palBlock, transpIdx, indexOf;
  let anyTransparent = false;
  for (const s of subs) for (let i = 3; i < s.data.length; i += 4) if (s.data[i] < 128) { anyTransparent = true; break; }
  const origTransp = !!(h.flags & F_TRANSP);
  let figureMode = false;

  if (opts.figure && !keep) {
    // Character parts: if every colour already exists in the original palette,
    // keep it (the game's colour variants keep working). Otherwise use the
    // "fixed colour" layout: slots 2-7 are recoloured by the game per tama,
    // so they stay unused; near black goes to slot 1 (0x000C in every variant).
    const pal0 = spriteDecode(orig).palettes[0].map(rgb565ToRgb);
    let fits = true;
    outer: for (const s of subs) for (let i = 0; i < s.data.length; i += 4) {
      if (s.data[i + 3] < 128) continue;
      let ok = false;
      for (let k = 1; k < pal0.length; k++) { const c = pal0[k]; if (Math.abs(c[0] - s.data[i]) <= 8 && Math.abs(c[1] - s.data[i + 1]) <= 8 && Math.abs(c[2] - s.data[i + 2]) <= 8) { ok = true; break; } }
      if (!ok) { fits = false; break outer; }
    }
    if (fits && h.bpp === 2) keep = true; else figureMode = h.bpp === 2;
  }

  if (keep) {
    const dec = spriteDecode(orig);
    const pal0 = dec.palettes[0].map(rgb565ToRgb);
    transpIdx = origTransp ? h.transp : -1;
    const ncol = 1 << BITS[bpp];
    palBlock = orig.slice(h.palOffset, h.palOffset + 2 * ncol * Math.max(1, h.numPal));
    indexOf = (p) => (p[3] < 128 && transpIdx >= 0) ? transpIdx : nearestIndex(pal0, p[0], p[1], p[2], transpIdx);
  } else if (figureMode) {
    const FREE = [8, 9, 10, 11, 12, 13, 14, 15];
    const dark = (r, g, b) => r + g + b < 90;
    const opaque = [];
    for (const s of subs) for (let i = 0; i < s.data.length; i += 4) if (s.data[i + 3] >= 128 && !dark(s.data[i], s.data[i + 1], s.data[i + 2])) opaque.push([s.data[i], s.data[i + 1], s.data[i + 2]]);
    const pal = opaque.length ? quantize(opaque, FREE.length) : [];
    transpIdx = 0;
    palBlock = new Uint8Array(32);
    w16(palBlock, 0, 0x07e0); w16(palBlock, 2, 0x000c);
    pal.forEach((c, k) => w16(palBlock, 2 * FREE[k], rgbTo565(c[0], c[1], c[2])));
    const cache = new Map();
    indexOf = (p) => {
      if (p[3] < 128) return 0;
      if (dark(p[0], p[1], p[2]) || !pal.length) return 1;
      const key = (p[0] << 16) | (p[1] << 8) | p[2];
      let v = cache.get(key);
      if (v === undefined) { v = FREE[nearestIndex(pal, p[0], p[1], p[2], -1)]; cache.set(key, v); }
      return v;
    };
  } else {
    const useT = origTransp || anyTransparent;
    const opaque = [];
    for (const s of subs) for (let i = 0; i < s.data.length; i += 4) if (!useT || s.data[i + 3] >= 128) opaque.push([s.data[i], s.data[i + 1], s.data[i + 2]]);
    let maxBits = BITS[bpp];
    if (opts.allowMoreColors) maxBits = Math.max(maxBits, 8);
    const avail = (1 << maxBits) - (useT ? 1 : 0);
    let pal = opaque.length ? quantize(opaque, avail) : [[0, 0, 0]];
    let bits = BITS[bpp];
    while ((1 << bits) - (useT ? 1 : 0) < pal.length && bits < 8) bits *= 2;
    bpp = { 1: 0, 2: 1, 4: 2, 8: 3 }[bits];
    const ncol = 1 << bits;
    transpIdx = useT ? 0 : -1;
    const full = useT ? [[0, 0, 0], ...pal] : pal.slice();
    while (full.length < ncol) full.push([0, 0, 0]);
    palBlock = new Uint8Array(2 * ncol);
    full.forEach((c, i) => w16(palBlock, 2 * i, rgbTo565(c[0], c[1], c[2])));
    if (useT) w16(palBlock, 0, 0x07e0);            // like the originals: slot 0 holds the colour key
    const cache = new Map();
    const palIdx = useT ? full.slice(1, pal.length + 1) : full.slice(0, pal.length);
    indexOf = (p) => {
      if (useT && p[3] < 128) return 0;
      const key = (p[0] << 16) | (p[1] << 8) | p[2];
      let v = cache.get(key);
      if (v === undefined) { v = nearestIndex(palIdx, p[0], p[1], p[2], -1) + (useT ? 1 : 0); cache.set(key, v); }
      return v;
    };
  }

  const bits = BITS[bpp];
  const datas = [];
  forEachCell((si, gx, gy) => {
    const raw = new Uint8Array(Math.ceil(npx * bits / 8));
    for (let k = 0; k < npx; k++) {
      const v = indexOf(cellRGBA(si, gx, gy, k));
      const bit = k * bits; raw[bit >> 3] |= v << (bit & 7);
    }
    datas.push(rleEncode(raw).map(x => x ^ 0x53));
  });
  const palOff = 24, pixOff = palOff + palBlock.length, tblLen = 8 * n;
  let pos = pixOff + tblLen; const tbl = [], body = [];
  for (const d of datas) {
    const pad = (4 - pos % 4) % 4; if (pad) { body.push(new Uint8Array(pad)); pos += pad; }
    tbl.push([pos - pixOff, d.length]); body.push(d); pos += d.length;
  }
  const out = new Uint8Array(pos);
  const useT = transpIdx >= 0;
  writeHeader(out, { dataLength: pos, flags: F_XOR | F_RLE_BYTE | 0x03 | (useT ? F_TRANSP : 0), bpp, numSprites: n, sw: w, sh: hh, ox: h.ox, oy: h.oy, iw: h.iw, ih: h.ih, palBpp: 0x11, numPal: keep ? Math.max(1, h.numPal) : 1, transp: useT ? transpIdx : 0, palOffset: palOff, pixOffset: pixOff });
  out.set(palBlock, palOff);
  tbl.forEach((t, i) => { w32(out, pixOff + 8 * i, t[0]); w32(out, pixOff + 8 * i + 4, t[1]); });
  out.set(concat(body), pixOff + tblLen);
  if (opts.keepLength) {
    // Eye sprites: the game checks their byte length (a different length makes
    // a substitute character hatch). Pad with zeros to the original length.
    if (out.length > orig.length) throw new Error('eye image too detailed: ' + out.length + ' bytes, at most ' + orig.length + ' allowed');
    const padded = new Uint8Array(orig.length); padded.set(out); w32(padded, 0, orig.length);
    return padded;
  }
  return out;
}
function writeHeader(b, h) {
  w32(b, 0, h.dataLength); b[4] = h.flags; b[5] = h.bpp; w16(b, 6, h.numSprites); b[8] = h.sw; b[9] = h.sh;
  b[10] = h.ox & 255; b[11] = h.oy & 255; b[12] = h.iw; b[13] = h.ih; b[14] = h.palBpp; b[15] = h.numPal;
  w16(b, 16, h.transp); w16(b, 18, h.palOffset); w16(b, 20, h.pixOffset); w16(b, 22, 0);
}
/** Same format as `orig`, all pixels transparent. */
function spriteBlank(orig, opts = {}) {
  const d = spriteDecode(orig);
  const subs = d.subs.map(s => ({ w: s.w, h: s.h, data: new Uint8ClampedArray(s.w * s.h * 4) }));
  return spriteEncode(orig, subs, opts);
}
/** Smallest image with the same frame (count, cell size, grid, anchor): 1 bit, all cells share one
 *  run-length stream. Transparent originals stay transparent, the others become black (launcher ROM, 1.6.8). */
function spriteTiny(orig) {
  const h = spriteHeader(orig), n = h.numSprites, npx = h.sw * h.sh;
  const tr = !!(h.flags & F_TRANSP);
  const d = rleEncode(new Uint8Array(Math.ceil(npx / 8))).map(x => x ^ 0x53);
  const palOff = 24, pixOff = palOff + 4, dataOff = pixOff + 8 * n;
  const len = dataOff + d.length, pos = (len + 3) & ~3;
  const out = new Uint8Array(pos);
  writeHeader(out, { dataLength: pos, flags: F_XOR | F_RLE_BYTE | 0x03 | (tr ? F_TRANSP : 0), bpp: 0, numSprites: n, sw: h.sw, sh: h.sh, ox: h.ox, oy: h.oy, iw: h.iw, ih: h.ih, palBpp: 0x11, numPal: 1, transp: 0, palOffset: palOff, pixOffset: pixOff });
  w16(out, palOff, tr ? 0x07e0 : 0); w16(out, palOff + 2, 0);
  for (let i = 0; i < n; i++) { w32(out, pixOff + 8 * i, dataOff - pixOff); w32(out, pixOff + 8 * i + 4, d.length); }
  out.set(d, dataOff);
  return out;
}
/** Encoding options for part k (0..5) of a character group. */
function partOptions(k, extra = {}) { return Object.assign({ figure: true, keepLength: k === 1 || k === 4 }, extra); }

// ------------------------------------------------------- character groups
// One character = 6 consecutive sprites: body, eyes, mouth (large) and the same small.
const GROUP_PARTS = [
  { key: 'body', w: 64, h: 64, n: 20 }, { key: 'eyes', w: 64, h: 32, n: 20 }, { key: 'mouth', w: 64, h: 32, n: 18 },
  { key: 'body_s', w: 32, h: 32, n: 24 }, { key: 'eyes_s', w: 32, h: 16, n: 14 }, { key: 'mouth_s', w: 32, h: 16, n: 14 },
];
function findGroups(entries) {
  const out = [];
  for (let i = 0; i + 5 < entries.length; i++) {
    let ok = true;
    for (let k = 0; k < 6 && ok; k++) {
      const h = spriteHeader(entries[i + k]), p = GROUP_PARTS[k];
      ok = h.sw === p.w && h.sh === p.h && h.numSprites === p.n && h.bpp <= 3;
    }
    if (ok) out.push(i);
  }
  return out;
}

/** Egg: small (32x32, 5 frames) at i, big (64x64, 5 frames) at i+3. -1 if not found. */
// Baby, child and teen share inherited eye images, adults have their own. Anchor: the eye
// image of the first adult, identical in all six editions.
const ADULT_EYE_CRC = 2777596871;
function eyeSig(e) {
  const subs = spriteDecode(e).subs; let n = 0; for (const x of subs) n += x.data.length;
  const b = new Uint8Array(n); let o = 0; for (const x of subs) { b.set(x.data, o); o += x.data.length; }
  return crc32(b) >>> 0;
}
function findFirstAdult(entries, groups) {
  const sigs = groups.map(g => { try { return eyeSig(entries[g + 1]); } catch (e) { return -1; } });
  const i = sigs.indexOf(ADULT_EYE_CRC);
  if (i >= 0) return groups[i];
  let last = 0; sigs.forEach((x, k) => { if (k > 0 && sigs.indexOf(x) !== sigs.lastIndexOf(x)) last = k; });
  return groups[Math.min(groups.length - 1, last + 1)];
}
function findEgg(entries) {
  for (let i = 0; i + 3 < entries.length; i++) {
    const a = spriteHeader(entries[i]), b = spriteHeader(entries[i + 3]);
    if (a.sw === 32 && a.sh === 32 && a.numSprites === 5 && b.sw === 64 && b.sh === 64 && b.numSprites === 5) return i;
  }
  return -1;
}

// ------------------------------------------------------- launcher ROM (1.6.8)
/*
 * "Start device only": the Paradise keeps its setup (language, clock, birthday, continue or reset,
 * planet name), everything else becomes the smallest possible image of the same frame, so the room
 * goes to the programs. Kept: every image before the first character set (screens, buttons, frames)
 * and the pictures the setup and its intro showed in the Paradise emulator (Land), found by content in
 * every edition.
 */
const LAUNCH_KEEP_CRC = [0xe73bd469, 0x287aa401, 0xce0a2a87, 0x1d427953, 0x70c4b300, 0x5ebe8cc7, 0x5fe0481b, 0xb5b75196,
  0x53ff02fb, 0x9e3c2e47, 0x1d2e4d3e, 0x786d7183, 0x7bc20923, 0xc6e5b7b7, 0xf5e876d9, 0x3a8acbb5, 0x615b997e, 0x6c829b75,
  0xc5f0155f, 0xbff645c6, 0x4efa5d79, 0x5112672d, 0x8178c20b, 0xe56b3533, 0x0d47aefd, 0xa81e4c59, 0xfbaeefa0, 0x73b23e63];
function launcherKeep(ctx) {
  const first = ctx.groups.length ? ctx.groups[0] : 0, crcs = new Set(LAUNCH_KEEP_CRC), keep = new Set();
  for (let i = 0; i < first; i++) keep.add(i);
  ctx.entries.forEach((e, i) => { if (crcs.has(crc32(e))) keep.add(i); });
  return keep;
}
/** Changed entries for a launcher ROM: Map index -> tiny image (only where it saves room). */
function launcherChanges(ctx) {
  const keep = launcherKeep(ctx), out = new Map();
  ctx.entries.forEach((e, i) => { if (keep.has(i)) return; let t; try { t = spriteTiny(e); } catch (x) { return; } if (t.length < e.length) out.set(i, t); });
  return out;
}
/** Is this ROM already a launcher ROM (most images outside the kept set are tiny)? */
function isLauncher(ctx) {
  const keep = launcherKeep(ctx); let n = 0, tiny = 0;
  ctx.entries.forEach((e, i) => { if (keep.has(i)) return; n++; const h = spriteHeader(e); if (h.bpp === 0 && e.length <= 64 + 8 * h.numSprites) tiny++; });
  return n > 0 && tiny * 10 >= n * 9;
}

// ------------------------------------------------------------------ ROM
function openRom(rom) {
  if (rom.length < ASSET_BASE + 16 || String.fromCharCode(...rom.subarray(0, 8)) !== 'SONIXDEV') throw new Error('not a Paradise flash dump (SONIXDEV header missing)');
  const root = arc2Parse(rom, ASSET_BASE);
  if (root.ents.length !== 3) throw new Error('unexpected asset archive (' + root.ents.length + ' files)');
  const files = arc2Files(rom, ASSET_BASE);
  const entries = packSplit(files[1]);
  const end = ASSET_BASE + 16 + root.length;
  // free space ends at the first used 4 KB page after the assets (version page / user data)
  let limit = (end + 0xfff) & ~0xfff;
  const page = (p) => { for (let i = p; i < p + 0x1000 && i < rom.length; i++) if (rom[i] !== 0xff) return false; return true; };
  while (limit < rom.length && page(limit)) limit += 0x1000;
  // Edition ID: on its own page (0xD49000, Tropical/Glacier 0xD29000). Previously the first
  // used page after the characters, which went wrong with DOOM or save slots next to it.
  let editionId = 0;
  for (const p of [0xD49000, 0xD29000]) { const v = p + 4 <= rom.length ? u32(rom, p) : 0; if (v >= 1 && v <= 6) { editionId = v; break; } }
  if (!editionId) editionId = limit < rom.length ? u32(rom, limit) : 0;
  const groups = findGroups(entries);
  return { rom, root, files, entries, groups, adult: groups.length ? findFirstAdult(entries, groups) : -1, egg: findEgg(entries), assetEnd: end, limit, editionId, edition: EDITIONS[editionId] || ('Edition ' + editionId) };
}
/** New ROM with replaced sprite entries. `changed`: Map index -> Uint8Array. */
function buildRom(ctx, changed, dataPack) {
  const entries = ctx.entries.map((e, i) => changed.has(i) ? changed.get(i) : e);
  const files = [dataPack || ctx.files[0], packJoin(entries), ctx.files[2]];
  const root = arc2Build(files);
  const end = ASSET_BASE + root.length;
  if (end > ctx.limit) throw new Error('too big: assets would end at 0x' + end.toString(16) + ', free space ends at 0x' + ctx.limit.toString(16));
  const out = ctx.rom.slice();
  out.set(root, ASSET_BASE);
  if (end < ctx.assetEnd) out.fill(0xff, end, ctx.assetEnd);
  verifyRom(out, [...changed.keys()]);
  return { rom: out, assetEnd: end, free: ctx.limit - end };
}
function verifyRom(rom, indices) {
  const check = (off) => {
    const a = arc2Parse(rom, off);
    if (arc2Checksum(rom, off, a.length) !== a.chk) throw new Error('checksum wrong at 0x' + off.toString(16));
    for (const e of a.ents) if (u32(rom, off + e.offset) === ARC2_MAGIC) check(off + e.offset);
  };
  check(ASSET_BASE);
  const entries = packSplit(arc2Files(rom, ASSET_BASE)[1]);
  for (const i of indices) spriteDecode(entries[i]);
  return true;
}

// ------------------------------------------------------------- sheets
// Sheet of one sprite: subimages left to right, wrapping, no gaps.
function sheetLayout(w, h, n) {
  const cols = Math.max(1, Math.min(n, Math.floor(1280 / w) || 1));
  return { cols, rows: Math.ceil(n / cols), w, h };
}
function sheetFromSubs(subs) {
  const L = sheetLayout(subs[0].w, subs[0].h, subs.length);
  const W = L.cols * L.w, H = L.rows * L.h, px = new Uint8ClampedArray(W * H * 4);
  subs.forEach((s, i) => blit(px, W, s, (i % L.cols) * L.w, ((i / L.cols) | 0) * L.h));
  return { w: W, h: H, data: px };
}
function subsFromSheet(img, w, h, n) {
  const L = sheetLayout(w, h, n);
  if (img.w !== L.cols * w || img.h !== L.rows * h) throw new Error('sheet must be ' + L.cols * w + 'x' + L.rows * h + ' px, got ' + img.w + 'x' + img.h);
  const out = [];
  for (let i = 0; i < n; i++) out.push(crop(img, (i % L.cols) * w, ((i / L.cols) | 0) * h, w, h));
  return out;
}
// Character sheet: the 6 parts as rows, cells at fixed positions.
const GROUP_SHEET_W = 1280, GROUP_ROWS_Y = [0, 64, 96, 128, 160, 176], GROUP_SHEET_H = 192;
function groupSheet(parts) { // parts: 6 arrays of subimages
  const px = new Uint8ClampedArray(GROUP_SHEET_W * GROUP_SHEET_H * 4);
  parts.forEach((subs, k) => subs.forEach((s, i) => blit(px, GROUP_SHEET_W, s, i * GROUP_PARTS[k].w, GROUP_ROWS_Y[k])));
  return { w: GROUP_SHEET_W, h: GROUP_SHEET_H, data: px };
}
function groupFromSheet(img) {
  if (img.w !== GROUP_SHEET_W || img.h !== GROUP_SHEET_H) throw new Error('character sheet must be ' + GROUP_SHEET_W + 'x' + GROUP_SHEET_H + ' px, got ' + img.w + 'x' + img.h);
  return GROUP_PARTS.map((p, k) => { const a = []; for (let i = 0; i < p.n; i++) a.push(crop(img, i * p.w, GROUP_ROWS_Y[k], p.w, p.h)); return a; });
}
function blit(dst, DW, s, x0, y0) {
  for (let y = 0; y < s.h; y++) dst.set(s.data.subarray(y * s.w * 4, (y + 1) * s.w * 4), ((y0 + y) * DW + x0) * 4);
}
function crop(img, x0, y0, w, h) {
  const px = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) px.set(img.data.subarray(((y0 + y) * img.w + x0) * 4, ((y0 + y) * img.w + x0 + w) * 4), y * w * 4);
  return { w, h, data: px };
}
function emptyImage(w, h) { return { w, h, data: new Uint8ClampedArray(w * h * 4) }; }
function sameImage(a, b) { if (a.w !== b.w || a.h !== b.h) return false; for (let i = 0; i < a.data.length; i += 4) { if (a.data[i + 3] < 128 && b.data[i + 3] < 128) continue; for (let k = 0; k < 4; k++) if (Math.abs(a.data[i + k] - b.data[i + k]) > 4) return false; } return true; }

// --------------------------------------------------------------- character card
const DIM_DIMS = { 3528: [42, 42], 25600: [80, 160], 2560: [32, 40], 2400: [80, 15], 4800: [160, 15], 1536: [32, 24], 7168: [64, 56] };
const DIM_POSES = ['idle1', 'idle2', 'walk1', 'walk2', 'run1', 'run2', 'train1', 'train2', 'happy', 'sleep', 'attack', 'dodge'];
const DIM_BABY_POSES = ['idle1', 'idle2', 'happy', 'attack', 'sleep'];
function dimLoad(raw) {
  const d = raw.map(b => b ^ 0xff);
  const b = 0x100000;
  if (String.fromCharCode(...d.subarray(b, b + 8)) !== 'GP_SPIFI') throw new Error('no sprite package');
  const n = u32(d, b + 0x48), offs = [];
  for (let i = 0; i <= n; i++) offs.push(u32(d, b + 0x4c + 4 * i));
  const sprites = [];
  for (let i = 0; i < n; i++) {
    const dims = DIM_DIMS[offs[i + 1] - offs[i]]; if (!dims) { sprites.push(null); continue; }
    const [w, h] = dims, o = b + offs[i], px = new Uint8ClampedArray(w * h * 4);
    for (let k = 0; k < w * h; k++) {
      const v = u16(d, o + 2 * k); if (v === 0x07e0) continue;
      const c = rgb565ToRgb(v); px.set([c[0], c[1], c[2], 255], 4 * k);
    }
    sprites.push({ w, h, data: px });
  }
  const isSize = (s, w, h) => s && s.w === w && s.h === h;
  const chars = []; let i = 0;
  while (i < n) {
    const s = sprites[i];
    if (isSize(s, 80, 15) || isSize(s, 160, 15)) {
      let j = i + 1; const frames = [];
      while (j < n && (isSize(sprites[j], 32, 24) || isSize(sprites[j], 64, 56))) frames.push(sprites[j++]);
      if (j < n && isSize(sprites[j], 80, 160)) j++;
      const baby = frames.length > 0 && frames[0].w === 32;
      const poses = baby ? DIM_BABY_POSES : DIM_POSES, f = {};
      frames.forEach((im, k) => { if (poses[k]) f[poses[k]] = im; });
      if (frames.length) chars.push({ index: chars.length, name: s, baby, frames: f });
      i = j;
    } else i++;
  }
  const eggs = sprites.slice(0, 24).filter(s => isSize(s, 32, 40));
  return { chars, eggs };
}

// ------------------------------------------------------------ TamaSma cards (1.6.8)
/*
 * Character cards of the Tamagotchi Smart (1 or 2 MB). Header at 0x10 "BANDAINTPD...TAMASUMA"; at 0x1000 the list
 * of packs (u16 0x3232, u16 count, then 16 bytes each: offset at +8, size at +16, from 0x1000). Pack 0: data (20
 * tables, offsets in u16 units), pack 1: pictures (offsets of image sets, sprites, colours, pixels).
 * Image set: first sprite u16, width and height in sprites u8 u8, first colour / 4 u16. Sprite: pixel index u16,
 * x and y offset i16 (centre), properties u16 (bits 0-1 bpp 2/4/6/8, 4-5 width 8..64, 6-7 height, 15 four times).
 * Pixels MSB first, index * sprite bytes. Colours 15 bit, bit 15 = transparent.
 * Characters (table 11, 96 bytes): id, type (1 egg, 2 baby, 3 child, 4 teen, 5 adult, 6 other), frame group at +28.
 * Frame groups (table 18): 53 frames of 4 bytes (first layer, count; 0xFFFF = none). Layers (table 15): a mask of
 * which u16 follow (bit 0 x, 1 y, 2 sub-image, 4, 5, 8 unknown, 9 type, 10 image id). Image ids with bit 15 are
 * on the card (low byte = image set), others are in the toy (accessories): left out. Type 3 body, 1 face.
 */
function smaDetect(raw) { return raw.length >= 0x1100 && String.fromCharCode(...raw.subarray(0x10, 0x1A)) === 'BANDAINTPD' && String.fromCharCode(...raw.subarray(0x20, 0x28)) === 'TAMASUMA'; }
const SMA_POSES = { idle1: 1, idle2: 2, happy: 3, walk1: 22, walk2: 23, run1: 22, run2: 23, train1: 11, train2: 12, sleep: 1, attack: 11, dodge: 7 };
function smaLoad(raw) {
  if (!smaDetect(raw)) throw new Error('no TamaSma card');
  const d = raw, B = 0x1000, s16 = (o) => { const v = u16(d, o); return v > 32767 ? v - 65536 : v; };
  if (u16(d, B) !== 0x3232) throw new Error('no pack list');
  const dp = B + u32(d, B + 8), sp = B + u32(d, B + 24);
  const to = []; for (let i = 0; i < 20; i++) to.push(dp + u32(d, dp + 4 * i) * 2);
  const tab = (i) => [to[i], to[i + 1] - to[i]];
  // pictures
  const [io, so, po, xo] = [0, 4, 8, 12].map((k) => sp + u32(d, sp + k));
  const sprites = [];
  for (let o = so; o + 8 <= po; o += 8) {
    const pr = u16(d, o + 6), q = pr >> 15, bpp = [2, 4, 6, 8][pr & 3];
    const w = [8, 16, 32, 64][(pr >> 4) & 3] * (q ? 4 : 1), h = [8, 16, 32, 64][(pr >> 6) & 3] * (q ? 4 : 1);
    sprites.push({ idx: u16(d, o), ox: s16(o + 2), oy: s16(o + 4), bpp, w, h });
  }
  const sets = []; for (let o = io; o + 6 <= so; o += 6) sets.push({ fs: u16(d, o), ws: d[o + 2], hs: d[o + 3], pal: u16(d, o + 4) });
  const colour = (i) => u16(d, po + 2 * i);
  const cache = new Map();
  const subimage = (si, sub) => {
    const key = si * 4096 + sub; if (cache.has(key)) return cache.get(key);
    const st = sets[si]; if (!st) return null;
    const s0 = sprites[st.fs]; if (!s0) return null;
    const W = st.ws * s0.w, H = st.hs * s0.h, img = emptyImage(W, H), first = st.fs + sub * st.ws * st.hs;
    for (let r = 0; r < st.hs; r++) for (let c = 0; c < st.ws; c++) {
      const sp1 = sprites[first + r * st.ws + c]; if (!sp1) return null;
      const n = sp1.w * sp1.h * sp1.bpp / 8, base = xo + sp1.idx * n;
      for (let i = 0; i < sp1.w * sp1.h; i++) {
        const bit = i * sp1.bpp; let v = 0;
        for (let k = 0; k < sp1.bpp; k++) { const b = bit + k; v = (v << 1) | ((d[base + (b >> 3)] >> (7 - (b & 7))) & 1); }
        const col = colour(st.pal * 4 + v); if (col >> 15) continue;
        const o = ((r * sp1.h + ((i / sp1.w) | 0)) * W + c * sp1.w + (i % sp1.w)) * 4;
        img.data.set([((col >> 10) & 31) << 3, ((col >> 5) & 31) << 3, (col & 31) << 3, 255], o);
      }
    }
    const f = sprites[first], res = { img, ox: f.ox - (s0.w >> 1), oy: f.oy - (s0.h >> 1) };
    cache.set(key, res); return res;
  };
  // frames
  const [lo, ls] = tab(15), layers = [];
  for (let i = 0; i + 2 <= ls;) {
    const m = u16(d, lo + i), L = { x: 0, y: 0, sub: 0, type: 0, img: 0 };
    for (const [bit, key] of [[0, 'x'], [1, 'y'], [2, 'sub'], [4, ''], [5, ''], [8, ''], [9, 'type'], [10, 'img']]) {
      if (!(m & (1 << bit))) continue;
      i += 2; if (key) L[key] = key === 'x' || key === 'y' ? s16(lo + i) : u16(d, lo + i);
    }
    layers.push(L); i += 2;
  }
  const [fo, fsz] = tab(18), frames = [];
  for (let k = 0; k + 4 <= fsz; k += 4) { const li = u16(d, fo + k), n = u16(d, fo + k + 2); frames.push(li === 0xFFFF ? null : layers.slice(li, li + n)); }
  const render = (f) => {
    const can = emptyImage(128, 128);
    for (const L of f || []) {
      if (!(L.img & 0x8000) || (L.type !== 1 && L.type !== 3)) continue;
      const si = subimage(L.img & 0xFF, L.sub); if (!si) continue;
      const x0 = 64 + L.x + si.ox, y0 = 100 + L.y + si.oy;
      for (let y = 0; y < si.img.h; y++) for (let x = 0; x < si.img.w; x++) {
        const s = (y * si.img.w + x) * 4; if (!si.img.data[s + 3]) continue;
        const X = x0 + x, Y = y0 + y; if (X < 0 || Y < 0 || X >= 128 || Y >= 128) continue;
        can.data.set(si.img.data.subarray(s, s + 4), (Y * 128 + X) * 4);
      }
    }
    return can;
  };
  // characters
  const [co, cs] = tab(11), chars = [], eggs = [];
  for (let k = 0; k + 96 <= cs; k += 96) {
    const type = u16(d, co + k + 2), grp = u16(d, co + k + 28) & 0xFF, fr = frames.slice(grp * 53, grp * 53 + 53);
    if (type < 1 || type > 5 || !fr[1]) continue;
    const imgs = {}; for (const [p, i] of Object.entries(SMA_POSES)) imgs[p] = render(fr[i] || fr[1]);
    // one box around all poses, so they keep their place to each other
    let bb = [128, 128, -1, -1];
    for (const im of Object.values(imgs)) for (let y = 0; y < 128; y++) for (let x = 0; x < 128; x++) if (im.data[(y * 128 + x) * 4 + 3]) { bb[0] = Math.min(bb[0], x); bb[1] = Math.min(bb[1], y); bb[2] = Math.max(bb[2], x); bb[3] = Math.max(bb[3], y); }
    if (bb[2] < 0) continue;
    const f = {}; for (const p in imgs) f[p] = crop(imgs[p], bb[0], bb[1], bb[2] - bb[0] + 1, bb[3] - bb[1] + 1);
    if (type === 1) { eggs.push(place(f.idle1, 32, 40, 1)); continue; }
    chars.push({ index: chars.length, name: null, baby: type === 2, stage: type, frames: f });
  }
  return { chars, eggs, kind: 'sma' };
}
const BODY_POSE_MAP = ['idle1', 'idle2', 'walk1', 'happy', 'idle1', 'walk2', 'idle2', 'idle1', 'happy', 'run1', 'run2', 'attack', 'train1', 'dodge', 'sleep', 'sleep', 'train2', 'dodge', 'idle1', 'idle2'];
const SMALL_POSE_MAP = ['idle1', 'idle2', 'walk1', 'idle1', 'idle2', 'happy', 'idle1', 'idle2', 'happy', 'idle1', 'walk1', 'walk2', 'attack', 'train1', 'walk1', 'walk2', 'idle1', 'walk1', 'idle2', 'walk2', 'sleep', 'sleep', 'happy', 'happy'];
const BABY_FALLBACK = { walk1: 'idle1', walk2: 'idle2', run1: 'idle1', run2: 'idle2', train1: 'attack', train2: 'idle2', dodge: 'idle2' };
function pose(ch, name) { return ch.frames[name] || ch.frames[BABY_FALLBACK[name] || 'idle1'] || ch.frames.idle1; }
/** Scale (nearest up, box down) and place bottom-centred on a W x H canvas. */
function place(img, W, H, scale, bottom) {
  let s = img;
  if (scale !== 1) {
    const nw = Math.max(1, Math.round(img.w * scale)), nh = Math.max(1, Math.round(img.h * scale)), px = new Uint8ClampedArray(nw * nh * 4);
    for (let y = 0; y < nh; y++) for (let x = 0; x < nw; x++) {
      const x0 = Math.floor(x / scale), y0 = Math.floor(y / scale), x1 = Math.max(x0 + 1, Math.floor((x + 1) / scale)), y1 = Math.max(y0 + 1, Math.floor((y + 1) / scale));
      let r = 0, g = 0, b = 0, a = 0, cnt = 0;
      for (let yy = y0; yy < Math.min(y1, img.h); yy++) for (let xx = x0; xx < Math.min(x1, img.w); xx++) {
        const o = (yy * img.w + xx) * 4; cnt++; const al = img.data[o + 3];
        a += al; r += img.data[o] * al; g += img.data[o + 1] * al; b += img.data[o + 2] * al;
      }
      const o = (y * nw + x) * 4;
      if (cnt && a / cnt >= 110) { px[o] = r / a; px[o + 1] = g / a; px[o + 2] = b / a; px[o + 3] = 255; }
    }
    s = { w: nw, h: nh, data: px };
  }
  if (s.w > W || s.h > H) { const f = Math.min(W / s.w, H / s.h); return place(s, W, H, f, bottom); }
  const out = emptyImage(W, H);
  blit(out.data, W, s, (W - s.w) >> 1, (bottom === undefined ? H : bottom) - s.h);
  return out;
}
/** 6 parts for a character group from a card character. */
/** Egg frames for the small and big egg sprite from a card (frames 0,1,4,6,7). */
function dimEggSubs(eggs) {
  const pick = [0, 1, 4, 6, 7].map(k => eggs[Math.min(k, eggs.length - 1)]);
  return { small: pick.map(e => place(e, 32, 32, 0.625, 28)), big: pick.map(e => place(e, 64, 64, 1.25, 54)) };
}
function dimCharParts(ch, bigScale, smallScale) {
  if (bigScale === undefined) bigScale = ch.baby ? 2 : 1;
  if (smallScale === undefined) smallScale = ch.baby ? 1 : 0.5;
  const body = BODY_POSE_MAP.map(p => place(pose(ch, p), 64, 64, bigScale));
  const small = SMALL_POSE_MAP.map(p => place(pose(ch, p), 32, 32, smallScale));
  const blank = (k) => Array.from({ length: GROUP_PARTS[k].n }, () => emptyImage(GROUP_PARTS[k].w, GROUP_PARTS[k].h));
  return [body, blank(1), blank(2), small, blank(4), blank(5)];
}


// ------------------------------------------------------------------ eyes
// The game draws the eyes as their own layer over the body, and also into the
// cell (deepest zoom). For a foreign character the eye is moved out of the body
// into that layer, and the layer is placed on the character's eye through the
// composite definitions in the data pack (5 x u16: sprite, flags, x, y, subimage;
// y is the bottom edge, x the centre). Expressions: open eye, closed = a line,
// '- -' (sleep) = empty. Measured in the emulator: hatch, field, zoom, cell.
const EYE_OPEN = new Set([0, 3, 4, 5, 8, 10, 11, 12, 15, 16, 17]), EYE_EMPTY = new Set([7, 19]);
const EYE_OPEN_S = new Set([0, 3, 5, 6, 9, 10, 11, 13]), EYE_EMPTY_S = new Set();
const EYE_STEPS = [[EYE_OPEN, EYE_OPEN_S], [new Set([0, 3, 8, 12, 15, 16]), new Set([0, 5, 6, 13])], [new Set([0, 12]), new Set([0, 5, 13])], [new Set([0]), new Set([5, 13])]];
const lum = (d, o) => (d[o] * 3 + d[o + 1] * 6 + d[o + 2]) / 10;
function pix(img, x, y) { const o = (y * img.w + x) * 4; return img.data.subarray(o, o + 4); }
function skinColor(img, r) {
  const cnt = new Map();
  for (let y = r[1] - 2; y <= r[3] + 2; y++) for (let x = r[0] - 2; x <= r[2] + 2; x++) {
    if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]) continue;
    if (x < 0 || y < 0 || x >= img.w || y >= img.h) continue;
    const c = pix(img, x, y); if (c[3] < 128 || lum(c, 0) <= 70) continue;
    const k = (c[0] << 16) | (c[1] << 8) | c[2]; cnt.set(k, (cnt.get(k) || 0) + 1);
  }
  let best = 0, bk = 0; for (const [k, n] of cnt) if (n > best) { best = n; bk = k; }
  return best ? [(bk >> 16) & 255, (bk >> 8) & 255, bk & 255] : [0, 0, 0];
}
function cloneImg(img) { return { w: img.w, h: img.h, data: new Uint8ClampedArray(img.data) }; }
function inpaint(dst, src, r) {
  const skin = skinColor(src, r);
  const ok = (c) => c[3] >= 128 && lum(c, 0) > 70 && Math.abs(c[0] - skin[0]) + Math.abs(c[1] - skin[1]) + Math.abs(c[2] - skin[2]) < 120;
  for (let y = r[1]; y <= r[3]; y++) for (let x = r[0]; x <= r[2]; x++) {
    if (pix(src, x, y)[3] < 128) continue;
    let best = null;
    for (let d = 1; d < 12 && !best; d++) for (const [xx, yy] of [[r[0] - d, y], [r[2] + d, y], [x, r[1] - d], [x, r[3] + d]]) {
      if (xx < 0 || yy < 0 || xx >= src.w || yy >= src.h) continue;
      const c = pix(src, xx, yy); if (ok(c)) { best = c; break; }
    }
    const o = (y * dst.w + x) * 4; const c = best || [skin[0], skin[1], skin[2], 255];
    dst.data[o] = c[0]; dst.data[o + 1] = c[1]; dst.data[o + 2] = c[2]; dst.data[o + 3] = 255;
  }
}
function findTemplate(img, tmpl, ax, ay, radius) {
  let best = null;
  for (let dy = -radius; dy <= radius; dy++) for (let dx = -radius; dx <= radius; dx++) {
    const x0 = ax + dx, y0 = ay + dy;
    if (x0 < 0 || y0 < 0 || x0 + tmpl.w > img.w || y0 + tmpl.h > img.h) continue;
    let err = 0;
    for (let y = 0; y < tmpl.h; y++) for (let x = 0; x < tmpl.w; x++) {
      const a = pix(tmpl, x, y), b = pix(img, x0 + x, y0 + y);
      if (a[3] < 128 && b[3] < 128) continue;
      if ((a[3] < 128) !== (b[3] < 128)) { err++; continue; }
      if (Math.abs(a[0] - b[0]) + Math.abs(a[1] - b[1]) + Math.abs(a[2] - b[2]) > 60) err++;
    }
    if (!best || err < best[2]) best = [x0, y0, err];
  }
  return best;
}
function shiftImg(img, dx, dy) {
  if (!dx && !dy) return img;
  const out = emptyImage(img.w, img.h);
  for (let y = 0; y < img.h; y++) for (let x = 0; x < img.w; x++) {
    const xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= img.w || yy >= img.h) continue;
    out.data.set(pix(img, x, y), (yy * img.w + xx) * 4);
  }
  return out;
}
/** Remove the eye (rect in the idle1 frame) from every pose, return frames and the eye patches. */
function dimRemoveEye(ch, r) {
  const idle = ch.frames.idle1;
  const tmpl = crop(idle, r[0], r[1], r[2] - r[0] + 1, r[3] - r[1] + 1);
  const frames = {};
  for (const [name, im] of Object.entries(ch.frames)) {
    const hit = findTemplate(im, tmpl, r[0], r[1], 6);
    let im2 = im;
    if (hit && hit[2] <= tmpl.w * tmpl.h * 0.25) {
      im2 = cloneImg(im);
      inpaint(im2, im, [hit[0], hit[1], hit[0] + tmpl.w - 1, hit[1] + tmpl.h - 1]);
      im2 = shiftImg(im2, r[0] - hit[0], r[1] - hit[1]);
    }
    frames[name] = im2;
  }
  const skin = skinColor(idle, r);
  const open = emptyImage(idle.w, idle.h), closed = emptyImage(idle.w, idle.h);
  for (let y = r[1]; y <= r[3]; y++) for (let x = r[0]; x <= r[2]; x++) {
    const c = pix(idle, x, y);
    if (c[3] >= 128 && Math.abs(c[0] - skin[0]) + Math.abs(c[1] - skin[1]) + Math.abs(c[2] - skin[2]) > 60) open.data.set(c, (y * idle.w + x) * 4);
  }
  const mid = ((r[1] + r[3]) >> 1) + 1;
  for (let x = r[0] + 1; x < r[2]; x++) if (pix(idle, x, mid)[3] >= 128) closed.data.set([16, 16, 24, 255], (mid * idle.w + x) * 4);
  return { ch: Object.assign({}, ch, { frames }), open, closed };
}
/** Guess the eye: bright pixel next to dark ones in the upper part. Returns a rect or null. */
function dimGuessEye(ch) {
  const im = ch.frames.idle1; let bb = [im.w, im.h, -1, -1];
  for (let y = 0; y < im.h; y++) for (let x = 0; x < im.w; x++) if (pix(im, x, y)[3] >= 128) { bb[0] = Math.min(bb[0], x); bb[1] = Math.min(bb[1], y); bb[2] = Math.max(bb[2], x); bb[3] = Math.max(bb[3], y); }
  if (bb[2] < 0) return null;
  let best = null;
  for (let y = bb[1]; y < bb[1] + (bb[3] - bb[1]) * 0.6; y++) for (let x = bb[0]; x <= bb[2]; x++) {
    const c = pix(im, x, y); if (c[3] < 128 || lum(c, 0) < 200) continue;
    let dark = 0;
    for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) { const xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= im.w || yy >= im.h) continue; const q = pix(im, xx, yy); if (q[3] >= 128 && lum(q, 0) < 60) dark++; }
    const score = dark * 2 - (y - bb[1]) * 0.2;
    if (dark >= 2 && (!best || score > best[0])) best = [score, x, y];
  }
  return best ? eyeRectAt(ch, best[1], best[2]) : null;
}
/** Rect around the eye at (x, y): all non-skin pixels within 5 px. */
function eyeRectAt(ch, x, y) {
  const im = ch.frames.idle1; const r0 = [x - 1, y - 1, x + 1, y + 1]; const skin = skinColor(im, [x - 4, y - 4, x + 4, y + 4]);
  let r = [x, y, x, y];
  for (let yy = Math.max(0, y - 5); yy <= Math.min(im.h - 1, y + 5); yy++) for (let xx = Math.max(0, x - 5); xx <= Math.min(im.w - 1, x + 5); xx++) {
    const c = pix(im, xx, yy); if (c[3] < 128) continue;
    if (Math.abs(c[0] - skin[0]) + Math.abs(c[1] - skin[1]) + Math.abs(c[2] - skin[2]) > 60) { r[0] = Math.min(r[0], xx); r[1] = Math.min(r[1], yy); r[2] = Math.max(r[2], xx); r[3] = Math.max(r[3], yy); }
  }
  void r0;
  return r;
}
/** Character parts with eye layer. Returns {parts, eyeBig:[dx,top], eyeSmall:[dx,top]} or null. */
function dimCharPartsWithEyes(ch, rect, bigScale, smallScale) {
  if (bigScale === undefined) bigScale = ch.baby ? 2 : 1;
  if (smallScale === undefined) smallScale = ch.baby ? 1 : 0.5;
  const rm = dimRemoveEye(ch, rect);
  const mask = emptyImage(ch.frames.idle1.w, ch.frames.idle1.h);
  for (let y = rect[1]; y <= rect[3]; y++) for (let x = rect[0]; x <= rect[2]; x++) mask.data.set([255, 255, 255, 255], (y * mask.w + x) * 4);
  const one = (W, H, eyeH, scale, poses, n, openSet, emptySet) => {
    const m = place(mask, W, H, scale); let bb = [W, H, -1, -1];
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (m.data[(y * W + x) * 4 + 3]) { bb[0] = Math.min(bb[0], x); bb[1] = Math.min(bb[1], y); bb[2] = Math.max(bb[2], x); bb[3] = Math.max(bb[3], y); }
    if (bb[2] < 0) return null;
    const top = Math.max(0, Math.min(H - eyeH, bb[1] - (eyeH >> 2)));
    let dx = ((bb[0] + bb[2] + 1) >> 1) - (W >> 1);
    if ((bb[0] - dx) & 1) dx += 1;   // even start column packs better (4 bpp)
    const body = poses.map(p => place(pose(rm.ch, p), W, H, scale));
    const layer = (patch) => crop(shiftImg(place(patch, W, H, scale), -dx, 0), 0, top, W, eyeH);
    const lo = layer(rm.open), lc = layer(rm.closed), em = emptyImage(W, eyeH);
    const eyes = Array.from({ length: n }, (_, j) => emptySet.has(j) ? em : (openSet.has(j) ? lo : lc));
    return { body, eyes, lo, lc, em, off: [dx, top] };
  };
  const big = one(64, 64, 32, bigScale, BODY_POSE_MAP, 20, EYE_OPEN, EYE_EMPTY);
  const small = one(32, 32, 16, smallScale, SMALL_POSE_MAP, 14, EYE_OPEN_S, EYE_EMPTY_S);
  if (!big || !small) return null;
  const blank = (k) => Array.from({ length: GROUP_PARTS[k].n }, () => emptyImage(GROUP_PARTS[k].w, GROUP_PARTS[k].h));
  return { parts: [big.body, big.eyes, blank(2), small.body, small.eyes, blank(5)], eyeBig: big.off, eyeSmall: small.off, big, small };
}
/** Fewer open expressions until the eye sprite fits its original byte length. */
function eyeVariants(layer, isBig) {
  const n = isBig ? 20 : 14, empty = isBig ? EYE_EMPTY : EYE_EMPTY_S;
  return EYE_STEPS.map(st => Array.from({ length: n }, (_, j) => empty.has(j) ? layer.em : (st[isBig ? 0 : 1].has(j) ? layer.lo : layer.lc)));
}
const s16v = (v) => (v > 32767 ? v - 65536 : v);
/* 1.6.8 (EYE_V2): the layer table of the data pack read the way the game reads it. A layer is a mask word, then one
 * word per set bit (0 x, 1 y, 2 sub-image, 3..8 other, 9 type, 10 picture), the picture comes last. x and y are the
 * centre of the picture. Frames (table 18) point into it. The old scan took the word after the picture as the start
 * of its fields, which are those of the next layer (eyes moved the mouth). EYE_V2 = false brings the old way back. */
const EYE_V2 = true;
function paraLayers(dp) {
  try {
    const u = (o) => dp[o] | (dp[o + 1] << 8), U = (o) => (u(o) | (u(o + 2) << 16)) >>> 0;
    const to = []; for (let i = 0; i < 20; i++) to.push(U(4 * i) * 2);
    const lo = to[15], le = to[16], fo = to[18], fe = to[19];
    if (!(lo < le && le <= dp.length && fo < fe && fe <= dp.length)) return null;
    const layers = [];
    for (let i = lo; i + 2 <= le;) {
      const m = u(i); if (m & 0xF800) return null;
      const L = { m, x: 0, y: 0, sub: 0, type: 0, img: -1, ox: -1, oy: -1 };
      for (let b = 0; b <= 10; b++) {
        if (!(m & (1 << b))) continue; i += 2;
        if (b === 0) { L.x = s16v(u(i)); L.ox = i; } else if (b === 1) { L.y = s16v(u(i)); L.oy = i; }
        else if (b === 2) L.sub = u(i); else if (b === 9) L.type = u(i); else if (b === 10) L.img = u(i);
      }
      layers.push(L); i += 2;
    }
    const frames = [];
    for (let k = fo; k + 4 <= fe; k += 4) { const li = u(k), n = u(k + 2); if (li !== 0xFFFF && li + n <= layers.length) frames.push(layers.slice(li, li + n)); }
    return { layers, frames };
  } catch (e) { return null; }
}
/* body and eye layer of every frame showing both (each eye layer once); null when the table cannot be read */
function eyePairs(dp, bodyId, eyeId) {
  if (!EYE_V2) return null;
  const P = paraLayers(dp); if (!P) return null;
  const out = [], seen = new Set();
  for (const f of P.frames) {
    const b = f.find((l) => l.img === bodyId), e = f.find((l) => l.img === eyeId);
    if (!b || !e || seen.has(e) || e.ox < 0 || e.oy < 0) continue;
    seen.add(e); out.push({ b, e });
  }
  return out;
}
function eyeMove(dataPack, bodyId, eyeId, fn) {
  const pr = eyePairs(dataPack, bodyId, eyeId); if (!pr) return null;
  const d = dataPack.slice();
  for (const { b, e } of pr) { const [x, y] = fn(b, e); w16(d, e.ox, x & 0xffff); w16(d, e.oy, y & 0xffff); }
  return { dataPack: d, count: pr.length };
}
/** Hide a character's eye layer on the field: move every eye entry far off screen.
 *  The eye sprite itself stays untouched, so the cell keeps the original eyes with all moods. */
function hideEyes(dataPack, bodyId, eyeId, far = 2000) {
  const v2 = eyeMove(dataPack, bodyId, eyeId, (b) => [b.x + far, b.y - far]); if (v2) return v2;
  const d = dataPack.slice(); const n = d.length >> 1; let count = 0;
  const w = (i) => d[2 * i] | (d[2 * i + 1] << 8);
  for (let i = 0; i < n - 12; i++) {
    if (w(i) !== bodyId || w(i + 1) < 1000 || w(i + 1) > 1600 || w(i + 4) > 40) continue;
    for (let j = i + 5; j < i + 12; j++) if (w(j) === eyeId && w(j + 1) >= 1000 && w(j + 1) <= 1600) {
      const bx = s16v(w(i + 2)), by = s16v(w(i + 3));
      w16(d, 2 * (j + 2), (bx + far) & 0xffff); w16(d, 2 * (j + 3), (by - far) & 0xffff);
      count++; break;
    }
  }
  return { dataPack: d, count };
}
/** Place the eye layer of a character at (dx, top) relative to its body in every composite definition. */
function setEyeOffset(dataPack, bodyId, eyeId, hBody, hEye, dx, top) {
  /* EYE_V2: same width; centres: eye top = body top + top */
  const v2 = eyeMove(dataPack, bodyId, eyeId, (b) => [b.x + dx, b.y - (hBody >> 1) + top + (hEye >> 1)]); if (v2) return v2;
  const d = dataPack.slice(); const n = d.length >> 1; let count = 0;
  const w = (i) => d[2 * i] | (d[2 * i + 1] << 8);
  for (let i = 0; i < n - 12; i++) {
    if (w(i) !== bodyId || w(i + 1) < 1000 || w(i + 1) > 1600 || w(i + 4) > 40) continue;
    for (let j = i + 5; j < i + 12; j++) if (w(j) === eyeId && w(j + 1) >= 1000 && w(j + 1) <= 1600) {
      const bx = s16v(w(i + 2)), by = s16v(w(i + 3));
      w16(d, 2 * (j + 2), (bx + dx) & 0xffff); w16(d, 2 * (j + 3), (by + top - (hBody - hEye)) & 0xffff);
      count++; break;
    }
  }
  return { dataPack: d, count };
}
/* 1.6.8: where a character's eye layer sits relative to its body, as most of its composite entries have it ([x, y]
 * of the top left corners, y counted from the top of the body), or null. */
/* The first composite entry of a character: sub-images of body and eyes and the eye layer's offset (for a preview) */
function eyeFirst(dataPack, bodyId, eyeId) {
  const pr = eyePairs(dataPack, bodyId, eyeId);
  if (pr) {   /* dx, dy: top left of the eyes inside the body picture (same width; height from the caller) */
    if (!pr.length) return null;
    const { b, e } = pr.find((p) => p.e.sub === 0) || pr[0];
    return { bs: b.sub, es: e.sub, dx: e.x - b.x, dy: e.y - b.y, centred: true };
  }
  const d = dataPack, n = d.length >> 1, w = (i) => d[2 * i] | (d[2 * i + 1] << 8);
  for (let i = 0; i < n - 12; i++) {
    if (w(i) !== bodyId || w(i + 1) < 1000 || w(i + 1) > 1600 || w(i + 4) > 40) continue;
    for (let j = i + 5; j < i + 12; j++) if (w(j) === eyeId && w(j + 1) >= 1000 && w(j + 1) <= 1600) return { bs: w(i + 4), es: w(j + 4), dx: s16v(w(j + 2)) - s16v(w(i + 2)), dy: s16v(w(j + 3)) - s16v(w(i + 3)) };
  }
  return null;
}
/* 1.6.8: one frame of a character for the preview: body sub-image, eyes and mouth (sub-image and centre offset to
 * the body). Prefers a frame with the eyes open (sub-image 0). */
function faceFirst(dataPack, bodyId, eyeId, mouthId) {
  const P = EYE_V2 ? paraLayers(dataPack) : null; if (!P) return null;
  let best = null;
  for (const f of P.frames) {
    const b = f.find((l) => l.img === bodyId), e = f.find((l) => l.img === eyeId); if (!b || !e) continue;
    const m = f.find((l) => l.img === mouthId), r = { bs: b.sub, eye: { sub: e.sub, dx: e.x - b.x, dy: e.y - b.y }, mouth: m ? { sub: m.sub, dx: m.x - b.x, dy: m.y - b.y } : null };
    if (!best) best = r;
    if (e.sub === 0 && m && m.sub === 0) return r;
    if (e.sub === 0 && m && !best.mouth) best = r;
  }
  return best;
}
/* 1.6.8: every pose of a character (frames with body and eyes; each pair of eye and mouth layers once): body
 * sub-image, eyes and mouth with sub-image, centre offset to the body and where their x and y words are (to move
 * one pose alone). */
function facePoses(dataPack, bodyId, eyeId, mouthId) {
  const P = EYE_V2 ? paraLayers(dataPack) : null; if (!P) return [];
  const out = [], seen = new Set();
  const part = (l, b) => (l && l.ox >= 0 && l.oy >= 0 ? { sub: l.sub, dx: l.x - b.x, dy: l.y - b.y, ox: l.ox, oy: l.oy } : null);
  for (const f of P.frames) {
    const b = f.find((l) => l.img === bodyId), e = f.find((l) => l.img === eyeId); if (!b || !e) continue;
    const m = f.find((l) => l.img === mouthId), key = e.ox + ':' + (m ? m.ox : -1);
    if (seen.has(key)) continue; seen.add(key);
    out.push({ bs: b.sub, eye: part(e, b), mouth: part(m, b) });
  }
  return out;
}
/* move single layers: list of { ox, oy, d: [dx, dy] } (word positions from facePoses) */
function layersMove(dataPack, list) {
  const d = dataPack.slice(), r = (o) => s16v(d[o] | (d[o + 1] << 8));
  for (const { ox, oy, d: v } of list) { if (ox < 0 || oy < 0) continue; w16(d, ox, (r(ox) + v[0]) & 0xffff); w16(d, oy, (r(oy) + v[1]) & 0xffff); }
  return d;
}
function eyeRelGet(dataPack, bodyId, eyeId, hBody, hEye) {
  const pr = eyePairs(dataPack, bodyId, eyeId);
  if (pr) {
    const seen = new Map();
    for (const { b, e } of pr) { const k = (e.x - b.x) + ',' + (e.y - b.y + ((hBody - hEye) >> 1)); seen.set(k, (seen.get(k) || 0) + 1); }
    let best = null, bn = 0; for (const [k, c] of seen) if (c > bn) { bn = c; best = k; }
    return best ? best.split(',').map(Number) : null;
  }
  const d = dataPack, n = d.length >> 1, w = (i) => d[2 * i] | (d[2 * i + 1] << 8), seen = new Map();
  for (let i = 0; i < n - 12; i++) {
    if (w(i) !== bodyId || w(i + 1) < 1000 || w(i + 1) > 1600 || w(i + 4) > 40) continue;
    for (let j = i + 5; j < i + 12; j++) if (w(j) === eyeId && w(j + 1) >= 1000 && w(j + 1) <= 1600) {
      const k = (s16v(w(j + 2)) - s16v(w(i + 2))) + ',' + (s16v(w(j + 3)) - s16v(w(i + 3)) + hBody - hEye); seen.set(k, (seen.get(k) || 0) + 1); break;
    }
  }
  let best = null, bn = 0; for (const [k, c] of seen) if (c > bn) { bn = c; best = k; }
  return best ? best.split(',').map(Number) : null;
}
/* 1.6.8: move a character's eye layer by (dx, dy) in every composite entry (the animation stays as it is) */
function shiftEyes(dataPack, bodyId, eyeId, dx, dy) {
  const v2 = eyeMove(dataPack, bodyId, eyeId, (b, e) => [e.x + dx, e.y + dy]); if (v2) return v2;
  const d = dataPack.slice(); const n = d.length >> 1; let count = 0;
  const w = (i) => d[2 * i] | (d[2 * i + 1] << 8);
  for (let i = 0; i < n - 12; i++) {
    if (w(i) !== bodyId || w(i + 1) < 1000 || w(i + 1) > 1600 || w(i + 4) > 40) continue;
    for (let j = i + 5; j < i + 12; j++) if (w(j) === eyeId && w(j + 1) >= 1000 && w(j + 1) <= 1600) {
      w16(d, 2 * (j + 2), (s16v(w(j + 2)) + dx) & 0xffff); w16(d, 2 * (j + 3), (s16v(w(j + 3)) + dy) & 0xffff);
      count++; break;
    }
  }
  return { dataPack: d, count };
}
// Known eye positions (idle frame) for the first supported card, keyed by a pixel checksum of idle1.
const DIM_EYE_PRESETS = { '108888711': [14, 19, 18, 23], '116452384': [28, 8, 33, 13], '581122605': [31, 7, 36, 12], '1245232413': [19, 20, 34, 25], '1336972138': [25, 36, 36, 42], '1500027408': [27, 10, 34, 16], '1848189483': [17, 23, 24, 30], '2311689890': [26, 18, 32, 24], '2355390565': [29, 25, 36, 31], '2531248920': [4, 12, 17, 16], '2626563298': [4, 11, 22, 19], '2954084235': [26, 31, 31, 36], '3228635844': [25, 12, 34, 17], '3828096515': [25, 10, 30, 16], '3939049875': [22, 16, 27, 22] };
// Characters on that card without visible eyes (null = no eye layer).
Object.assign(DIM_EYE_PRESETS, { '1999209136': null, '26973819': null });
function dimEyePreset(ch) { const k = String(crc32(ch.frames.idle1.data)); return k in DIM_EYE_PRESETS ? DIM_EYE_PRESETS[k] : undefined; }

// ------------------------------------------------------------ number test
const DIGITS = ['111101101101111', '010110010010111', '111001111100111', '111001111001111', '101101111001001', '111100111001111', '111100111101111', '111001001001001', '111101111101111', '111101111001111'];
function numberImage(text, w, h, scale) {
  const img = emptyImage(w, h);
  const set = (x, y, r, g, b) => { if (x < 0 || y < 0 || x >= w || y >= h) return; const o = (y * w + x) * 4; img.data.set([r, g, b, 255], o); };
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const edge = x === 0 || y === 0 || x === w - 1 || y === h - 1; set(x, y, edge ? 230 : 255, edge ? 30 : 255, edge ? 30 : 255); }
  const tw = text.length * 4 * scale - scale, x0 = (w - tw) >> 1, y0 = (h - 5 * scale) >> 1;
  [...text].forEach((ch, i) => { const g = DIGITS[+ch]; for (let r = 0; r < 5; r++) for (let c = 0; c < 3; c++) if (g[r * 3 + c] === '1') for (let yy = 0; yy < scale; yy++) for (let xx = 0; xx < scale; xx++) set(x0 + (i * 4 + c) * scale + xx, y0 + r * scale + yy, 0, 0, 0); });
  return img;
}

// ------------------------------------------------------------------- ZIP
const CRC_TABLE = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(d) { let c = 0xffffffff; for (let i = 0; i < d.length; i++) c = CRC_TABLE[(c ^ d[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
/** Minimal ZIP writer (stored). files: [{name, data: Uint8Array}] */
function zipBuild(files) {
  const enc = new TextEncoder(), local = [], central = []; let off = 0;
  for (const f of files) {
    const name = enc.encode(f.name), crc = crc32(f.data);
    const lh = new Uint8Array(30 + name.length);
    w32(lh, 0, 0x04034b50); w16(lh, 4, 20); w16(lh, 8, 0); w32(lh, 14, crc); w32(lh, 18, f.data.length); w32(lh, 22, f.data.length); w16(lh, 26, name.length); lh.set(name, 30);
    const ch = new Uint8Array(46 + name.length);
    w32(ch, 0, 0x02014b50); w16(ch, 4, 20); w16(ch, 6, 20); w32(ch, 16, crc); w32(ch, 20, f.data.length); w32(ch, 24, f.data.length); w16(ch, 28, name.length); w32(ch, 42, off); ch.set(name, 46);
    local.push(lh, f.data); central.push(ch); off += lh.length + f.data.length;
  }
  const cd = concat(central), end = new Uint8Array(22);
  w32(end, 0, 0x06054b50); w16(end, 8, files.length); w16(end, 10, files.length); w32(end, 12, cd.length); w32(end, 16, off);
  return concat([...local, cd, end]);
}
/** ZIP reader. inflateRaw: async (Uint8Array) => Uint8Array for deflated entries. */
async function zipRead(buf, inflateRaw) {
  let eocd = -1; for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) if (u32(buf, i) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error('not a ZIP file');
  const n = u16(buf, eocd + 10); let p = u32(buf, eocd + 16); const out = []; const dec = new TextDecoder();
  for (let k = 0; k < n; k++) {
    const method = u16(buf, p + 10), csize = u32(buf, p + 20), nlen = u16(buf, p + 28), xlen = u16(buf, p + 30), clen = u16(buf, p + 32), lo = u32(buf, p + 42);
    const name = dec.decode(buf.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + xlen + clen;
    if (name.endsWith('/')) continue;
    const start = lo + 30 + u16(buf, lo + 26) + u16(buf, lo + 28), raw = buf.subarray(start, start + csize);
    if (method === 0) out.push({ name, data: raw.slice() });
    else if (method === 8) out.push({ name, data: await inflateRaw(raw) });
    else throw new Error('unsupported ZIP compression in ' + name);
  }
  return out;
}


// ------------------------------------------------------------- fields (worlds)
/*
 * What belongs to which field. Every edition carries complete sets of characters
 * for several field types (1 Land, 2 Water, 3 Sky, 4 Forest, 5 Tropical, 6 Glacier):
 * Land/Water/Sky ROMs types 1-3, Jade Forest 1-4, Tropical/Glacier all six.
 * Per field: 1 child, 4 teens, 17 adults. Characters come from the character table
 * of the data pack (30 byte records: id, ?, name index, stage, line, ..., field at
 * word 7); the sprite groups follow the character ids in order (group 0 = fallback).
 * Field maps are 7 sprites of 64x64x64 per field (Tropical/Glacier: plus 7 overlay
 * sprites of 64x64x32), close-ups are 6 sprites of 64x64x4 per field type.
 */
const FIELD_NAMES = { 1: 'Land', 2: 'Water', 3: 'Sky', 4: 'Forest', 5: 'Tropical', 6: 'Glacier' };
function charTable(dp) {
  const u32 = (o) => (dp[o] | (dp[o + 1] << 8) | (dp[o + 2] << 16) | (dp[o + 3] << 24)) >>> 0;
  const u16 = (o) => dp[o] | (dp[o + 1] << 8);
  const n = u32(0);
  for (let t = 0; t < Math.min(n, 40); t++) {
    const a = u32(4 + 4 * t) * 2, b = u32(8 + 4 * t) * 2;
    if (b > a && b <= dp.length && (b - a) % 30 === 0 && b - a >= 60 && u16(a) === 0xFFFF && u16(a + 30) === 1000) {
      const out = [];
      for (let o = a; o < b; o += 30) out.push({ id: u16(o), name: u16(o + 4) + 1, stage: u16(o + 6), line: u16(o + 8), field: u16(o + 14) });
      return out;
    }
  }
  return [];
}
function fieldSets(ctx) {
  const chars = charTable(ctx.files[0]).filter((c) => c.id > 1000 && c.id < 9000);
  if (chars.length + 1 !== ctx.groups.length) return [];
  chars.forEach((c, i) => { c.group = ctx.groups[i + 1]; });
  const types = [...new Set(chars.map((c) => c.field).filter((f) => f >= 1 && f <= 6))].sort((a, b) => a - b);
  const kind = (i) => { const h = spriteHeader(ctx.entries[i]); return h.sw === 64 && h.sh === 64 ? Math.max(1, h.iw) * Math.max(1, h.ih) : 0; };
  const maps = [], over = [];
  ctx.entries.forEach((e, i) => { const k = kind(i); if (k === 64) maps.push(i); });
  // close-ups: the longest run of 64x64x4 sprites, 6 per field type
  let best = [0, 0];
  for (let i = 0; i < ctx.entries.length; i++) { if (kind(i) !== 4) continue; let j = i; while (j + 1 < ctx.entries.length && kind(j + 1) === 4) j++; if (j - i + 1 > best[1] - best[0]) best = [i, j + 1]; i = j; }
  const nClose = Math.floor((best[1] - best[0]) / 6);
  const nMap = Math.floor(maps.length / 7);
  // maps belong to the highest field types present (Tropical/Glacier: Forest, Tropical, Glacier)
  const mapTypes = types.slice(Math.max(0, types.length - nMap));
  return types.map((tp) => {
    const m = mapTypes.indexOf(tp), maps7 = m >= 0 ? maps.slice(7 * m, 7 * m + 7) : [];
    const overlays = [];
    if (maps7.length) { const last = maps7[6]; for (let j = last + 1; j < last + 8 && j < ctx.entries.length && kind(j) === 32; j++) overlays.push(j); }
    const close = tp <= nClose ? Array.from({ length: 6 }, (_, k) => best[0] + 6 * (tp - 1) + k) : [];
    return { type: tp, name: FIELD_NAMES[tp], chars: chars.filter((c) => c.field === tp), maps: maps7, overlays, closeups: close };
  });
}


// ------------------------------------------------------------ texts (1.6.8)
/*
 * The third file of the asset archive is an ARC2 with one text table per language (JP, EN, FR, DE, PT, ES, IT and
 * two Chinese ones). A table: u16 offsets (in u16 units from the table start; the first one is the number of
 * entries), then the strings, u16 glyph codes ending in 0. Names (Tamas, items, fields) are entries of it. A changed
 * name goes into every table with Latin letters; the strings are laid out anew, so a name may be longer.
 */
const TXT_ENC = { ' ': 305, '&': 375, "'": 376, '.': 381, '!': 370, '?': 388, '/': 382 };
for (let i = 0; i < 26; i++) TXT_ENC[String.fromCharCode(65 + i)] = 318 + i;
for (let i = 0; i < 10; i++) TXT_ENC[String(i)] = 306 + i;
const TXT_DEC = {}; for (const k in TXT_ENC) TXT_DEC[TXT_ENC[k]] = k; TXT_DEC[317] = ' ';
/** Clean a name for the Paradise font: capitals, digits, space and . & ' ! ? / ; null if nothing is left. */
function textClean(s) { const t = String(s || '').toUpperCase().split('').filter((c) => c in TXT_ENC).join('').replace(/\s+/g, ' ').trim(); return t || null; }
function textTablesOf(rom) {
  const root = arc2Parse(rom, ASSET_BASE), off = ASSET_BASE + root.ents[2].offset;
  if (u32(rom, off) !== ARC2_MAGIC) return null;
  const tabs = arc2Files(rom, off).map((b) => {
    const w = (o) => b[o] | (b[o + 1] << 8), n = w(0), offs = [];
    for (let i = 0; i < n; i++) offs.push(w(2 * i));
    return { b, n, offs };
  });
  return { off, tabs };
}
function txtWords(t, i) { const w = (o) => t.b[o] | (t.b[o + 1] << 8), out = []; for (let o = 2 * t.offs[i]; o + 1 < t.b.length; o += 2) { const c = w(o); if (!c) break; out.push(c); } return out; }
/* Latin table: its words for LAND/WATER/SKY-like entries are capitals or accented letters */
function txtLatin(t) { if (t.n < 20) return false; const ws = txtWords(t, 1); return ws.length > 2 && ws.every((c) => (c >= 305 && c <= 470) || c === 61440); }
/** English text i (Paradise font), '' if it has other glyphs */
function textGet(rom, i) {
  const x = textTablesOf(rom); if (!x) return '';
  const t = x.tabs.find(txtLatin); if (!t || i >= t.n) return '';
  let s = ''; for (const c of txtWords(t, i)) { if (TXT_DEC[c] === undefined) return ''; s += TXT_DEC[c]; } return s.trim();
}
/** ROM with changed texts. edits: Map text index -> name (cleaned). Throws if the archive would not fit. */
function textApply(rom, edits) {
  if (!edits || !edits.size) return rom;
  const x = textTablesOf(rom); if (!x) throw new Error('text tables not found');
  const tabs = x.tabs.map((t) => {
    if (!txtLatin(t)) return t.b;
    const end = t.b.length >> 1, segs = [];
    for (let i = 0; i < t.n; i++) {
      const a = t.offs[i], b = i + 1 < t.n ? t.offs[i + 1] : end;
      let seg = t.b.subarray(2 * a, 2 * b);
      const nm = edits.has(i) ? textClean(edits.get(i)) : null;
      // only an entry that is exactly its string plus the 0 is replaced (the first and the last are not)
      if (nm && i > 0 && i + 1 < t.n && b === a + txtWords(t, i).length + 1) {
        const ns = new Uint8Array(2 * (nm.length + 1)); [...nm].forEach((c, k) => w16(ns, 2 * k, TXT_ENC[c])); seg = ns;
      }
      segs.push(seg);
    }
    let pos = t.n; const head = new Uint8Array(2 * t.n);
    segs.forEach((sg, i) => { if (pos > 0xFFFF) throw new Error('text table too long'); w16(head, 2 * i, pos); pos += sg.length >> 1; });
    const out = concat([head, ...segs]);
    return (t.b.length & 1) ? concat([out, new Uint8Array(1)]) : out;
  });
  const files = arc2Files(rom, ASSET_BASE); files[2] = arc2Build(tabs);
  const ctx = openRom(rom), root = arc2Build(files), end = ASSET_BASE + root.length;
  if (end > ctx.limit) throw new Error('too big: assets would end at 0x' + end.toString(16));
  const out = rom.slice(); out.set(root, ASSET_BASE);
  if (end < ctx.assetEnd) out.fill(0xff, end, ctx.assetEnd);
  verifyRom(out, []);
  return out;
}

root.ParamixCore = { paraLayers, faceFirst, facePoses, layersMove, EYE_V2, smaDetect, smaLoad, eyeFirst, eyeRelGet, shiftEyes, textClean, textGet, textApply, textTablesOf, TXT_ENC,
  fieldSets, charTable, FIELD_NAMES,
  ASSET_BASE, EDITIONS, GROUP_PARTS, GROUP_SHEET_W, GROUP_SHEET_H,
  arc2Parse, arc2Files, arc2Build, arc2Checksum, packSplit, packJoin, spriteHeader, spriteDecode, spriteEncode, spriteBlank, spriteTiny, partOptions, launcherKeep, launcherChanges, isLauncher,
  rleEncode, rleDecode, quantize, findGroups, findEgg, openRom, buildRom, verifyRom,
  sheetLayout, sheetFromSubs, subsFromSheet, groupSheet, groupFromSheet, emptyImage, sameImage, crop,
  dimLoad, dimCharParts, dimEggSubs, dimCharPartsWithEyes, dimGuessEye, eyeRectAt, eyeVariants, setEyeOffset, hideEyes, dimEyePreset, DIM_EYE_PRESETS, place, numberImage, zipBuild, zipRead, crc32,
};
if (typeof module !== 'undefined') module.exports = root.ParamixCore;
})(typeof window !== 'undefined' ? window : globalThis);
