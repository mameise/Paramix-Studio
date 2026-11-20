// 1.7.6 SAVE_PACK_ASK + DETECT_TAG: a big P's save of another firmware version (reset vector unknown) into a ROM:
// shown as P's, the Studio asks to pack, after yes saving works. node test/ui_savepack_test.js <paradise.bin> <ps.sav> <out>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, sav, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const g = new Uint8Array(fs.readFileSync(sav)); g[0x400000] ^= 0x10;   // another version
  let seed = 7; const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24;
  let filled = 0; for (let b = 0; b < 128 && filled < 12; b++) if (g.subarray(b << 16, (b + 1) << 16).every((v) => v === 0xFF) && b > 0x10) { for (let i = 0; i < 0x10000; i++) g[(b << 16) + i] = rnd(); filled++; }
  const big = out + '/big.sav'; fs.writeFileSync(big, g);
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  const dialogs = []; page.on('dialog', (d) => { dialogs.push(d.message()); d.accept(); });
  await page.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'en'); } catch (e) {} });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="save"]');
  await page.setInputFiles('#fileEmuSav', big);
  await page.waitForTimeout(1500);
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const st = await page.textContent('#saveStat');
  ok(/P's/.test(st), 'status names P\'s: ' + st);
  ok(dialogs.some((d) => /only fits packed/.test(d)), 'asked to pack: ' + dialogs.join(' | '));
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 180000 }), page.click('#btnSave')]);
  const romOut = out + '/rom_out.bin'; await dl.saveAs(romOut);
  ok(fs.statSync(romOut).size === 16 << 20, 'ROM saved');
  ok(!errors.length, 'no page errors ' + errors.join(' '));
  await browser.close(); process.exit(bad ? 1 : 0);
})();
