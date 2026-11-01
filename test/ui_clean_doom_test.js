// ROM without the Paradise with DOOM as a program of the menu (1.7.2 test3 DOOM_FREE): DOOM anywhere, any program can be
// the main one; load again, add one, save again; a start device with DOOM becomes a ROM without the Paradise.
// node test/ui_clean_doom_test.js <paradise.bin> <DOOM.WAD> <tg18.bin> <tama-go.bin> <p2.bin> <start device with DOOM or -> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js'), D = require('../src/doom.js');
(async () => {
  const [rom, wad, tg18, go, p2, para, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message)); p.on('dialog', (d) => d.accept());
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (name) => {
    const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
    for (let i = 0; i < 90; i++) { await p.waitForTimeout(2000); const tt = await p.evaluate(() => document.getElementById('toast').className + ' ' + document.getElementById('toast').textContent); if (/err/.test(tt)) { console.log('toast:', tt); break; } if (!(await p.evaluate(() => document.getElementById('busy').classList.contains('on')))) break; }
    const dl = await dlp; const f = out + '/' + name + dl.suggestedFilename(); await dl.saveAs(f); return f;
  };
  const star = async (nm) => { const rows = p.locator('#appsList .prow'); const n = await rows.count(); for (let i = 0; i < n; i++) if ((await rows.nth(i).locator('.pname').textContent()) === nm) { await rows.nth(i).locator('button.pstar').click(); await p.waitForTimeout(1500); return true; } return false; };
  const doomOf = (img) => { const fa = F.feldApps(img, fe.stub), xs = E.appsRead(img, fa.apps); const d = xs.find((x) => x.kind === 'doom'); return { fa, xs, d, c: d ? E.doomCfg(img, d.off) : null }; };
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);
  await p.click('#cleanOn'); await p.waitForTimeout(500); await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on'), null, { timeout: 60000 });
  ok(await p.isVisible('.tab[data-tab="doom"]'), 'DOOM tab stays');
  await p.click('.tab[data-tab="doom"]');
  await p.setInputFiles('#fileWad', wad);
  await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on') && /E\d/.test(document.getElementById('doomStat').innerText), null, { timeout: 180000 });
  await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileApp', tg18); await p.waitForTimeout(1500);
  await p.setInputFiles('#fileApp', go); await p.waitForTimeout(800);
  console.log('Liste:', await p.textContent('#appsList'));
  ok(await star('TAMA MEETS'), 'star on TAMA MEETS');
  console.log('Liste:', await p.textContent('#appsList'));
  const r1 = await save('');
  const img = new Uint8Array(fs.readFileSync(r1)), ci = F.cleanInfo(img), o = doomOf(img);
  ok(/_doom_E\d_clean\.bin$/.test(r1), 'file name ' + path.basename(r1));
  ok(!!ci && ci.feld === 0xB1000, 'clean ROM, menu at its usual place');
  ok(o.d && o.fa.apps.map((a) => a.name).join(',').includes('DOOM'), 'DOOM is a program of the menu: ' + o.fa.apps.map((a) => a.name).join(', '));
  const t18 = o.xs.find((x) => x.kind === 't18');
  ok(t18 && o.fa.mainSrc === 0x10000000 + t18.off - 0x11000, 'TAMA MEETS is the main program');
  if (o.c) {
    const c = o.c; console.log('DOOM: program 0x' + o.d.off.toString(16), 'code 0x' + c.codeOff.toString(16), 'save 0x' + c.saveOff.toString(16), 'WAD 0x' + (c.romOff).toString(16), 'region 0x' + c.regionOff.toString(16));
    ok(c.codeOff >= 0xA0000 && c.codeOff === o.d.off + 0x10000, 'program part behind its image');
    ok(c.regionOff + c.regionLen <= (c.romOff & ~0x3FFFFF) + 0x800000, 'WAD and level data inside the packed pointer windows');
    ok(img.subarray(c.regionOff, c.regionOff + c.regionLen).every((v) => v === 0xFF), 'level data empty (DOOM makes it)');
    const xf = new Uint8Array(fs.readFileSync(__dirname + '/../doom/doomf_xip.bin')); ok(Buffer.from(img.subarray(c.codeOff, c.codeOff + xf.length)).equals(Buffer.from(xf)), 'program part in place');
  }
  { const c = E.t18Cfg(img, t18.off); const bl = c.map.concat(c.spare).filter((x) => x !== 0xFFFF); ok(!bl.includes(0xFF), 'T18Emu blocks not at the rescue start'); }
  // load again
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="emu"]');
  const l2 = await p.textContent('#appsList'); console.log('Liste:', l2);
  ok(/DOOM/.test(l2) && /TAMA MEETS.{0,20}Erster Eintrag/.test(l2), 'loaded again: DOOM in the list, TAMA MEETS still the main program');
  await p.click('#btnSave'); await p.waitForTimeout(2000);
  ok(/nichts/i.test(await p.textContent('#toast')), 'nothing changed: ' + await p.textContent('#toast'));
  await star('DOOM');
  await p.setInputFiles('#fileApp', p2); await p.waitForTimeout(800);
  const r2 = await save('2_');
  const img2 = new Uint8Array(fs.readFileSync(r2)), o2 = doomOf(img2);
  ok(o2.d && Buffer.from(o2.d.mcu).equals(Buffer.from(o.d.mcu)) && o2.fa.apps.length === 4, 'one more program, DOOM with the same WAD');
  ok(o2.fa.mainSrc === 0x10000000 + o2.d.off - 0x11000, 'DOOM is the main program now');
  // a start device with DOOM (with the Paradise) becomes a ROM without it
  if (para !== '-') {
    await p.setInputFiles('#fileRom', para); await p.waitForTimeout(3000);
    await p.click('.tab[data-tab="feld"]'); await p.click('#cleanOn'); await p.waitForTimeout(500); await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on'), null, { timeout: 60000 });
    await p.click('.tab[data-tab="emu"]'); console.log('Liste:', await p.textContent('#appsList'));
    const r3 = await save('3_'); const img3 = new Uint8Array(fs.readFileSync(r3)), o3 = doomOf(img3), src = new Uint8Array(fs.readFileSync(para));
    const len = src[0x830000] | src[0x830001] << 8 | src[0x830002] << 16 | src[0x830003] << 24;
    ok(F.isClean(img3) && o3.d && Buffer.from(o3.d.mcu).equals(Buffer.from(src.subarray(0x830004, 0x830004 + len))), 'start device to clean: DOOM and its WAD along');
    ok(o3.d && Buffer.from(img3.subarray(o3.c.saveOff, o3.c.saveOff + 0x2000)).equals(Buffer.from(src.subarray(0x10E000, 0x110000))), 'start device to clean: DOOM save along');
    ok(o3.fa.mainSrc === 0x10000000 + o3.d.off - 0x11000, 'start device to clean: DOOM stays the main program');
  }
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
