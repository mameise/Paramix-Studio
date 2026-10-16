// More emulators in the browser: Friends as T7Emu, Tama-Go as a second one, menu on, save, load again.
// node test/ui_apps_test.js <paradise.bin> <friends.bin> <tama-go.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js'), SV = require('../src/saves.js');
(async () => {
  const [rom, fr, go, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileEmu', fr); await p.waitForTimeout(300);
  await p.setInputFiles('#fileApp', go); await p.waitForTimeout(300);
  console.log('Liste:', await p.textContent('#appsList'));
  await p.click('.tab[data-tab="feld"]'); if (!(await p.isChecked('#feldOn'))) await p.click('#feldOn'); await p.waitForTimeout(300);
  const [dl] = await Promise.all([p.waitForEvent('download', { timeout: 120000 }), p.click('#btnSave')]);
  const r1 = out + '/apps.bin'; await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), fa = F.feldApps(img, fe.stub);
  ok(fa.name0 === 'FRIENDS' && fa.apps.length === 1 && fa.apps[0].name === 'TAMA-GO', 'saved: ' + fa.name0 + ' + ' + fa.apps.map((x) => x.name));
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="emu"]');
  const l = await p.textContent('#appsList');
  ok(/TAMA-GO/.test(l), 'loaded again: ' + l);
  const plan = await p.textContent('#plan');
  console.log('Plan:', plan.slice(0, 200));
  // save.bin into the second T7Emu through the Save tab
  const sv = new Uint8Array(4098); sv[0] = 0xFF; sv[1] = 0xFF; for (let i = 2; i < 4098; i++) sv[i] = (i * 7) & 255;
  fs.writeFileSync(out + '/save.bin', sv);
  await p.click('.tab[data-tab="save"]');
  ok(!(await p.isHidden('#saveForRow')), 'Save tab: choice of the T7Emu shown');
  await p.selectOption('#saveFor', '0');
  await p.setInputFiles('#fileEmuSav', out + '/save.bin'); await p.waitForTimeout(300);
  console.log('Status:', await p.textContent('#saveStat'));
  const [dl2] = await Promise.all([p.waitForEvent('download', { timeout: 120000 }), p.click('#btnSave')]);
  const r2 = out + '/apps2.bin'; await dl2.saveAs(r2);
  const img2 = new Uint8Array(fs.readFileSync(r2)), x = E.appsRead(img2, F.feldApps(img2, fe.stub).apps)[0];
  const got = x && SV.t7SaveOut(img2, x.saveOff);
  ok(got && Buffer.from(got).equals(Buffer.from(sv)) && !SV.t7SaveOut(img2), 'save of TAMA-GO in its own area, main one untouched');
  // and out again from the loaded ROM
  await p.setInputFiles('#fileRom', r2); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="save"]'); await p.selectOption('#saveFor', '0');
  const [dl3] = await Promise.all([p.waitForEvent('download', { timeout: 30000 }), p.click('#btnEmuOut')]);
  const r3 = out + '/' + dl3.suggestedFilename(); await dl3.saveAs(r3);
  ok(Buffer.from(fs.readFileSync(r3)).equals(Buffer.from(sv)) && /tama-go/.test(r3), 'export of TAMA-GO: ' + path.basename(r3));
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
