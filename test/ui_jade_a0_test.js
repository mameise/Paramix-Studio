// 1.6.9 test25 (GitHub issue): Jade Forest wave 2, the firmware reaches past 0x0A0000. With "Start device" the
// emulators (T7Emu, V1Emu) go into the menu of a normal ROM;
// 0x0A0000.. of the firmware stays as it is. A colour Tama is refused outside a start device.
// node test/ui_jade_a0_test.js <jade.bin> <p2.bin> <tama-go.bin> <out_dir> [idl.bin]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, p2, go, out, idl] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) }, raw = new Uint8Array(fs.readFileSync(rom));
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="emu"]');
  const st = async () => p.evaluate(() => document.getElementById('emuStat').innerText + '|' + document.getElementById('btnEmu').disabled);
  let s = await st(); ok(/Feldwechsel oder Items/.test(s) && /false$/.test(s), 'normal ROM: hint, button on (' + s + ')');
  if (idl) { await p.setInputFiles('#fileEmu', idl); await p.waitForTimeout(800);
    const tt = await p.evaluate(() => document.getElementById('toast').textContent); ok(/nur im Startgerät/.test(tt), 'colour Tama refused: ' + tt); }
  await p.setInputFiles('#fileEmu', p2); await p.waitForTimeout(800);
  await p.setInputFiles('#fileEmu', go); await p.waitForTimeout(800);
  await p.click('.tab[data-tab="feld"]'); if (!(await p.isChecked('#feldOn'))) await p.click('#feldOn'); await p.waitForTimeout(500); await p.click('.tab[data-tab="emu"]');
  const rows = async () => p.evaluate(() => [...document.querySelectorAll('#appsList .prow')].map((r) => r.querySelector('.pname').textContent));
  const l = await rows(); console.log('Liste:', l);
  ok(l.includes('TAMA P2') && l.includes('TAMA-GO'), 'both in the list');
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1));
  ok(Buffer.from(img.subarray(0x9F000, 0xA1700)).equals(Buffer.from(raw.subarray(0x9F000, 0xA1700))), 'firmware at 0x0A0000 unchanged');
  const fa = F.feldApps(img, fe.stub); console.log('menu:', fa.apps.map((a) => a.name).join(', '));
  ok(fa.apps.some((a) => a.name === 'TAMA P2') && fa.apps.some((a) => a.name === 'TAMA-GO'), 'both in the menu');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
