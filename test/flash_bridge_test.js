// Flasher against the emulated Paradise. Needs the UART bridge of the Paradise
// system emulator (a separate project, not part of this repository): it runs the
// device firmware and forwards UART bytes over stdin/stdout.
// node test/flash_bridge_test.js <bridge-binary> <firmware.bin>
const { spawn } = require('child_process');
const fs = require('fs');
const FL = require('../src/flash.js'), F = require('../src/feld.js'), E = require('../src/emu.js');
const [bridge, fwFile] = process.argv.slice(2);
const app = new Uint8Array(fs.readFileSync(__dirname + '/../flash/flashapp.bin'));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok    ' : 'WRONG ') + m); if (!c) bad++; };
function start(file, out) {
  const p = spawn(bridge, [file, out], { stdio: ['pipe', 'pipe', 'inherit'] });
  const io = { onData: null, write: (d) => new Promise((r) => p.stdin.write(Buffer.from(d), r)) };
  p.stdout.on('data', (d) => io.onData && io.onData(new Uint8Array(d)));
  const ready = new Promise((r) => setTimeout(r, 100));
  return { p, io, ready, close: () => new Promise((r) => { p.on('exit', r); p.stdin.end(); }) };
}
(async () => {
  const base = new Uint8Array(fs.readFileSync(fwFile));
  const out = '/tmp/claude-0/bridge_flash.bin';
  const b = start(fwFile, out);
  await new Promise((r) => setTimeout(r, 15000));   // let the device reach the download screen
  const t0 = Date.now();
  const { dev, loaded } = await FL.connect(b.io, app, { timeScale: 20 }, (ph, d, t) => {});
  ok(loaded && dev.info.capacity === 0x1000000, 'connected, app loaded: ' + JSON.stringify(dev.info) + ' ' + ((Date.now() - t0) / 1000 | 0) + ' s');
  const r = await dev.read(0, 0x10000);
  ok(Buffer.compare(Buffer.from(r), Buffer.from(base.subarray(0, 0x10000))) === 0, 'read 64 KB matches');
  // new image: one byte in the character data, one in the save data
  const img = new Uint8Array(base); img[0x200010] ^= 0x5A; img[0xEFE100] ^= 0x33; img[0x0A0000 + 0x8000] ^= 1;
  const keep = FL.keepPlan(F, E, null, {});
  const before = await dev.read(0xEFE000, 0x1000);
  let t1 = Date.now();
  const res = await dev.write(img, { onlyChanged: true, keep }, null);
  const after = await dev.read(0xEFE000, 0x1000), ch = await dev.read(0x200000, 0x1000);
  ok(Buffer.compare(Buffer.from(before), Buffer.from(after)) === 0 && Buffer.compare(Buffer.from(ch), Buffer.from(img.subarray(0x200000, 0x201000))) === 0, 'save sector kept, character sector written');
  ok(res.written === 2 && res.kept >= 1, 'only changes, keep saves: written ' + res.written + ', kept ' + res.kept + ' ' + ((Date.now() - t1) / 1000 | 0) + ' s ' + res.notes.join('; '));
  t1 = Date.now();
  const res2 = await dev.write(img, { onlyChanged: true, keep: null }, null);
  ok(res2.written === res.kept, 'only changes without keep: written ' + res2.written + ' ' + ((Date.now() - t1) / 1000 | 0) + ' s');
  // edition check
  const other = new Uint8Array(img); other[0xD49000] = 2;
  let err = ''; try { await dev.write(other, { onlyChanged: true, keep }, null); } catch (e) { err = e.message; }
  ok(/Untick/.test(err), 'other edition refused: ' + err);
  await b.close();
  const fl = new Uint8Array(fs.readFileSync(out));
  ok(Buffer.compare(Buffer.from(fl), Buffer.from(img)) === 0, 'device flash equals the image at the end');
  console.log(bad ? bad + ' wrong' : 'all good');
  process.exit(bad ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
