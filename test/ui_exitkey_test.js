// test24: tab Menu, "Back from programs": the mode goes into every program (word 0x13C of its vector table), is shown
// again after loading, and 0 gives the programs their own word back.
// node test/ui_exitkey_test.js <land.bin> <colour Tama firmware> <tamago.bin> <out>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [land, fw, go, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message)); p.on('dialog', (d) => d.accept());
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (n) => { const dlp = p.waitForEvent('download', { timeout: 240000 }).catch(() => null); await p.click('#btnSave'); const dl = await dlp; if (!dl) { console.log('no download: ' + await p.evaluate(() => document.getElementById('toast').textContent)); process.exit(1); } const f = out + '/' + n; await dl.saveAs(f); return f; };
  const load = async (f) => { await p.setInputFiles('#fileRom', f); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2500); };
  const u32 = (a, o) => (a[o] | a[o + 1] << 8 | a[o + 2] << 16 | a[o + 3] << 24) >>> 0;
  const offs = (img) => [0xA0000].concat(E.appsRead(img, F.feldApps(img, fe.stub).apps).map((x) => x.off));
  await load(land);
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', fw); await p.waitForTimeout(3000);
  await p.setInputFiles('#fileApp', go); await p.waitForTimeout(1000);
  const r0 = await save('r0.bin'); let img = new Uint8Array(fs.readFileSync(r0));
  const own = offs(img).map((o) => u32(img, o + 0x13C));
  ok(own.length >= 2 && own.every((w, i) => w === u32(img, offs(img)[i] + 0x138)), 'default: the programs keep their own word (' + own.length + ' programs)');
  await load(r0); await p.click('.tab[data-tab="feld"]');
  ok(await p.evaluate(() => document.getElementById('exitKey').value) === '0', 'shown: 0');
  await p.selectOption('#exitKey', '1'); await p.waitForTimeout(300);
  const r1 = await save('r1.bin'); img = new Uint8Array(fs.readFileSync(r1));
  ok(offs(img).every((o) => u32(img, o + 0x13C) === 0x58450001), 'wheel and A: in every program');
  await load(r1); await p.click('.tab[data-tab="feld"]');
  ok(await p.evaluate(() => document.getElementById('exitKey').value) === '1', 'shown again after loading: 1');
  await p.selectOption('#exitKey', '0'); await p.waitForTimeout(300);
  const r2 = await save('r2.bin'); img = new Uint8Array(fs.readFileSync(r2));
  ok(offs(img).every((o, i) => u32(img, o + 0x13C) === own[i]), 'back to 0: the own words again');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
