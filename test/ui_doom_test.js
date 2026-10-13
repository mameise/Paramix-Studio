// DOOM tab in the browser (local only, needs your own WAD): load ROM, pick WAD, save, compare with doom_test.js.
// Usage: node test/ui_doom_test.js <rom> <DOOM.WAD> <output_dir> [reference.bin]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, wad, out, ref] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="doom"]');
  await page.waitForTimeout(300);
  await page.screenshot({ path: out + '/doom_1.png' });
  await page.setInputFiles('#fileWad', wad);
  await page.waitForFunction(() => !document.getElementById('busy').classList.contains('on') && /E\d/.test(document.getElementById('doomStat').innerText), null, { timeout: 120000 });
  await page.screenshot({ path: out + '/doom_2.png' });
  const stat = await page.evaluate(() => [document.getElementById('doomStat').innerText, [...document.querySelectorAll('#doomEp option')].map(o => o.value), document.getElementById('doomMusic').checked]);
  console.log('Status:', JSON.stringify(stat));
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#btnSave')]);
  const romOut = out + '/' + dl.suggestedFilename(); await dl.saveAs(romOut);
  await page.waitForTimeout(300);
  await page.screenshot({ path: out + '/doom_3.png' });
  console.log('ROM:', romOut, 'Fehler:', errors);
  if (ref) { const a = fs.readFileSync(romOut), b = fs.readFileSync(ref); console.log('gleich wie Referenz:', a.equals(b)); }
  // loaded ROM with DOOM: emulator tab offers removal (shared stub)
  await page.setInputFiles('#fileRom', romOut);
  await page.waitForTimeout(1500);
  await page.click('.tab[data-tab="emu"]');
  await page.waitForTimeout(300);
  console.log('Entfernen angeboten:', await page.evaluate(() => !document.getElementById('btnEmuOff').hidden));
  await browser.close();
})();
