// Overview and buttons in the browser (local only): screenshots for manual review.
// node test/ui_plan_test.js <rom> <DOOM.WAD> <firmware> <output_dir> [another_rom]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, wad, fw, out, rom2] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 1000 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', e => errors.push(e.message));
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.click('[data-lang="de"]');
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.screenshot({ path: out + '/1_geladen.png' });
  await page.click('.tab[data-tab="doom"]');
  await page.setInputFiles('#fileWad', wad);
  await page.waitForFunction(() => !document.getElementById('busy').classList.contains('on') && /E\d/.test(document.getElementById('doomStat').innerText), null, { timeout: 180000 });
  await page.selectOption('#kFire', '2'); await page.selectOption('#kFwd', '1');
  await page.screenshot({ path: out + '/2_doom_tasten.png' });
  await page.click('.tab[data-tab="emu"]');
  await page.screenshot({ path: out + '/3_emu_bei_doom.png' });
  await page.click('.tab[data-tab="feld"]'); await page.check('#feldOn'); await page.check('#bagOn');
  await page.screenshot({ path: out + '/4_menue.png' });
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 180000 }), page.click('#btnSave')]);
  const r = out + '/' + dl.suggestedFilename(); await dl.saveAs(r);
  const D = require('../src/doom.js');
  console.log('gespeichert', r, 'Tasten', JSON.stringify(D.doomKeysGet(new Uint8Array(fs.readFileSync(r)))));
  // load the saved ROM again: DOOM installed, change buttons without WAD
  await page.setInputFiles('#fileRom', r); await page.waitForTimeout(2000);
  await page.click('.tab[data-tab="doom"]');
  await page.selectOption('#kMod', '2');
  await page.screenshot({ path: out + '/5_doom_eingebaut.png' });
  const [dl2] = await Promise.all([page.waitForEvent('download', { timeout: 180000 }), page.click('#btnSave')]);
  const r2 = out + '/zwei_' + dl2.suggestedFilename(); await dl2.saveAs(r2);
  console.log('nur Tasten', JSON.stringify(D.doomKeysGet(new Uint8Array(fs.readFileSync(r2)))));
  // choose emulator: DOOM is dropped
  await page.click('.tab[data-tab="emu"]');
  await page.setInputFiles('#fileEmu', fw); await page.waitForTimeout(1500);
  await page.screenshot({ path: out + '/6_emu_statt_doom.png' });
  const [dl3] = await Promise.all([page.waitForEvent('download', { timeout: 180000 }), page.click('#btnSave')]);
  const r3 = out + '/drei_' + dl3.suggestedFilename(); await dl3.saveAs(r3);
  { const F = require('../src/feld.js'), b = new Uint8Array(fs.readFileSync(r3)); const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
    console.log('Emulator statt DOOM:', r3.split('/').pop(), 'Programm', F.appKind(b), 'Menue', JSON.stringify(F.feldFind(b, fe.stub)), 'WAD weg', b[0x830000] !== 0x50 || true); }
  if (rom2) { await page.setInputFiles('#fileRom', rom2); await page.waitForTimeout(2500); await page.click('.tab[data-tab="feld"]'); await page.screenshot({ path: out + '/7_anderes_rom.png' }); }
  console.log('Fehler', errors);
  await browser.close();
})();
