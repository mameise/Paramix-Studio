// Sheet round trip in the browser: get sheet, upload it unchanged (no change),
// upload it edited (change), load Glacier.
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, rom2, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true, locale: 'de-CH' });
  const errors = []; page.on('pageerror', e => errors.push(e.message));
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom); await page.waitForSelector('#work.on');
  await page.click('.tile[data-kind="fig"][data-idx="243"]');
  const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#dDown')]);
  const sheet = out + '/figur_0243.png'; await dl.saveAs(sheet);
  await page.setInputFiles('#fileOne', sheet); await page.waitForTimeout(800);
  const c1 = await page.evaluate(() => window.__paramix.changed.size);
  // edit: color the body row red (all opaque pixels of the first 64 rows)
  const edited = await page.evaluate(async () => {
    const img = document.getElementById('dCanvas');
    const r = await fetch(document.querySelector('#dCanvas').toDataURL()); void r;
    return true;
  });
  void edited;
  const b64 = fs.readFileSync(sheet).toString('base64');
  const editedPath = out + '/figur_0243_rot.png';
  const data = await page.evaluate(async (b64) => {
    const im = new Image(); im.src = 'data:image/png;base64,' + b64; await im.decode();
    const cv = document.createElement('canvas'); cv.width = im.width; cv.height = im.height;
    const g = cv.getContext('2d'); g.drawImage(im, 0, 0);
    const d = g.getImageData(0, 0, cv.width, 64);
    for (let i = 0; i < d.data.length; i += 4) if (d.data[i + 3] > 128 && d.data[i] + d.data[i + 1] + d.data[i + 2] > 200) { d.data[i] = 230; d.data[i + 1] = 40; d.data[i + 2] = 60; }
    g.putImageData(d, 0, 0);
    return cv.toDataURL('image/png').split(',')[1];
  }, b64);
  fs.writeFileSync(editedPath, Buffer.from(data, 'base64'));
  await page.setInputFiles('#fileOne', editedPath); await page.waitForTimeout(1200);
  const c2 = await page.evaluate(() => [...window.__paramix.changed.keys()]);
  await page.screenshot({ path: out + '/rot.png' });
  await page.click('#dClose');
  await page.setInputFiles('#fileRom', rom2); await page.waitForSelector('#work.on'); await page.waitForTimeout(1500);
  const info = await page.evaluate(() => document.getElementById('info').innerText.replace(/\n/g, ' | '));
  await page.screenshot({ path: out + '/glacier.png' });
  console.log('unveraendert hochgeladen -> Aenderungen:', c1, '| bearbeitet -> geaendert:', c2, '|', info, '| Fehler', errors);
  await browser.close();
})();
