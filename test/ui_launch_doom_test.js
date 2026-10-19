// Start device with DOOM and three emulators (1.6.8): DOOM at 0x0A0000, T18Emu of the Emulator tab, TamaEmu and T7Emu
// as further programs. Checks that nothing overlaps.
// node test/ui_launch_doom_test.js <paradise.bin> <DOOM.WAD> <tg18.bin> <idl.bin> <tama-go.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const C = require('../src/core.js'), E = require('../src/emu.js'), F = require('../src/feld.js'), D = require('../src/doom.js');
(async () => {
  const [rom, wad, tg18, idl, go, out] = process.argv.slice(2);
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
  await p.setInputFiles('#fileEmu', tg18); await p.waitForTimeout(500);
  if (idl !== '-') { await p.setInputFiles('#fileApp', idl); await p.waitForTimeout(800); }
  if (go !== '-') { await p.setInputFiles('#fileApp', go); await p.waitForTimeout(300); }
  console.log('Liste:', await p.textContent('#appsList'));
  console.log('Plan:', (await p.textContent('#plan')).slice(0, 400));
  p.on('console', (m) => { if (m.type() === 'error') console.log('console:', m.text()); });
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  for (let i = 0; i < 60; i++) { await p.waitForTimeout(2000); const tt = await p.evaluate(() => document.getElementById('toast').className + ' ' + document.getElementById('toast').textContent); if (/err/.test(tt)) { console.log('toast:', tt); break; } if (!(await p.evaluate(() => document.getElementById('busy').classList.contains('on')))) break; }
  const dl = await dlp;
  const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), fa = F.feldApps(img, fe.stub), ff = F.feldFind(img, fe.stub);
  ok(F.appKind(img) === 1, 'DOOM at 0x0A0000');
  ok(fa.apps.map((a) => a.name).join(',') === (process.env.EXPECT || 'TAMA MEETS,ID L,TAMA-GO'), 'programs: DOOM + ' + fa.apps.map((a) => a.name).join(', '));
  console.log('menu at 0x' + ff.off.toString(16));
  // who uses what
  const used = [];
  const add = (a, z, who) => used.push([a, z, who]);
  add(0xA0000, 0xB0000, 'doom app'); add(0xB0000, 0xB1000, 'stub'); add(0xB1000, 0xB1000 + D.embedded ? 0 : 0, '');
  add(ff.off, ff.off + 0x7000, 'menu'); add(0x10E000, 0x110000, 'doom save'); add(0x830000, 0xD48000, 'doom wad/region');
  for (const a of E.appsRead(img, fa.apps)) {
    const c = E.extraCfg(img, a.off);
    if (c.kind === 't7') add(a.off, a.off + 0x20000 + ((c.romLen + 0xFFFF) & ~0xFFFF), 't7');
    if (c.kind === 'tama') { add(c.guestOff, c.spareEnd, 'tama guest'); add(a.off, a.off + 0x10000 + 0x50000, 'tama img+state'); ok(c.saveOff === a.off + 0x10000 && c.guestOff % 0x40000 === 0, 'TamaEmu layout guest 0x' + c.guestOff.toString(16) + ' state 0x' + c.saveOff.toString(16)); }
    if (c.kind === 't18') { for (const k of c.map.concat(c.spare)) if (k !== 0xFFFF) add(k << 16, (k + 1) << 16, 't18 block ' + k.toString(16)); add(a.off, a.off + 0x10000 + 0x30000, 't18 img+save'); add(0xF0000, 0x100000, 't18 xip'); if (c.flags & 4) add(0xFD0000, 0xFF0000, 't18 hot'); ok(c.saveOff === a.off + 0x10000, 'T18Emu save 0x' + c.saveOff.toString(16) + ' hot ' + !!(c.flags & 4)); }
  }
  const ctx = C.openRom(img); add(0x111000, ctx.assetEnd, 'pictures');
  let ov = 0;
  for (let i = 0; i < used.length; i++) for (let j = i + 1; j < used.length; j++) { const [a, z, w] = used[i], [a2, z2, w2] = used[j]; if (a < z2 && a2 < z && w && w2) { ov++; console.log('  overlap', w, w2, a.toString(16), a2.toString(16)); } }
  ok(!ov, 'no overlaps (' + used.length + ' areas)');
  if (fa.apps.some((a) => a.name === 'TAMA MEETS')) ok(ff.off >= 0x100000 || ff.off + 0x7000 <= 0xF0000, 'menu clear of the T18 flash part');
  // load again, save again: the same ROM (nothing to do)
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="emu"]');
  console.log('Liste neu:', await p.textContent('#appsList'));
  const toast = await Promise.race([p.waitForEvent('download', { timeout: 120000 }).then((d) => 'download ' + d.suggestedFilename()), (async () => { await p.click('#btnSave'); await p.waitForTimeout(60000); return 'none'; })()]);
  console.log('second save:', toast);
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
