// 1.7.6 DETECT_TAG: another version of a P's or 4U (unknown reset vector) is still recognised by its download names.
// node test/detect_tag_test.js <firmware.bin>... (P's, 4U+, iD L)
const fs = require('fs'), E = require('../src/emu.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
for (const f of process.argv.slice(2)) {
  const g = new Uint8Array(fs.readFileSync(f)), d0 = E.emuDetect(g), h = g.slice(); h[0x400000] ^= 0x10;
  ok(E.emuDetect(h) === d0 || (d0 === '4u-plain' && E.emuDetect(h) === '4u'), f.split('/').pop() + ': ' + d0 + ' also with another reset vector');
}
process.exit(bad ? 1 : 0);
