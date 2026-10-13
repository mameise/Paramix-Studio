// Fields tab in the browser (local only): load ROM, turn on field switch, save, compare with feld_cli.js,
// load the result again (checkbox must be ticked), turn it off and save (must give the original).
// Usage: node test/ui_feld_test.js <rom> <output_dir> [reference.bin]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, out, ref] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (name) => { const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#btnSave')]); const p = out + '/' + name + '_' + dl.suggestedFilename(); await dl.saveAs(p); return p; };
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="feld"]');
  await page.waitForTimeout(300);
  console.log('Status 1:', JSON.stringify(await page.evaluate(() => [document.getElementById('feldStat').innerText, document.getElementById('feldOn').checked, document.getElementById('feldOn').disabled])));
  if (!(await page.isDisabled('#feldOn'))) await page.check('#feldOn');
  await page.check('#bagOn');
  await page.screenshot({ path: out + '/feld_1.png' });
  const a = await save('an');
  console.log('ROM:', a);
  if (ref) console.log('gleich wie Referenz:', fs.readFileSync(a).equals(fs.readFileSync(ref)));
  await page.setInputFiles('#fileRom', a);
  await page.waitForTimeout(1500);
  await page.click('.tab[data-tab="feld"]');
  console.log('Status 2:', JSON.stringify(await page.evaluate(() => [document.getElementById('feldStat').innerText, document.getElementById('feldOn').checked, document.getElementById('bagOn').checked])));
  await page.screenshot({ path: out + '/feld_2.png' });
  if (!(await page.isDisabled('#feldOn'))) await page.uncheck('#feldOn');
  await page.uncheck('#bagOn');
  const b = await save('aus');
  console.log('aus = Original:', fs.readFileSync(b).equals(fs.readFileSync(rom)), 'Fehler:', errors);
  await browser.close();
})();
