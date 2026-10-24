// 1.6.9 test13: start device with DOOM and two emulators in the menu; an emulator is chosen as the main program (star)
// with "start when switched on". DOOM stays at 0x0A0000, the menu's settings name the main program (version 9).
// node test/ui_launch_main_test.js <paradise.bin> <DOOM.WAD> <idl.bin> <tama-go.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, wad, idl, go, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="doom"]');
  await p.setInputFiles('#fileWad', wad);
  await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on') && /E\d/.test(document.getElementById('doomStat').innerText), null, { timeout: 120000 });
  await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileEmu', idl); await p.waitForTimeout(800);
  await p.setInputFiles('#fileApp', go); await p.waitForTimeout(500);
  const rows = async () => p.evaluate(() => [...document.querySelectorAll('#appsList .prow')].map((r) => r.querySelector('.pname').textContent + '|' + r.querySelector('.ptag').textContent + '|' + (r.querySelector('.pstar.on') ? 'main' : '')));
  console.log('Liste:', await rows());
  const rowBtn = (name, sel) => p.evaluate(([n, s]) => { const r = [...document.querySelectorAll('#appsList .prow')].find((x) => x.querySelector('.pname').textContent === n); const k = r && r.querySelector(s); if (k) k.click(); return !!k; }, [name, sel]);
  ok(await rowBtn('TAMA-GO', '.pstar'), 'star of TAMA-GO clickable');
  await p.waitForTimeout(300);
  let l = await rows(); console.log('Liste:', l);
  ok(l.some((x) => x.startsWith('TAMA-GO|') && x.endsWith('|main')) && l.some((x) => x.startsWith('DOOM|') && !x.endsWith('|main')), 'TAMA-GO main, DOOM in the menu');
  await rowBtn('TAMA-GO', '.ptog'); await p.waitForTimeout(300);
  ok(await p.evaluate(() => { const c = [...document.querySelectorAll('#appsList .pdet label.check')].find((x) => /Einschalten/.test(x.textContent)); if (!c) return false; c.querySelector('input').click(); return true; }), 'autostart box at TAMA-GO');
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), fa = F.feldApps(img, fe.stub);
  ok(F.appKind(img) === 1, 'DOOM at 0x0A0000');
  const go1 = fa.apps.find((a) => a.name === 'TAMA-GO');
  ok(fa.v9 && go1 && fa.mainSrc === go1.src && fa.auto, 'menu: main ' + (fa.mainSrc >>> 0).toString(16) + ' auto ' + fa.auto + ' (' + fa.apps.map((a) => a.name).join(', ') + ')');
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="emu"]');
  l = await rows(); console.log('Liste neu:', l);
  ok(l.some((x) => x.startsWith('TAMA-GO|') && x.endsWith('|main')), 'read back: TAMA-GO main');
  const r2 = await Promise.race([p.waitForEvent('download', { timeout: 120000 }).then((d) => 'download'), (async () => { await p.click('#btnSave'); await p.waitForTimeout(30000); return 'none'; })()]);
  console.log('second save:', r2);
  // back to DOOM
  ok(await rowBtn('DOOM', '.pstar'), 'star of DOOM clickable'); await p.waitForTimeout(300);
  const dlp3 = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const d3 = await dlp3; const r3 = out + '/b_' + d3.suggestedFilename(); await d3.saveAs(r3);
  const fa3 = F.feldApps(new Uint8Array(fs.readFileSync(r3)), fe.stub);
  ok(fa3.v9 && fa3.mainSrc === 0, 'DOOM main again');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
