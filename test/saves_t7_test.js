const SV = require('../src/saves.js'); const assert = require('assert');
let img = new Uint8Array(16 << 20).fill(0xFF);
assert.strictEqual(SV.t7SaveOut(img), null);
const sv = new Uint8Array(4098); sv[0] = 0x05; for (let i = 2; i < 4098; i++) sv[i] = i * 7 & 255;
img = SV.t7SaveIn(img, sv);
assert.deepStrictEqual(SV.t7SaveOut(img), sv);
const sv2 = sv.slice(); sv2[100] ^= 0x55; sv2[0] = 0x07;
img = SV.t7SaveIn(img, sv2);                       // goes into the other slot with a higher sequence
assert.deepStrictEqual(SV.t7SaveOut(img), sv2);
img = SV.t7SaveIn(img, sv);                        // back into slot A
assert.deepStrictEqual(SV.t7SaveOut(img), sv);
assert.ok(img.subarray(0xC0000, 0xC1000).every(v => v === 0xFF));
console.log('saves_t7 ok');
