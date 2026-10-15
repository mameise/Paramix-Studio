// Save games against the emulated Paradise (UART bridge, see flash_bridge_test.js).
// node test/save_bridge_test.js <bridge-binary> <firmware.bin>
const { spawn } = require('child_process');
const fs = require('fs');
const FL = require('../src/flash.js'), SV = require('../src/saves.js');
const [bridge, fwFile] = process.argv.slice(2);
const app = new Uint8Array(fs.readFileSync(__dirname + '/../flash/flashapp.bin'));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok    ' : 'WRONG ') + m); if (!c) bad++; };
(async () => {
  const out = '/tmp/claude-0/bridge_save.bin';
  const p = spawn(bridge, [fwFile, out], { stdio: ['pipe', 'pipe', 'inherit'] });
  const io = { onData: null, write: (d) => new Promise((r) => p.stdin.write(Buffer.from(d), r)) };
  p.stdout.on('data', (d) => io.onData && io.onData(new Uint8Array(d)));
  await new Promise((r) => setTimeout(r, 15000));
  const { dev } = await FL.connect(io, app, { timeScale: 20 }, () => {});
  const ed = dev.info.edition, [a, b] = SV.region(ed);
  ok(ed >= 1 && ed <= 6, 'edition ' + ed);
  const clock = await dev.clockRead();
  ok(clock && typeof clock.counter === 'number', 'clock read: ' + JSON.stringify(clock));
  let t0 = Date.now();
  const used = await dev.usedSectors(a, b);
  ok(used.length > 0, 'used save sectors: ' + used.length + ' (' + ((Date.now() - t0) / 1000).toFixed(1) + ' s)');
  const base = new Uint8Array(fs.readFileSync(fwFile));
  const exp = []; for (let s = a; s < b; s += 0x1000) { let ff = true; for (let i = s; i < s + 0x1000; i++) if (base[i] !== 0xFF) { ff = false; break; } if (!ff) exp.push(s); }
  ok(exp.every((s) => used.includes(s)), 'every used sector of the image found (device ' + used.length + ', image ' + exp.length + ')');
  // write two pages of a save, set the clock
  const map = new Map(); const pg = new Uint8Array(0x1000).fill(0x5A); map.set(0xE10000, pg); map.set(0xE11000, new Uint8Array(0x1000).fill(0xFF));
  t0 = Date.now();
  const w = await dev.writeSectors(map);
  const r1 = await dev.read(0xE10000, 0x1000);
  ok(w === 2 && r1.every((v) => v === 0x5A), 'two pages written w=' + w + ' r=' + r1[0] + ' len=' + r1.length + ' (' + ((Date.now() - t0) / 1000).toFixed(1) + ' s)');
  const set = await dev.clockSet(1234567);
  const c2 = await dev.clockRead();
  ok(set && c2 && c2.counter - 1234567 < 5, 'clock set: ' + JSON.stringify(c2));
  p.stdin.end(); await new Promise((r) => p.on('exit', r));
  const fl = new Uint8Array(fs.readFileSync(out));
  ok(fl[0xE10000] === 0x5A && fl.subarray(0x200000, 0x300000).every((v, i) => v === base[0x200000 + i]), 'device flash: page written, the rest untouched');
  console.log(bad ? bad + ' wrong' : 'all good'); process.exit(bad ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
