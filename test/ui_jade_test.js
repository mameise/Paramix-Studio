// Sky as 4th field on Jade Forest in the browser (local only).
// Usage: node test/ui_jade_test.js <jade.bin> <output_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', e => errors.push(e.message));
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="feld"]');
  await page.check('#sixOn');
  console.log('Status 1:', await page.evaluate(() => [document.getElementById('sixOnTxt').innerText, document.getElementById('btnSix').hidden, document.getElementById('feldStat').innerText]));
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 180000 }), page.click('#btnSave')]);
  const p = out + '/' + dl.suggestedFilename(); await dl.saveAs(p);
  await page.setInputFiles('#fileRom', p); await page.waitForTimeout(1500); await page.click('.tab[data-tab="feld"]');
  console.log('Status 2:', p, await page.evaluate(() => document.getElementById('feldStat').innerText));
  console.log('errors:', errors);
  await browser.close();
})();
