// 1.6.8: which program is the main one (stars), with and without DOOM, start device and normal ROM.
// Every flow is saved and loaded again, so no state ends in a loop.
// node test/ui_main_test.js <paradise.bin> <DOOM.WAD> <idl.bin> <friends.bin> <tama-go.bin> <p2.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, wad, idl, fr, go, p2, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const list = async () => (await p.$$eval('#appsList .prow', (l) => l.map((r) => r.querySelector('.pname').textContent + (r.classList.contains('gone') ? '(weg)' : '') + [...r.querySelectorAll('.pstar')].map((s) => (s.textContent === '★' ? '*' : s.disabled ? '-' : '+')).join('')))).join(' ');
  const load = async (f, launch) => {
    await p.setInputFiles('#fileRom', f); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2500);
    if (launch && !(await p.isChecked('#launchOn'))) { await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500); }
    await p.click('.tab[data-tab="emu"]');
  };
  const pick = async (f, ms = 1500) => { await p.setInputFiles('#fileEmu', f); await p.waitForTimeout(ms); };
  const doom = async () => {
    await p.click('.tab[data-tab="doom"]'); await p.setInputFiles('#fileWad', wad);
    await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on') && /E\d/.test(document.getElementById('doomStat').innerText), null, { timeout: 120000 });
    await p.click('.tab[data-tab="emu"]');
  };
  const btn = async (name, txt) => { const r = p.locator('#appsList .prow').filter({ has: p.locator('.pname', { hasText: new RegExp('^' + name + '$') }) }).first(); await r.locator('button', { hasText: txt }).first().click(); await p.waitForTimeout(1500); };
  const save = async (n) => {
    const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
    await p.waitForTimeout(100); const dl = await Promise.race([dlp, (async () => { for (let i = 0; i < 300; i++) { await p.waitForTimeout(1000); const tt = await p.evaluate(() => document.getElementById('toast').className + ' ' + document.getElementById('toast').textContent); if (/err/.test(tt)) { console.log('toast:', tt); return null; } } return null; })()]).catch(() => null); if (!dl) return null;
    const f = out + '/' + n; await dl.saveAs(f); return f;
  };
  const info = (f) => { const img = new Uint8Array(fs.readFileSync(f)); const fa = F.feldApps(img, fe.stub); return { kind: F.appKind(img), type: F.emuType(img), apps: fa.apps.map((a) => a.name).join(','), name0: fa.name0, auto: (() => { const es = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin')), ff = F.feldFind(img, fe.stub), l = [[0xB0000, es], [0xB0000, fe.stub]]; if (ff) l.push([ff.stubOff, fe.stub]); return E.autoGet(img, l); })() }; };

  // 1) start device: DOOM main, iD L in the menu
  await load(rom, true); await doom(); await pick(idl);
  ok(await list() === 'DOOM* ID L-', '1 start device with DOOM: ' + await list());
  const r1 = await save('m1.bin'); const i1 = info(r1);
  ok(i1.kind === 1 && i1.apps === 'ID L', '1 saved: DOOM at 0x0A0000, menu ' + i1.apps);
  // 2) loaded again, DOOM out, star on iD L: iD L main, DOOM stays out
  await load(r1);
  ok(await list() === 'DOOM* ID L-', '2 loaded: ' + await list());
  await btn('DOOM', '×');
  ok(await list() === 'DOOM(weg) ID L+', '2 DOOM taken out: ' + await list());
  await btn('ID L', '☆');
  ok(await list() === 'DOOM(weg) ID L*', '2 iD L main: ' + await list());
  const r2 = await save('m2.bin'); const i2 = info(r2);
  ok(i2.kind === 2 && i2.type === 'tama' && i2.apps === '', '2 saved: iD L at 0x0A0000, no DOOM, menu empty (' + JSON.stringify(i2) + ')');
  await load(r2);
  ok(await list() === 'ID L*', '2 loaded again: ' + await list());
  // 3) DOOM out and back
  await load(r1); await btn('DOOM', '×'); await btn('DOOM', '↺');
  ok(await list() === 'DOOM* ID L-', '3 DOOM back: ' + await list());
  // 4) start device without DOOM: Friends, Tama-Go, iD L; star on iD L
  await load(rom, true); await pick(fr, 500); await pick(go, 500); await pick(idl);
  ok(await list() === 'FRIENDS* TAMA-GO+ ID L+', '4 start device: ' + await list());
  await btn('ID L', '☆');
  ok(await list() === 'ID L* FRIENDS+ TAMA-GO+', '4 iD L main: ' + await list());
  const r4 = await save('m4.bin'); const i4 = info(r4);
  ok(i4.kind === 2 && i4.type === 'tama' && i4.apps === 'FRIENDS,TAMA-GO', '4 saved: ' + JSON.stringify(i4));
  await load(r4);
  ok(await list() === 'ID L* FRIENDS+ TAMA-GO+', '4 loaded again: ' + await list());
  await pick(idl, 800);
  ok(/schon dabei/.test(await p.textContent('#toast')) && await list() === 'ID L* FRIENDS+ TAMA-GO+', '4 iD L twice refused');
  // 5) normal ROM: Friends, then iD L (only main): iD L main, Friends to the menu; P2 to the menu
  await load(rom); await pick(fr, 500); await pick(idl, 2500); await pick(p2, 800);
  ok(await list() === 'ID L* FRIENDS- TAMA P2-', '5 normal ROM: ' + await list());
  // next to an 8 MB colour Tama nothing else fits into a normal ROM; Friends with P2 does
  await load(rom); await pick(fr, 500); await pick(p2, 800);
  ok(await list() === 'FRIENDS* TAMA P2+', '5 Friends and P2: ' + await list());
  await p.click('.tab[data-tab="feld"]'); if (!(await p.isChecked('#feldOn'))) await p.click('#feldOn'); await p.waitForTimeout(300); await p.click('.tab[data-tab="emu"]');
  const r5 = await save('m5.bin'); const i5 = r5 ? info(r5) : {};
  ok(i5.kind === 2 && i5.type === 't7' && i5.apps === 'TAMA P2', '5 saved: ' + JSON.stringify(i5));
  // 6) normal ROM with DOOM: DOOM main with its autostart switch
  await load(rom); await doom();
  ok(await list() === 'DOOM*', '6 normal ROM with DOOM: ' + await list());
  await p.locator('#appsList .prow .ptog').first().click(); await p.waitForTimeout(300);
  await p.locator('.pdet input[type=checkbox]').first().check(); await p.waitForTimeout(300);
  const r6 = await save('m6.bin'); const i6 = r6 ? info(r6) : {};
  ok(i6.kind === 1 && i6.auto === true, '6 saved: DOOM starts when switched on (' + JSON.stringify(i6) + ')');
  // 7) that ROM, iD L chosen: iD L replaces DOOM, ↺ brings DOOM back
  await load(r6); await pick(idl);
  ok(await list() === 'DOOM(weg) ID L*', '7 iD L instead of DOOM: ' + await list());
  await btn('DOOM', '↺');
  ok(await list() === 'DOOM*', '7 DOOM back: ' + await list());
  // 8) normal ROM: P2 as the main program with autostart, Friends to the menu, then swapped
  await load(rom); await pick(p2, 800);
  ok(await list() === 'TAMA P2*', '8 P2 main: ' + await list());
  await p.locator('#appsList .prow .ptog').first().click(); await p.waitForTimeout(300);
  await p.locator('.pdet input[type=checkbox]').first().check(); await p.waitForTimeout(300);
  await pick(fr, 800);
  ok(await list() === 'TAMA P2* FRIENDS+', '8 Friends to the menu: ' + await list());
  await p.click('.tab[data-tab="feld"]'); if (!(await p.isChecked('#feldOn'))) await p.click('#feldOn'); await p.waitForTimeout(300); await p.click('.tab[data-tab="emu"]');
  const r8 = await save('m8.bin'); const i8 = r8 ? info(r8) : {};
  ok(i8.kind === 2 && i8.type === 'v1' && i8.auto === true && i8.apps === 'FRIENDS', '8 saved: ' + JSON.stringify(i8));
  if (r8) fs.copyFileSync(r8, out + '/v1main.bin');
  await load(r8);
  ok(await list() === 'TAMA P2* FRIENDS+', '8 loaded again: ' + await list());
  await btn('FRIENDS', '☆');
  ok(await list() === 'FRIENDS* TAMA P2+', '8 Friends main, P2 to the menu: ' + await list());
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
