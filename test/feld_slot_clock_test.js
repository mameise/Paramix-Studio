const assert = require('assert');
const F = require('../src/feld.js');
const u32 = (p, o) => new DataView(p.buffer, p.byteOffset, p.byteLength).getUint32(o, true);
const w32 = (p, o, n) => new DataView(p.buffer, p.byteOffset, p.byteLength).setUint32(o, n, true);
function crc(p) {
  let c = 0;
  for (let i = 4; i < p.length; i++) { c ^= p[i]; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1; }
  return c;
}
const p = new Uint8Array(4096); w32(p, 12, 300); w32(p, 16, 40000);
let c = crc(p); p[0] = c; p[1] = c >> 8; p[2] = ~c; p[3] = ~c >> 8;
const source = F.slotBuild(new Map([[0xEFE000, p], [0xEFF000, p]]), 0, 0xEFE000, 0xF00000);
w32(source.head, 4080, 0x43544D50); w32(source.head, 4084, 100); w32(source.head, 4088, 1000); w32(source.head, 4092, 0x43544D50 ^ 100 ^ 1000);
const raw = source.stream.slice(), pages = F.slotPages(source), rebased = pages.get(0xEFE000);
assert.equal(u32(rebased, 16), 39100); assert.equal(u32(rebased, 12), 300);
assert.equal(rebased[0] | rebased[1] << 8, crc(rebased)); assert.deepEqual(source.stream, raw);
assert.deepEqual(pages.get(0xEFE000), pages.get(0xEFF000));
const rebuilt = F.slotBuild(pages, 1, 0xEFE000, 0xF00000);
assert.equal(u32(rebuilt.head, 4080), 0xFFFFFFFF); assert.equal(u32(F.slotPages(rebuilt).get(0xEFE000), 16), 39100);
const out = new Uint8Array(0x10000), f = { slotOff: 0x1000, slotSize: 0x4000, nslots: 3 };
F.slotPlace(out, f, 2, source, new Set());
assert.deepEqual(out.slice(0x9000 + 4080, 0xA000), source.head.slice(4080));
source.head[4092] ^= 1; assert.throws(() => F.slotPages(source), /clock damaged/);
console.log('PASS: received-save clock survives Studio export, editing and slot relocation');
