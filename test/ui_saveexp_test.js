// 1.6.11 test2: save export and import for every program in the list: Game Boy and Master System (.sav, the cartridge
// RAM), Wolf3D (its saved game), T18Emu (firmware with its save). Imported, written, read back, exported again.
// node test/ui_saveexp_test.js <paradise.bin> <game.gb with battery> <cart.sav> <game.sms> <folder .WL1> <tg18 fw> <tg18 fw with save> <out>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, gb, gsav, sms, wdir, t18, t18s, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const ssav = Buffer.alloc(0x2000); for (let i = 0; i < ssav.length; i++) ssav[i] = (i * 7 + 3) & 255; fs.writeFileSync(out + '/sms.sav', ssav);
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (n) => { const dlp = p.waitForEvent('download', { timeout: 240000 }); await p.click('#btnSave'); const dl = await dlp; const f = out + '/' + n; await dl.saveAs(f); return f; };
  const load = async (f) => { await p.setInputFiles('#fileRom', f); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2500); };
  await load(rom);
  await p.click('.tab[data-tab="feld"]'); await p.click('#bagOn'); await p.waitForTimeout(800);
  await p.click('.tab[data-tab="gb"]'); await p.setInputFiles('#fileGb', gb); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="sms"]'); await p.setInputFiles('#fileSms', sms); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="wolf"]'); await p.setInputFiles('#fileWolf', fs.readdirSync(wdir).filter((n) => /\.WL1$/i.test(n)).map((n) => path.join(wdir, n))); await p.waitForTimeout(4000);
  const r1 = await save('r1.bin'); await load(r1);
  await p.click('.tab[data-tab="save"]');
  const opts = await p.evaluate(() => [...document.getElementById('saveFor').options].map((o) => [o.value, o.textContent]));
  ok(opts.length === 3, 'three programs in the list: ' + opts.map((o) => o[1]).join(', '));
  const pick = async (re) => { const v = opts.find((o) => re.test(o[1])); await p.selectOption('#saveFor', v[0]); await p.waitForTimeout(200); };
  const imp = async (re, f) => { await pick(re); await p.setInputFiles('#fileEmuSav', f); await p.waitForTimeout(1500); return p.evaluate(() => document.getElementById('saveStat').textContent + ' | ' + document.getElementById('toast').textContent); };
  const exp = async (re) => { await pick(re); const dlp = p.waitForEvent('download', { timeout: 60000 }).catch(() => null); await p.click('#btnEmuOut'); const dl = await dlp; if (!dl) return null; const f = out + '/x-' + dl.suggestedFilename(); await dl.saveAs(f); return fs.readFileSync(f); };
  ok(!(await p.evaluate(() => document.getElementById('btnEmuOut').disabled)), 'export button on');
  // fresh: the Game Boy has no save yet
  await pick(/POKEMON|RED/i); await p.click('#btnEmuOut'); await p.waitForTimeout(500);
  ok(/Noch kein Spielstand/.test(await p.evaluate(() => document.getElementById('toast').textContent)), 'no save yet: said');
  console.log('  ' + await imp(/POKEMON|RED/i, gsav));
  console.log('  ' + await imp(/ALEX|KIDD|MASTER/i, out + '/sms.sav'));
  ok(/passt nicht/.test(await imp(/WOLF/i, out + '/sms.sav')), 'a wrong file for Wolf3D refused');
  const r2 = await save('r2.bin'); await load(r2);
  const img = new Uint8Array(fs.readFileSync(r2)), xs = E.appsRead(img, F.feldApps(img, fe.stub).apps);
  const g = xs.find((x) => x.kind === 'gb'), s = xs.find((x) => x.kind === 'sms');
  const gs = fs.readFileSync(gsav);
  ok(g && Buffer.from(g.save.subarray(0x1000, 0x1000 + 0x8000)).equals(gs.subarray(0, 0x8000)) && g.save.subarray(0x9000).every((v) => v === 0xFF), 'Game Boy: cartridge RAM in the ROM, no old state');
  ok(s && Buffer.from(s.save.subarray(0x1000, 0x3000)).equals(ssav), 'Master System: cartridge RAM in the ROM');
  await p.click('.tab[data-tab="save"]');
  ok(Buffer.from(await exp(/POKEMON|RED/i) || []).equals(gs.subarray(0, 0x8000)), 'Game Boy export = the .sav');
  ok(Buffer.from(await exp(/ALEX|KIDD|MASTER/i) || []).equals(ssav), 'Master System export = the .sav (8 KB)');
  // T18Emu as the main program: its firmware with the save in and out
  await load(rom);
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', t18); await p.waitForTimeout(2000);
  const r3 = await save('r3.bin'); await load(r3);
  await p.click('.tab[data-tab="save"]');
  const o2 = await p.evaluate(() => [...document.getElementById('saveFor').options].map((o) => o.textContent));
  ok(o2.length === 1 && /MEETS/.test(o2[0]), 'T18Emu listed: ' + o2.join(', '));
  // a firmware with a save that needs more blocks than are free: refused right away, the ROM stays as it was
  await p.setInputFiles('#fileEmuSav', t18s); await p.waitForTimeout(4000);
  const tt = await p.evaluate(() => document.getElementById('toast').textContent);
  if (/room|Platz/.test(tt)) ok(true, 'too big: refused at once (' + tt + ')');
  // one that fits: the installed firmware with a changed byte in a used block
  const want = E.t18FromRom(new Uint8Array(fs.readFileSync(r3)));
  let at = 0; while (want[at] === 0xFF) at++; want[at + 0x123] ^= 0x5A; fs.writeFileSync(out + '/t18fit.bin', want);
  await p.setInputFiles('#fileEmuSav', out + '/t18fit.bin'); await p.waitForTimeout(4000);
  const r4 = await save('r4.bin');
  const fw = E.t18FromRom(new Uint8Array(fs.readFileSync(r4)));
  ok(fw && Buffer.from(fw).equals(Buffer.from(want)), 'T18Emu: firmware with save in the ROM');
  await load(r4); await p.click('.tab[data-tab="save"]');
  { await p.waitForTimeout(300); const dlp = p.waitForEvent('download', { timeout: 60000 }).catch(() => null); await p.click('#btnEmuOut'); const dl = await dlp; const f = dl && out + '/x-t18.bin'; if (dl) await dl.saveAs(f);
    ok(dl && Buffer.from(fs.readFileSync(f)).equals(Buffer.from(want)), 'T18Emu export = the firmware with save'); }
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
