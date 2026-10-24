// 1.6.10 test1: the Wolf3D pack (src/wolf.js) from made-up files (no game data): pages, maps, sounds, pictures
// (Huffman, planes), the version by the number of chunks, missing files. node test/wolf_test.js
const W = require('../src/wolf.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const u16 = (b, o) => b[o] | (b[o + 1] << 8);
function w16(b, o, v) { b[o] = v & 255; b[o + 1] = (v >> 8) & 255; }
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
const cat = (...a) => { const n = a.reduce((s, x) => s + x.length, 0), o = new Uint8Array(n); let p = 0; for (const x of a) { o.set(x, p); p += x.length; } return o; };

// a Huffman tree over all 256 bytes: internal nodes 0..254 (head 254), each byte 8 bits
const nodes = new Uint16Array(510), code = []; let next = 254;
function tree(lo, hi, depth, bits) {
  const me = next--;
  const half = (lo + hi) >> 1;
  for (const [side, a, b] of [[0, lo, half], [1, half, hi]]) {
    const v = b - a === 1 ? a : 256 + tree(a, b, depth + 1, bits | (side << depth));
    if (b - a === 1) code[a] = { bits: bits | (side << depth), n: depth + 1 };
    nodes[2 * me + side] = v;
  }
  return me;
}
tree(0, 256, 0, 0);
function huff(data) {
  const out = []; let acc = 0, n = 0;
  for (const d of data) { const c = code[d]; for (let i = 0; i < c.n; i++) { if ((c.bits >> i) & 1) acc |= 1 << n; n++; if (n === 8) { out.push(acc); acc = 0; n = 0; } } }
  if (n) out.push(acc);
  return Uint8Array.from(out);
}
const dict = new Uint8Array(1024); for (let i = 0; i < 510; i++) w16(dict, 2 * i, nodes[i]);

// pictures: chunks 56.. (Apogee 1.4 = 157 chunks); pic k has 8 x 4 dots, a pattern
const NCH = 157, FIRST = 56;
const picDots = (k) => { const d = new Uint8Array(32); for (let i = 0; i < 32; i++) d[i] = (k * 7 + i) & 255; return d; };
const planar = (d, w, h) => { const o = new Uint8Array(w * h); let s = 0; for (let p = 0; p < 4; p++) for (let y = 0; y < h; y++) for (let x = 0; x < w / 4; x++) o[s++] = d[y * w + 4 * x + p]; return o; };
const pictable = new Uint8Array((NCH - 3) * 4); for (let c = 3; c < NCH; c++) { w16(pictable, 4 * (c - 3), 8); w16(pictable, 4 * (c - 3) + 2, 4); }
const chunks = [];
const head = new Uint8Array(NCH * 3).fill(0xFF);
let graph = new Uint8Array(0);
const put = (c, raw) => { const h = huff(raw), l = new Uint8Array(4); w32(l, 0, raw.length); head[3 * c] = graph.length & 255; head[3 * c + 1] = (graph.length >> 8) & 255; head[3 * c + 2] = graph.length >> 16; graph = cat(graph, l, h); };
put(0, pictable);
for (let k = 0; k < 91; k++) put(FIRST + k, planar(picDots(k), 8, 4));

// VSWAP: 2 walls, 1 sprite, 1 sound
const wall0 = new Uint8Array(4096).map((_, i) => i & 255), wall1 = new Uint8Array(4096).fill(7), spr = Uint8Array.from([1, 2, 3, 4, 5, 6]), snd = new Uint8Array(10);
const vh = new Uint8Array(6 + 4 * 4 + 2 * 4); w16(vh, 0, 4); w16(vh, 2, 2); w16(vh, 4, 3);
let off = vh.length; [wall0, wall1, spr, snd].forEach((d, i) => { w32(vh, 6 + 4 * i, off); w16(vh, 6 + 16 + 2 * i, d.length); off += d.length; });
const vswap = cat(vh, wall0, wall1, spr, snd);
// maps: one map; header 38 bytes, two planes
const p0 = Uint8Array.from([4, 0, 9, 9]), p1 = Uint8Array.from([2, 0, 5]);
const gm = new Uint8Array(8 + 38 + p0.length + p1.length); gm.set([0x54, 0x45, 0x44, 0x35, 0x76, 0x31, 0x2E, 0x30]);
w32(gm, 8, 8 + 38); w32(gm, 12, 8 + 38 + p0.length); w32(gm, 16, 0); w16(gm, 20, p0.length); w16(gm, 22, p1.length); w16(gm, 24, 0); w16(gm, 26, 64); w16(gm, 28, 64);
gm.set([0x4D, 0x41, 0x50], 30); gm.set(p0, 46); gm.set(p1, 46 + p0.length);
const mh = new Uint8Array(402); w16(mh, 0, 0xABCD); w32(mh, 2, 8);
// audio: sound 0 (PC speaker), the rest empty
const ah = new Uint8Array(4 * 88); const s0 = Uint8Array.from([3, 0, 0, 0, 5, 0, 10, 20, 0]); for (let i = 1; i < 88; i++) w32(ah, 4 * i, s0.length);
const files = { 'VSWAP.WL1': vswap, 'MAPHEAD.WL1': mh, 'GAMEMAPS.WL1': gm, 'AUDIOHED.WL1': ah, 'AUDIOT.WL1': s0, 'VGAHEAD.WL1': head, 'VGADICT.WL1': dict, 'VGAGRAPH.WL1': graph };

const r = W.build(files), p = r.pack;
ok(String.fromCharCode(p[0], p[1], p[2], p[3]) === 'W3DP' && u32(p, 12) === p.length, 'pack header and length');
ok(r.ext === 'WL1' && (u32(p, 8) & 1) === 1, 'shareware flag');
ok(u32(p, 16) === 3 && u32(p, 20) === 2, 'pages: 3 (no digitized sound), sprites from 2');
{ const t = u32(p, 24); const o0 = u32(p, t), o2 = u32(p, t + 16);
  ok(p.subarray(o0, o0 + 4096).every((v, i) => v === wall0[i]), 'wall 0 in place');
  ok(u32(p, t + 20) === 6 && p.subarray(o2, o2 + 6).every((v, i) => v === spr[i]), 'sprite in place'); }
ok(u32(p, 28) === 1 && u32(p, 32) === 0xABCD, 'one map, RLEW tag');
{ const h = u32(p, u32(p, 36)); const a = u32(p, h), b = u32(p, h + 4);
  ok(p[a] === 4 && p[a + 2] === 9 && p[b] === 2 && p[b + 2] === 5 && u32(p, h + 8) === 0, 'map planes moved, plane starts relative to the pack');
  ok(String.fromCharCode(p[h + 22], p[h + 23], p[h + 24]) === 'MAP', 'map name'); }
ok(u32(p, 40) === 87 && u32(p, u32(p, 44) + 4) === s0.length && u32(p, u32(p, 44) + 8) === 0, 'PC speaker sound 0, others empty');
{ const t = u32(p, 52); let all = u32(p, 48) === 91;
  all = all && u32(p, t) === 0 && u32(p, t + 8 * 45) === 0;   // pictures the program never shows stay out
  for (const k of [42, 43, 90]) { const o = u32(p, t + 8 * k); all = all && u16(p, t + 8 * k + 4) === 8 && u16(p, t + 8 * k + 6) === 4 && p.subarray(o, o + 32).every((v, i) => v === picDots(k)[i]); }
  ok(all, '91 picture places, the used ones unpacked (Huffman) and deplaned, the others empty'); }
ok(u32(p, 60) === 1, 'one episode');
// a full version prefers .WL6 when both are complete; a missing file is named
const both = Object.assign({}, files); for (const k of Object.keys(files)) both[k.replace('WL1', 'WL6')] = files[k];
ok(W.pickSet(both).ext === 'WL6', 'full version first');
const miss = Object.assign({}, files); delete miss['VGADICT.WL1'];
let e1 = ''; try { W.build(miss); } catch (e) { e1 = e.message; }
ok(/VGADICT\.WL1/.test(e1), 'missing file named: ' + e1);
let e2 = ''; try { W.build({ 'readme.txt': new Uint8Array(1) }); } catch (e) { e2 = e.message; }
ok(/no Wolfenstein/.test(e2), 'no game files: ' + e2);
const odd = Object.assign({}, files, { 'VGAHEAD.WL1': head.subarray(0, 140 * 3) });
let e3 = ''; try { W.build(odd); } catch (e) { e3 = e.message; }
ok(/unknown version/.test(e3), 'unknown VGAHEAD: ' + e3);
{ const big = new Uint8Array(vswap); w16(big, 6 + 16, 16384);
  let e4 = ''; try { W.build(Object.assign({}, files, { 'VSWAP.WL1': big })); } catch (e) { e4 = e.message; }
  ok(/not 64 x 64/.test(e4), 'walls of another size refused: ' + e4); }
console.log(bad ? 'FAIL ' + bad : 'all good');
process.exit(bad ? 1 : 0);
