// 1.7.3 test7: clean ROM: "Set the clock at the start" off (menu vflags bit 1), the emulators' clock switch then off and
// greyed; "remember where you left off" off for one emulator.
// node test/ui_cleanclock_test.js <clean rom with an emulator in the menu> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const stub = new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin'));
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const src = new Uint8Array(fs.readFileSync(rom));
  ok(F.isClean(src) && !F.feldApps(src, stub).noClock, 'clean ROM, clock asked at the start');
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="feld"]');
  ok(await p.evaluate(() => !document.getElementById('cleanClockRow').hidden && document.getElementById('cleanClock').checked), 'menu tab: "Uhr beim Start stellen" shown and on');
  await p.evaluate(() => { const c = document.getElementById('cleanClock'); c.checked = false; c.dispatchEvent(new Event('change')); });
  await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(300);
  await p.evaluate(() => { for (const r of document.querySelectorAll('#appsList .prow')) { const t = r.querySelector('.ptog'); if (t) { t.click(); return; } } });
  await p.waitForTimeout(300);
  const st = await p.evaluate(() => { const ls = Array.from(document.querySelectorAll('#appsList .pdet .tools'));
    const clk = ls.find((e) => /Uhr beim Start vom Paradise/.test(e.textContent)), ses = ls.find((e) => /Spielstand beim Verlassen/.test(e.textContent));
    const r = { clk: !!clk, off: clk ? clk.querySelector('input').disabled && !clk.querySelector('input').checked : false, hint: clk ? /Uhr beim Start stellen/.test(clk.textContent) : false, ses: !!ses };
    if (ses) { const c = ses.querySelector('input'); c.checked = false; c.dispatchEvent(new Event('change')); }
    return r; });
  ok(st.clk && st.off && st.hint, 'emulator: clock switch off, greyed, with the hint');
  ok(st.ses, 'emulator: "Spielstand beim Verlassen merken" there');
  const dlp = p.waitForEvent('download', { timeout: 180000 }); await p.click('#btnSave');
  const dl = await dlp.catch(() => null); ok(!!dl, 'something to write');
  if (dl) { const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1); const im = new Uint8Array(fs.readFileSync(r1));
    const fa = F.feldApps(im, stub); ok(fa.noClock === true, 'menu: clock not asked at the start (vflags bit 1)');
    const xs = E.appsRead(im, fa.apps).filter((x) => x.kind !== 'doom' && x.kind !== 'wolf');
    ok(xs.length > 0 && xs.every((x) => x.clock === undefined || x.clock === false), 'no emulator takes the clock from the Paradise');
    ok(xs.some((x) => x.sess === false), 'one emulator without remembering');
    await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000); await p.click('.tab[data-tab="feld"]');
    ok(await p.evaluate(() => !document.getElementById('cleanClock').checked), 'read back: "Uhr beim Start stellen" off'); }
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
