// T7Emu with the Tamagotchi Friends ROM in the browser: recognised, no figure button, saved with 768 KB, loaded again.
// node test/ui_friends_test.js <paradise.bin> <friends_rom.bin> <out_dir> [tama-go_rom.bin figure.bin]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js');
(async () => {
  const [rom, fr, out, go, fig] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  await page.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.click('.tab[data-tab="emu"]');
  if (go && fig) { await page.setInputFiles('#fileEmu', go); await page.waitForTimeout(300); await page.setInputFiles('#fileT7Fig', [fig]); await page.waitForTimeout(300); }
  await page.setInputFiles('#fileEmu', fr);
  await page.waitForTimeout(500);
  const st = await page.textContent('#emuStat');
  console.log('Status:', st.replace(/\n/g, ' | '));
  ok(/Friends/.test(st), 'Friends recognised');
  ok(await page.isHidden('#btnT7Fig'), 'no figure button');
  await page.screenshot({ path: out + '/fr_1.png' });
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#btnSave')]);
  const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), c1 = E.t7Cfg(img), FR = new Uint8Array(fs.readFileSync(fr));
  ok(c1 && c1.romLen === 0xC0000 && c1.figs.length === 0 && Buffer.from(img.subarray(c1.romOff, c1.romOff + c1.romLen)).equals(Buffer.from(FR)), 'saved: Friends ROM, no figures');
  await page.setInputFiles('#fileRom', r1); await page.waitForTimeout(1500);
  await page.click('.tab[data-tab="emu"]');
  const st2 = await page.textContent('#emuStat');
  console.log('Nach Laden:', st2.replace(/\n/g, ' | '));
  ok(/Friends/.test(st2) && await page.isHidden('#btnT7Fig'), 'loaded again: Friends, no figure button');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await browser.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
