// Emulator tab in the browser: load ROM, pick firmware, save, compare with the Python builder.
// Usage: node test/ui_emu_test.js <rom> <firmware> <output_dir> [reference.bin]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, fw, out, ref] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="emu"]');
  await page.waitForTimeout(300);
  await page.screenshot({ path: out + '/emu_1.png' });
  await page.setInputFiles('#fileEmu', fw);
  await page.waitForTimeout(800);
  await page.screenshot({ path: out + '/emu_2.png' });
  if (process.env.DLC) {
    await page.setInputFiles('#fileDlc', process.env.DLC.split(','));
    await page.waitForTimeout(800);
    console.log('Stores:', await page.evaluate(() => [...document.querySelectorAll('.dlcbox')].map(b => b.innerText.replace(/\n/g, ' | '))));
    // remove the first removable entry, then count again
    await page.click('.dlcrow button');
    await page.waitForTimeout(200);
    console.log('After remove:', await page.evaluate(() => [...document.querySelectorAll('.dlcbox h4')].map(h => h.textContent).join(', ')));
    await page.screenshot({ path: out + '/emu_2b.png' });
  }
  const stat = await page.evaluate(() => document.getElementById('emuStat').innerText);
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#btnSave')]);
  const romOut = out + '/' + dl.suggestedFilename(); await dl.saveAs(romOut);
  await page.waitForTimeout(300);
  await page.screenshot({ path: out + '/emu_3.png' });
  console.log('Status:', stat.replace(/\n/g, ' | '));
  console.log('ROM:', romOut, 'Fehler:', errors);
  if (ref) { const a = fs.readFileSync(romOut), b = fs.readFileSync(ref); console.log('gleich wie Referenz:', a.equals(b)); }
  // loaded ROM with emulator: offer removal
  await page.setInputFiles('#fileRom', romOut);
  await page.waitForTimeout(1500);
  await page.click('.tab[data-tab="emu"]');
  await page.waitForTimeout(300);
  const st2 = await page.evaluate(() => [document.getElementById('emuStat').innerText, document.getElementById('btnEmuOff').hidden]);
  console.log('nach Laden:', JSON.stringify(st2));
  await page.screenshot({ path: out + '/emu_4.png' });
  await browser.close();
})();
