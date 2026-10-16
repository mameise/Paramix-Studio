// T18Emu in the browser: TG18 firmware into a Paradise ROM, save, load again (status), remove.
// node test/ui_t18_test.js <paradise.bin> <tg18_firmware.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, fw, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="emu"]');
  await page.setInputFiles('#fileEmu', fw);
  await page.waitForTimeout(800);
  console.log('Status:', (await page.textContent('#emuStat')).replace(/\n/g, ' | '));
  await page.screenshot({ path: out + '/t18_1.png' });
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#btnSave')]);
  const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), c = E.t18Cfg(img);
  console.log('ROM', path.basename(r1), 'type', F.emuType(img), 'blocks', c && c.map.filter((b) => b !== 0xFFFF).length, 'spares', c && c.spare.length);
  await page.setInputFiles('#fileRom', r1); await page.waitForTimeout(1500);
  await page.click('.tab[data-tab="emu"]');
  console.log('Status after loading:', (await page.textContent('#emuStat')).replace(/\n/g, ' | '));
  await page.screenshot({ path: out + '/t18_2.png' });
  console.log(errors.length ? 'page errors: ' + errors.join('; ') : 'no page errors');
  await browser.close();
})();
