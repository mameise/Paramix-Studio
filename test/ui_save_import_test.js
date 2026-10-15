// Tab "Save": import a TamaEmu .sav into a ROM, save, export again, compare.
// Usage: node test/ui_save_import_test.js <rom_with_emu> <save.sav> <output_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, sav, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="save"]');
  console.log('Einspielen Paradise ohne Gerät gesperrt:', await page.isDisabled('#btnParaIn'));
  await page.setInputFiles('#fileEmuSav', sav);
  await page.waitForTimeout(800);
  console.log('Status:', await page.textContent('#saveStat'));
  console.log('Plan:', (await page.textContent('#plan')).slice(0, 120));
  await page.screenshot({ path: out + '/save_import.png' });
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#btnSave')]);
  const romOut = out + '/rom_out.bin'; await dl.saveAs(romOut);
  await page.setInputFiles('#fileRom', romOut); await page.waitForTimeout(1500);
  await page.click('.tab[data-tab="save"]');
  const [d2] = await Promise.all([page.waitForEvent('download'), page.click('#btnEmuOut')]);
  const back = out + '/back.sav'; await d2.saveAs(back);
  console.log('sav gleich nach Rundweg:', fs.readFileSync(back).equals(fs.readFileSync(sav)));
  // wrong file
  await page.setInputFiles('#fileEmuSav', { name: 'x.sav', mimeType: 'application/octet-stream', buffer: Buffer.alloc(1000) });
  await page.waitForTimeout(300);
  console.log('Falsche Datei:', await page.textContent('#toast'));
  console.log('Fehler:', errors);
  await browser.close();
})();
