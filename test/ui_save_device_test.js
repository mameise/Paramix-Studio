// Save games in the browser against the emulated Paradise (UART bridge, see ui_device_test.js).
// node test/ui_save_device_test.js <bridge-binary> <firmware.bin> <out-dir>
const { chromium } = require('playwright');
const { spawn } = require('child_process');
const fs = require('fs'), path = require('path');
const SV = require('../src/saves.js');
(async () => {
  const [bridge, fw, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const br = spawn(bridge, [fw, out + '/device_flash.bin'], { stdio: ['pipe', 'pipe', 'inherit'] });
  await new Promise((r) => setTimeout(r, 15000));
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.exposeFunction('__tx', (a) => new Promise((r) => br.stdin.write(Buffer.from(a), r)));
  await page.addInitScript(() => {
    const q = [], waiting = [];
    window.__rx = (a) => { const v = new Uint8Array(a); if (waiting.length) waiting.shift()({ value: v, done: false }); else q.push(v); };
    const port = { open: async () => {}, close: async () => {},
      writable: { getWriter: () => ({ write: (d) => window.__tx(Array.from(d)), releaseLock() {} }) },
      readable: { getReader: () => ({ read: () => q.length ? Promise.resolve({ value: q.shift(), done: false }) : new Promise((r) => waiting.push(r)), cancel: async () => {}, releaseLock() {} }) } };
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port } });
  });
  br.stdout.on('data', (d) => page.evaluate((a) => window.__rx(a), Array.from(d)).catch(() => {}));
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.click('[data-lang="en"]');
  await page.click('#btnDevice');
  await page.evaluate(() => { const c = window.ParamixFlash.connect; window.ParamixFlash.connect = (io, img, o, s) => c(io, img, { timeScale: 20 }, s); });
  await page.click('#btnConn');
  await page.waitForFunction(() => !document.getElementById('btnSaveRead').disabled, null, { timeout: 400000 });
  console.log('info:', await page.textContent('#devInfo'));
  // back up
  if (process.env.ROM) { await page.setInputFiles('#fileRom', process.env.ROM); await page.waitForSelector('#work.on', { timeout: 60000 }); }
  const dls = []; page.on('download', (d) => dls.push(d));
  await page.click('#btnSaveRead');
  await page.waitForFunction(() => /Backed up|Error/.test(document.getElementById('devStat').textContent), null, { timeout: 900000 });
  for (let t = 0; t < 60 && dls.length < (process.env.ROM ? 2 : 1); t++) await page.waitForTimeout(500);
  for (const d of dls) await d.saveAs(out + '/' + d.suggestedFilename());
  const d1 = dls[0]; const f1 = out + '/' + d1.suggestedFilename();
  if (dls[1]) { const g = fs.readFileSync(out + '/' + dls[1].suggestedFilename()); const fw = fs.readFileSync(process.env.GUEST); console.log('sav', dls[1].suggestedFilename(), g.length, 'equals firmware:', Buffer.compare(g.subarray(0, fw.length), fw) === 0); }
  await page.waitForFunction(() => !document.getElementById('btnSaveRead').disabled, null, { timeout: 300000 });
  console.log('backup:', d1.suggestedFilename(), '|', await page.textContent('#devStat'));
  const sv = SV.paraDecode(new Uint8Array(fs.readFileSync(f1)));
  console.log('pages', sv.pages.size, 'counter', sv.counter, 'edition', sv.edition);
  // change one page and write it back
  const p = 0xE20000; sv.pages.set(p, new Uint8Array(0x1000).fill(0x33));
  const f2 = out + '/changed.tamasave'; fs.writeFileSync(f2, SV.paraEncode(sv.pages, sv.edition, { counter: sv.counter, ctrl: 0xE0 }));
  page.on('dialog', (dl) => { console.log('dialog:', dl.message().split('\n')[0]); dl.accept(); });
  await page.setInputFiles('#fileSave', f2);
  await page.waitForFunction(() => /Done|Error/.test(document.getElementById('devStat').textContent), null, { timeout: 300000 });
  console.log('write:', await page.textContent('#devStat'));
  // a save of another edition is refused
  const f3 = out + '/other.tamasave'; fs.writeFileSync(f3, SV.paraEncode(sv.pages, sv.edition === 1 ? 2 : 1, null));
  await page.setInputFiles('#fileSave', f3); await page.waitForTimeout(500);
  console.log('other edition:', await page.textContent('#devStat'));
  await page.screenshot({ path: out + '/save_dev.png' });
  br.stdin.end(); await new Promise((r) => br.on('exit', r));
  const fl = fs.readFileSync(out + '/device_flash.bin');
  console.log('page on the device:', fl[p] === 0x33 && fl[p + 0xFFF] === 0x33, 'errors:', errors);
  await browser.close();
})();
