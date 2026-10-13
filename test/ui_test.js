// UI in a real browser: load ROM, get sheet, upload sheet, insert card character, save.
// Usage: node test/ui_test.js <rom> <card> <output_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, dim, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.screenshot({ path: out + '/1_start.png' });
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.waitForTimeout(1500);
  await page.screenshot({ path: out + '/2_figuren.png' });
  // open baby figure 237 (or first after egg)
  await page.click('.tile[data-kind="fig"][data-idx="237"]');
  await page.waitForTimeout(500);
  await page.screenshot({ path: out + '/3_drawer.png' });
  const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#dDown')]);
  const sheet = out + '/' + dl.suggestedFilename(); await dl.saveAs(sheet);
  await page.click('#dClose');
  // card tab
  await page.click('.tab[data-tab="dim"]');
  await page.setInputFiles('#fileDim', dim);
  await page.waitForTimeout(800);
  await page.screenshot({ path: out + '/4_dim.png' });
  // insert card character into the default target, and egg
  await page.selectOption('#dimArea select >> nth=0', process.env.DIGI || '2');
  if (process.env.ZIEL) await page.selectOption('#dimArea select >> nth=1', process.env.ZIEL);
  await page.click('#dimArea .btn.primary');
  await page.click('#dimArea .btn:not(.primary)');
  await page.waitForTimeout(500);
  // all sprites tab view
  await page.click('.tab[data-tab="all"]');
  await page.waitForTimeout(800);
  await page.screenshot({ path: out + '/5_alle.png' });
  await page.click('.tab[data-tab="figs"]');
  await page.click('.tile[data-kind="fig"][data-idx="' + (process.env.ZIEL || '243') + '"]');
  await page.waitForTimeout(400);
  await page.screenshot({ path: out + '/6_drawer_kartenfigur.png' });
  await page.click('#dClose');
  const [dl2] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#btnSave')]);
  const romOut = out + '/' + dl2.suggestedFilename(); await dl2.saveAs(romOut);
  await page.waitForTimeout(300);
  await page.screenshot({ path: out + '/7_saved.png' });
  const info = await page.evaluate(() => ({ changed: window.__paramix.changed.size, info: document.getElementById('info').innerText, toast: document.getElementById('toast').innerText }));
  console.log(JSON.stringify(info), 'sheet', sheet, 'rom', romOut, 'errors', errors);
  await browser.close();
})();
