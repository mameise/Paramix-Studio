// Device tab in the browser against the emulated Paradise (UART bridge of the
// Paradise system emulator, a separate project; see flash_bridge_test.js).
// node test/ui_device_test.js <bridge-binary> <firmware.bin> <out-dir>
// Web Serial is replaced by a small stand-in that forwards bytes to the bridge.
const { chromium } = require('playwright');
const { spawn } = require('child_process');
const fs = require('fs'), path = require('path');
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
    const port = {
      open: async () => {},
      close: async () => {},
      writable: { getWriter: () => ({ write: (d) => window.__tx(Array.from(d)), releaseLock() {} }) },
      readable: { getReader: () => ({ read: () => q.length ? Promise.resolve({ value: q.shift(), done: false }) : new Promise((r) => waiting.push(r)), cancel: async () => {}, releaseLock() {} }) },
    };
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port } });
  });
  br.stdout.on('data', (d) => page.evaluate((a) => window.__rx(a), Array.from(d)).catch(() => {}));
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.click('[data-lang="en"]');
  await page.click('#btnDevice');
  await page.screenshot({ path: out + '/dev_1.png' });
  // patience for the emulator: stretch the timeouts
  await page.evaluate(() => { const c = window.ParamixFlash.connect; window.ParamixFlash.connect = (io, img, o, s) => c(io, img, { timeScale: 20 }, s); });
  await page.click('#btnConn');
  await page.waitForFunction(() => !document.getElementById('btnDump').disabled, null, { timeout: 400000 });
  console.log('info:', await page.textContent('#devInfo'), '|', await page.textContent('#devStat'));
  await page.screenshot({ path: out + '/dev_2.png' });
  if (process.env.DUMP) {
    // read the whole device: the file is downloaded and opened in the Studio
    const t0 = Date.now();
    const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 3600000 }), page.click('#btnDump')]);
    const f = out + '/' + dl.suggestedFilename(); await dl.saveAs(f);
    await page.waitForSelector('#work.on', { timeout: 60000 });
    console.log('dump:', f, ((Date.now() - t0) / 1000 | 0) + ' s', '| equals device image:', fs.readFileSync(f).subarray(0, 0xD00000).equals(fs.readFileSync(fw).subarray(0, 0xD00000)), '|', await page.textContent('#devStat'));
  }
  // open the same firmware image as ROM, change nothing, write with keep saves
  await page.setInputFiles('#fileRom', fw);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  page.on('dialog', (d) => d.accept());
  await page.click('#btnWrite');
  await page.waitForTimeout(3000);
  await page.screenshot({ path: out + '/dev_3.png' });
  await page.waitForFunction(() => !document.getElementById('btnWrite').disabled, null, { timeout: 600000 });
  console.log('write:', (await page.textContent('#devStat')).replace(/\n/g, ' | '));
  await page.screenshot({ path: out + '/dev_4.png' });
  console.log('errors:', errors);
  await browser.close(); br.stdin.end();
})();
