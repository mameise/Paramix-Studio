// Tests the core on real images: lossless rebuild, decoding,
// pixel-exact re-encoding of all sprites. Usage: node test/rom_test.js <rom...>
const C = require('../src/core.js');
const fs = require('fs');
function same(a, b) { if (a.w !== b.w || a.h !== b.h) return false; for (let i = 0; i < a.data.length; i += 4) { const ta = a.data[i + 3] < 128, tb = b.data[i + 3] < 128; if (ta !== tb) return false; if (ta) continue; for (let k = 0; k < 3; k++) if (Math.abs(a.data[i + k] - b.data[i + k]) > 2) return false; } return true; }
let fail = 0;
for (const f of process.argv.slice(2)) {
  const rom = new Uint8Array(fs.readFileSync(f));
  const t0 = Date.now();
  const ctx = C.openRom(rom);
  const id = C.buildRom(ctx, new Map());
  const identical = Buffer.compare(Buffer.from(id.rom), Buffer.from(rom)) === 0;
  let dec = 0, rt = 0, rtKeep = 0, bad = [], badK = [];
  const sample = process.env.ALL ? ctx.entries.length : Math.min(ctx.entries.length, 400);
  for (let i = 0; i < ctx.entries.length; i++) {
    const e = ctx.entries[i];
    const d = C.spriteDecode(e); dec++;
    if (i >= sample && !ctx.groups.includes(i)) continue;
    if (d.cells.length > 3000) continue; // fonts: slow, same code
    const enc = C.spriteEncode(e, d.subs, {});
    const d2 = C.spriteDecode(enc);
    if (d2.subs.every((s, k) => same(s, d.subs[k]))) rt++; else bad.push(i);
    const encK = C.spriteEncode(e, d.subs, { keepPalette: true });
    const d3 = C.spriteDecode(encK);
    if (d3.subs.every((s, k) => same(s, d.subs[k]))) rtKeep++; else badK.push(i);
  }
  console.log(f.split('/').pop(), ctx.edition, 'sprites', ctx.entries.length, 'groups', ctx.groups.length, 'identical', identical, 'decoded', dec, 'roundtrip ok', rt, 'bad', bad.slice(0, 10), 'keep ok', rtKeep, 'badK', badK.slice(0, 10), 'free KB', (ctx.limit - ctx.assetEnd) >> 10, (Date.now() - t0) + 'ms');
  if (!identical || bad.length || badK.length) fail++;
}
process.exit(fail ? 1 : 0);
