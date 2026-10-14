// Six fields in the browser (local only): load Tropical/Glacier, tick "All 6 fields",
// pick a Land/Water/Sky ROM, save, load the result again.
// Usage: node test/ui_six_test.js <tropical.bin> <land.bin> <output_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, land, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (name) => { const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 180000 }), page.click('#btnSave')]); const p = out + '/' + name + '_' + dl.suggestedFilename(); await dl.saveAs(p); return p; };
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="feld"]');
  await page.waitForTimeout(300);
  console.log('row visible:', await page.isVisible('#sixRow'));
  const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.check('#sixOn')]);
  await fc.setFiles(land);
  await page.waitForTimeout(500);
  await page.check('#bagOn');
  console.log('Status 1:', await page.evaluate(() => document.getElementById('feldStat').innerText));
  console.log('Plan:', await page.evaluate(() => document.getElementById('plan').innerText.replace(/\s+/g, ' ').slice(0, 300)));
  await page.screenshot({ path: out + '/six_1.png', fullPage: true });
  const a = await save('six');
  console.log('ROM:', a);
  await page.setInputFiles('#fileRom', a);
  await page.waitForTimeout(2000);
  await page.click('.tab[data-tab="feld"]');
  console.log('Status 2:', JSON.stringify(await page.evaluate(() => [document.getElementById('feldStat').innerText, document.getElementById('sixOn').checked, document.getElementById('sixOn').disabled])));
  await page.screenshot({ path: out + '/six_2.png', fullPage: true });
  console.log('errors:', errors);
  await browser.close();
})();
