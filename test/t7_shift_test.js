// test27: a T7Emu EEPROM slot written 2 bytes off (flash DMA from a buffer at ...2) is still read by the Studio
const SV = require('../src/saves.js');
const img = new Uint8Array(0x1000000).fill(0xFF), base = 0x850000, at = base + 0x1000;
const pages = new Uint8Array(4096); for (let i = 0; i < 4094; i++) pages[i] = (i * 7 + 3) & 0xFF; pages[4094] = pages[4095] = 0;
let s = 0x7E7E; for (let i = 0; i < 4096; i++) s = (Math.imul(s, 31) + pages[i]) >>> 0;
const w32 = (o, v) => { img[o] = v & 255; img[o + 1] = (v >>> 8) & 255; img[o + 2] = (v >>> 16) & 255; img[o + 3] = v >>> 24; };
w32(at, 0x45453754); w32(at + 4, 5); w32(at + 8, 0xFFFF); w32(at + 12, s);
img[at + 0x1000] = 0xAB; img[at + 0x1001] = 0xCD; img.set(pages.subarray(0, 4094), at + 0x1002);   // as the DMA wrote it
const o = SV.t7SaveOut(img, base);
if (!o) throw new Error('shifted slot not found');
for (let i = 0; i < 4096; i++) if (o[2 + i] !== pages[i]) throw new Error('byte ' + i);
// a normal slot still reads as before
const n = SV.t7SaveIn(img, o, base), o2 = SV.t7SaveOut(n, base);
for (let i = 0; i < o.length; i++) if (o[i] !== o2[i]) throw new Error('round trip ' + i);
console.log('all good');
