// 1.6.8: volume, brightness and screen off for TamaEmu and T18Emu, main program and menu.
// test45: the brightness list is 5 % .. 100 % (select index i = light step i + 1, the last one 100 %)
// node test/ui_opt_test.js <paradise.bin> <idl.bin> <tg18.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, idl, tg18, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const setOpts = async (row, v) => { await p.locator('#appsList .prow').nth(row).locator('.ptog').click(); await p.waitForTimeout(300);
    const sels = p.locator('.pdet select'); for (let i = 0; i < 3; i++) await sels.nth(i).selectOption(String(v[i])); await p.waitForTimeout(300);
    await p.locator('#appsList .prow').nth(row).locator('.ptog').click(); await p.waitForTimeout(300); };   // closed again
  const save = async (n) => { const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave'); const dl = await dlp; const f = out + '/' + n; await dl.saveAs(f); return new Uint8Array(fs.readFileSync(f)); };
  // normal ROM, iD L main
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', idl); await p.waitForTimeout(1500);
  await setOpts(0, [1, 2, 3]);
  const a = await save('o1.bin');
  ok(JSON.stringify(E.emuOptGet(a)) === '{"vol":1,"dim":3,"off":3}', 'main iD L: ' + JSON.stringify(E.emuOptGet(a)));
  await p.setInputFiles('#fileRom', out + '/o1.bin'); await p.waitForTimeout(3000); await p.click('.tab[data-tab="emu"]');
  if (!(await p.$('.pdet'))) await p.locator('#appsList .prow .ptog').first().click(); await p.waitForTimeout(300);
  ok((await p.$$eval('.pdet select', (l) => l.map((s) => s.value).join(','))) === '1,2,3', 'loaded again: options shown');
  await p.locator('.pdet select').nth(0).selectOption('2'); await p.waitForTimeout(300);
  const a2 = await save('o2.bin');
  ok(E.emuOptGet(a2).vol === 2, 'changed on an installed ROM: saved (' + JSON.stringify(E.emuOptGet(a2)) + ')');
  // start device: TG18 main, iD L in the menu
  await p.setInputFiles('#fileRom', rom); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500); await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileEmu', tg18); await p.waitForTimeout(800); await p.setInputFiles('#fileEmu', idl); await p.waitForTimeout(1500);
  await setOpts(0, [3, 1, 5]); await setOpts(1, [2, 0, 2]);
  const c = await save('o3.bin');
  const xs = E.appsRead(c, F.feldApps(c, fe.stub).apps);
  ok(JSON.stringify(E.emuOptGet(c)) === '{"vol":3,"dim":2,"off":5}', 'main TG18: ' + JSON.stringify(E.emuOptGet(c)));
  ok(xs[0] && JSON.stringify(xs[0].opt) === '{"vol":2,"dim":1,"off":2}', 'iD L in the menu: ' + JSON.stringify(xs[0] && xs[0].opt));
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
