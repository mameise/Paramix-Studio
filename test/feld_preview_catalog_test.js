// Synthetic ARC2 archives: padding must not suppress preview metadata.
const assert = require('assert'), fs = require('fs');
const C = require('../src/core.js'), F = require('../src/feld.js'), E = require('../src/emu.js');
function fixture(padding) {
  const dp = Buffer.alloc(280), u16 = (o, v) => dp.writeUInt16LE(v, o), u32 = (o, v) => dp.writeUInt32LE(v, o);
  u32(0, 46); u32(40, 40); u32(44, 100); // 4 character records, 30 bytes each
  [65535, 1000, 1001, 4001].forEach((id, i) => { u16(80 + i * 30, id); u16(86 + i * 30, [0, 1, 2, 5][i]); u16(94 + i * 30, i === 3 ? 1 : 0); });
  u32(60, 100); u32(64, 136); u32(72, 136); u32(76, 140);
  for (let i = 0; i < 6; i++) { const o = 200 + i * 12; [0x607, 0, 0, 0, 0, (i < 3 ? 6 : 12) + i % 3].forEach((v, j) => u16(o + j * 2, v)); }
  u16(272, 0); u16(274, 3); u16(276, 3); u16(278, 3);
  const shapes = [[64, 64, 20], [64, 32, 20], [64, 32, 18], [32, 32, 24], [32, 16, 14], [32, 16, 14]];
  function sprite(w, h, n, variants = 1) {
    const b = Buffer.alloc(24 + variants * 32 + w * h / 2 * n);
    b.writeUInt32LE(b.length, 0); b[4] = 4; b[5] = 2; b.writeUInt16LE(n, 6); b[8] = w; b[9] = h;
    b[12] = b[13] = 1; b[14] = 17; b[15] = variants; b.writeUInt16LE(24, 18); b.writeUInt16LE(24 + variants * 32, 20);
    for (let i = 0; i < variants; i++) { b.writeUInt16LE(0x07E0, 24 + i * 32); b.writeUInt16LE(12, 26 + i * 32); }
    return b;
  }
  const entries = []; for (let i = 0; i < 3; i++) for (const s of shapes) entries.push(sprite(...s)); entries.push(sprite(64, 64, 1, 16));
  const base = C.packJoin(entries), pack = new Uint8Array(base.length + (4 - padding) % 4); pack.set(base);
  const rom = new Uint8Array(0x1000000).fill(255); rom.set(Buffer.from('SONIXDEV'));
  rom.set(C.arc2Build([dp, pack, C.arc2Build(Array(9).fill(new Uint8Array(8)))]), 0x111000);
  const b = Buffer.from(rom.buffer); b.writeUInt32LE(1, 0xD49000);
  ['PSID_TEST', 'PSID_HOME', 'PSID_MORE'].forEach((s, i) => {
    rom.set(Buffer.from(s + '\0'), 0x200000 + i * 16); b.writeUInt32LE(0x10000000 + 0x200000 + i * 16 - 0x11000, 0x300004 + i * 28);
    for (let j = 0; j < 4; j++) b.writeUInt32LE(0x10001001 + i * 256 + j * 16, 0x30000C + i * 28 + j * 4);
  });
  return rom;
}
const emb = { feld: new Uint8Array(fs.readFileSync(__dirname + '/../feld/feld.bin')), stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
for (let padding = 0; padding <= 3; padding++) {
  const rom = fixture(padding), entry = C.openRom(rom).root.ents[1]; assert.equal(entry.clen - entry.ulen, padding);
  const out = F.feldApply(rom, { tama: true }, [], emb, E), f = F.feldFind(out, emb.stub), b = Buffer.from(out), h = f.catOff + 12 + 40 * f.catN;
  assert.equal(b.readUInt32LE(h), 0x56525054); assert.equal(b.readUInt32LE(h + 12), 2);
  assert.deepEqual(F.feldStrip(out, emb, E), rom);
}
// Language helper page is owned even when its unused bytes are FF.
const rom = fixture(2), p = 0x10E000, b = Buffer.from(rom.buffer);
b.fill(255, p, p + 4096); b.writeUInt32LE(824, p + 36); b.write('ZHLANG01', p + 0x80);
[[0x88, 1], [0x8C, p], [0x90, 824], [0x94, 0], [0x98, 4096]].forEach(([o, v]) => b.writeUInt32LE(v, p + o));
let sum = 0; for (let i = 0; i < 128; i++) sum += b[p + i]; b.writeUInt32LE(sum, p + 0x9C);
assert.deepEqual(F.languagePatchPages(rom), [p]);
// A checksum-only fake marker is insufficient once the shared Settings code is
// validated; refuse it. settings_test.js covers genuine helper preservation.
assert.throws(() => F.feldApply(rom, { fields: true, tama: true }, [], emb, E), /Settings patch code/);
assert.throws(() => F.feldApply(rom, { fields: true, tama: true, paramixShortcut: false }, [], emb, E), /Settings patch code/);
b[p + 0x9C] ^= 1; assert.throws(() => F.languagePatchPages(rom), /damaged language/);
console.log('PASS: padded ARC2 preview indices and language-patch page preservation');
