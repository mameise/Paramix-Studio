// 1.6.8: a program that does not fit any more is refused when it is chosen, with the KB that are missing.
// node test/ui_room_test.js <paradise.bin> <out_dir> <firmware...>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, out, ...fws] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2000);
  await p.click('.tab[data-tab="feld"]'); if (!(await p.isChecked('#launchOn'))) await p.click('#launchOn'); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="emu"]');
  let refused = '';
  for (const f of fws) {
    await p.evaluate(() => { document.getElementById('toast').textContent = ''; });
    await p.setInputFiles('#fileEmu', f);
    await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on'), null, { timeout: 120000 }); await p.waitForTimeout(1500);
    const tt = await p.textContent('#toast'); console.log(path.basename(f), '->', tt.slice(0, 100));
    if (/keinen Platz/.test(tt)) { refused = tt; break; }
  }
  ok(/es fehlen etwa \d+ KB/.test(refused), 'refused with the KB missing: ' + refused);
  const n = await p.$$eval('#appsList .prow', (l) => l.length);
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const dl = await Promise.race([dlp, p.waitForTimeout(240000).then(() => null)]);
  ok(!!dl, 'the rest still saves (' + n + ' programs)');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
