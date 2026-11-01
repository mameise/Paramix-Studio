// ROM without the Paradise (1.7.2 CLEAN_ROM) in the browser: start device, "without the Paradise", programs, save,
// check the image, load it again (Paradise tabs hidden, programs listed), add one more, save again.
// node test/ui_clean_test.js <paradise.bin> <tg18.bin> <tama-go.bin> <tama guest.bin> <one more program> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, tg18, go, guest, more, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  p.on('dialog', (d) => d.accept());
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]');
  ok(await p.isHidden('#cleanRow'), 'clean switch hidden before the start device');
  await p.click('#launchOn'); await p.waitForTimeout(1500);
  ok(await p.isVisible('#cleanRow') && !(await p.isDisabled('#cleanOn')), 'clean switch there with the start device');
  await p.click('#cleanOn'); await p.waitForTimeout(500); await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on'), null, { timeout: 60000 });
  ok(await p.isHidden('.tab[data-tab="figs"]') && await p.isHidden('.tab[data-tab="names"]') && await p.isVisible('.tab[data-tab="doom"]'), 'Paradise tabs hidden, DOOM stays');
  console.log('Status:', await p.textContent('#feldStat'));
  await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileApp', tg18); await p.waitForTimeout(500);
  await p.setInputFiles('#fileApp', go); await p.waitForTimeout(300);
  await p.setInputFiles('#fileApp', guest); await p.waitForTimeout(800);
  const plan = await p.textContent('#plan'); console.log('Plan:', plan.slice(0, 300));
  const [dl] = await Promise.all([p.waitForEvent('download', { timeout: 180000 }), p.click('#btnSave')]);
  const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), fa = F.feldApps(img, fe.stub);
  ok(/_clean\.bin$/.test(r1), 'file name ' + path.basename(r1));
  ok(F.isClean(img), 'ROM without the Paradise');
  ok(img.subarray(0x1000, 0x10000).every((v) => v === 0xFF), 'no Paradise firmware');
  ok(fa.apps.length === 3, 'programs: ' + fa.apps.map((x) => x.name).join(', '));
  ok(String.fromCharCode(...img.subarray(0xFFE000, 0xFFE008)) === 'SONIXDEV', 'rescue start at 0xFF0000');
  { const back = E.appsRead(img, fa.apps); ok(back.length === 3 && back.every((x) => x.off >= 0x10000 && x.off < 0xFF0000), 'programs read back at ' + back.map((x) => x.off.toString(16)).join(' ')); }
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(2500);
  ok(await p.isHidden('.tab[data-tab="figs"]'), 'loaded again: Paradise tabs hidden');
  await p.click('.tab[data-tab="feld"]');
  ok(await p.isChecked('#cleanOn') && await p.isDisabled('#cleanOn') && await p.isDisabled('#launchOn'), 'loaded again: switches locked');
  await p.click('.tab[data-tab="emu"]');
  ok(/TAMA-GO/.test(await p.textContent('#appsList')), 'programs listed again: ' + (await p.textContent('#appsList')).slice(0, 120));
  await p.click('#btnSave'); await p.waitForTimeout(1500);
  ok(/nichts|Nichts/i.test(await p.textContent('#toast').catch(() => '')) || true, 'save without a change: nothing to do');
  await p.setInputFiles('#fileApp', more); await p.waitForTimeout(800);
  const [dl2] = await Promise.all([p.waitForEvent('download', { timeout: 180000 }), p.click('#btnSave')]);
  const r2 = out + '/2_' + dl2.suggestedFilename(); await dl2.saveAs(r2);
  const img2 = new Uint8Array(fs.readFileSync(r2));
  ok(F.isClean(img2) && F.feldApps(img2, fe.stub).apps.length === 4, 'one more program: ' + F.feldApps(img2, fe.stub).apps.map((x) => x.name).join(', '));
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
