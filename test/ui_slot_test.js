// Tab "Save" (1.6.8): choose the save slot for the Paradise export (ROM with menu in saves mode and a stored save).
// Usage: node test/ui_slot_test.js <rom_with_slots> <output_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const SV = require('../src/saves.js');
(async () => {
  const [rom, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="save"]');
  console.log('Platz sichtbar:', !(await page.isHidden('#paraSlotRow')));
  const opts = await page.$$eval('#paraSlot option', (o) => o.map((x) => x.value + '=' + x.textContent));
  console.log('Plätze:', opts.join(' | '));
  const r = new Uint8Array(fs.readFileSync(rom));
  for (const v of ['live', '1', '2']) {
    await page.selectOption('#paraSlot', v);
    const dl = await Promise.race([page.waitForEvent('download', { timeout: 5000 }).catch(() => null), page.click('#btnParaOut').then(() => page.waitForEvent('download', { timeout: 5000 }).catch(() => null))]);
    if (!dl) { await page.waitForTimeout(300); console.log(v, 'kein Download:', await page.textContent('#toast')); continue; }
    const f = out + '/' + dl.suggestedFilename(); await dl.saveAs(f);
    const sv = SV.paraDecode(new Uint8Array(fs.readFileSync(f)));
    const m = sv.pages.get(0xE20000);
    console.log(v, dl.suggestedFilename(), 'Seiten', sv.pages.size, 'Marke', !!m && m[7] === 0x33, 'Einst. gleich ROM', Buffer.compare(Buffer.from(sv.pages.get(0xEFE000)), Buffer.from(r.subarray(0xEFE000, 0xEFF000))) === 0);
  }
  await page.screenshot({ path: out + '/slot.png' });
  console.log('Fehler:', errors);
  await browser.close();
})();
