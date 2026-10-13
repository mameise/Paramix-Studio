// Check the DOOM build in the studio against the Python/C path (local only, needs your own DOOM.WAD; none of it goes into the repository).
// node test/doom_test.js <paradise.bin> <DOOM.WAD> [compare_mcu.wad] [output.bin]
const fs = require('fs');
const E = require('../src/emu.js'), D = require('../src/doom.js');
(async () => {
  const rom = new Uint8Array(fs.readFileSync(process.argv[2])), wad = new Uint8Array(fs.readFileSync(process.argv[3]));
  console.log('Episoden', D.wadEpisodes(wad));
  let t0 = Date.now();
  const sub = D.wadSub(wad, '1', true); console.log('Teil-WAD', sub.length, Date.now() - t0, 'ms');
  if (process.env.SUBREF) { const r = fs.readFileSync(process.env.SUBREF); console.log('Teil-WAD gleich wie wadsub.py:', Buffer.compare(Buffer.from(sub), r) === 0); }
  const ex = D.gbaExtras(sub);
  if (process.env.EXREF) { const r = fs.readFileSync(process.env.EXREF); console.log('Zusatz gleich wie gbaextras.py:', Buffer.compare(Buffer.from(ex), r) === 0); }
  const wasm = fs.readFileSync(__dirname + '/../doom/mcuwad.wasm');
  t0 = Date.now(); const mcu = await D.convert(sub, ex, wasm); console.log('umgewandelt', mcu.length, Date.now() - t0, 'ms');
  if (process.argv[4]) { const r = fs.readFileSync(process.argv[4]); console.log('MCU-WAD gleich wie mcuwad (C):', Buffer.compare(Buffer.from(mcu), r) === 0); }
  const emb = { app: fs.readFileSync(__dirname + '/../doom/doom_app.bin'), xip: fs.readFileSync(__dirname + '/../doom/doom_xip.bin'), wasm, stub: fs.readFileSync(__dirname + '/../emu/stub_tpl.bin') };
  const r = D.doomPatch(rom, mcu, emb, E);
  console.log('ROM fertig, frei vor dem Bereich', r.free, 'Byte');
  if (process.argv[5]) fs.writeFileSync(process.argv[5], r.rom);
})().catch(e => { console.error('FEHLER', e); process.exit(1); });
