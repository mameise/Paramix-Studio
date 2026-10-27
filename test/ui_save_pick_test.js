// test23: a save file goes to the program of its kind (whatever the list shows), also to an emulator added but not yet
// saved; T18Emu also takes the save of the T18Emu app (flash from 0x570000, or its .zip with flash.bin).
// node test/ui_save_pick_test.js <land.bin> <tamago.bin> <t7 save.bin> <colour Tama firmware> <its .sav or - (made from the firmware)> <tg18 firmware> <out>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js'), SV = require('../src/saves.js'), C = require('../src/core.js');
(async () => {
  const [land, go, t7s, psfw, pssav, tg18, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = [], dialogs = []; p.on('pageerror', (e) => errors.push(e.message)); p.on('dialog', (d) => { dialogs.push(d.message()); d.accept(); });
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (n) => { const dlp = p.waitForEvent('download', { timeout: 240000 }).catch(() => null); await p.click('#btnSave'); const dl = await dlp; if (!dl) { console.log('no download: ' + await p.evaluate(() => document.getElementById('toast').textContent)); process.exit(1); } const f = out + '/' + n; await dl.saveAs(f); return f; };
  const load = async (f) => { await p.setInputFiles('#fileRom', f); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2500); };
  const stat = () => p.evaluate(() => document.getElementById('saveStat').textContent + ' | ' + document.getElementById('toast').textContent);
  const imp = async (f) => { await p.click('.tab[data-tab="save"]'); await p.setInputFiles('#fileEmuSav', f); await p.waitForTimeout(3000); return stat(); };
  const opts = () => p.evaluate(() => [...document.getElementById('saveFor').options].map((o) => [o.value, o.textContent]));
  const T7 = new Uint8Array(fs.readFileSync(t7s));
  let PS; if (pssav === '-') { PS = new Uint8Array(fs.readFileSync(psfw)); let k = 0x100000; while (PS[k] === 0xFF) k++; PS[k + 3] ^= 0x5A; } else PS = new Uint8Array(fs.readFileSync(pssav));
  const psFile = out + '/tama.sav'; fs.writeFileSync(psFile, PS);

  // 1. T7Emu added in this session, not saved yet: its save is taken (before: "T7Emu has to be in the ROM")
  await load(land);
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', go); await p.waitForTimeout(2000);
  let s = await imp(t7s); ok(/T7Emu-Spielstand übernommen/.test(s), 'T7Emu just added: save taken (' + s.slice(0, 60) + ')');
  const r1 = await save('r1.bin'); let img = new Uint8Array(fs.readFileSync(r1));
  ok(Buffer.from(SV.t7SaveOut(img) || []).equals(Buffer.from(T7)), 'T7Emu save in the ROM');

  // 2. start device, colour Tama main, T7Emu in the menu: the list on one program, the file of the other goes to its own
  await load(land);
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);   // start device: room for both
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', psfw); await p.waitForTimeout(3000);
  await p.setInputFiles('#fileApp', go); await p.waitForTimeout(1000);
  const r2 = await save('r2.bin'); await load(r2);
  await p.click('.tab[data-tab="save"]'); const o = await opts(); ok(o.length === 2, 'two programs: ' + o.map((x) => x[1]).join(', '));
  await p.selectOption('#saveFor', 'm'); await p.waitForTimeout(200);
  const T7b = new Uint8Array(T7); T7b[100] ^= 0x33; fs.writeFileSync(out + '/t7b.bin', T7b);
  s = await imp(out + '/t7b.bin'); ok(/übernommen/.test(s), 'T7Emu save with the colour Tama chosen: to the T7Emu in the menu (' + s.slice(0, 60) + ')');
  await p.selectOption('#saveFor', o.find((x) => x[0] !== 'm')[0]); await p.waitForTimeout(200);
  s = await imp(psFile); ok(/übernommen/.test(s) && !dialogs.length, 'colour Tama save with T7Emu chosen: to the colour Tama, no question (' + s.slice(0, 60) + ')');
  const r3 = await save('r3.bin'); img = new Uint8Array(fs.readFileSync(r3));
  const xs = E.appsRead(img, F.feldApps(img, fe.stub).apps), x = xs.find((y) => E.extraKind(y) === 't7');
  ok(x && Buffer.from(SV.t7SaveOut(img, x.saveOff) || []).equals(Buffer.from(T7b)), 'T7Emu save in the ROM (in the menu)');
  const cfg = SV.emuCfg(img.subarray(0xA0000, 0xA1000));
  const g = cfg && await SV.emuGuestAsync((a, n) => img.subarray(a, a + n), cfg);
  ok(g && Buffer.from(g).equals(Buffer.from(PS)), 'colour Tama save in the ROM');

  // 3. T18Emu: the T18Emu app's save (flash from 0x570000) and its .zip
  await load(land);
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', tg18); await p.waitForTimeout(2500);
  const fw0 = new Uint8Array(fs.readFileSync(tg18)), part = fw0.slice(0x570000); part[0x5E0100 - 0x570000] ^= 0x5A; part[0x700000 - 0x570000] ^= 0x11;
  fs.writeFileSync(out + '/t18emu.sav', part);
  s = await imp(out + '/t18emu.sav'); ok(/übernommen/.test(s), 'T18Emu just added: app save taken (' + s.slice(0, 60) + ')');
  const r4 = await save('r4.bin'); img = new Uint8Array(fs.readFileSync(r4));
  let fw = E.t18FromRom(img), want = new Uint8Array(fw0); want.set(part, 0x570000);
  ok(fw && Buffer.from(fw).equals(Buffer.from(want)), 'T18Emu: app save in the firmware of the ROM');
  await load(r4);
  const flash = new Uint8Array(want); flash[0x600010] ^= 0x77;
  fs.writeFileSync(out + '/t18.zip', C.zipBuild([{ name: 'flash.bin', data: flash }, { name: 'rom.txt', data: new TextEncoder().encode('x') }]));
  s = await imp(out + '/t18.zip'); ok(/übernommen/.test(s), 'T18Emu: app .zip taken (' + s.slice(0, 60) + ')');
  const r5 = await save('r5.bin'); fw = E.t18FromRom(new Uint8Array(fs.readFileSync(r5)));
  ok(fw && Buffer.from(fw).equals(Buffer.from(flash)), 'T18Emu: .zip save in the ROM');
  // export goes back to the app: the whole flash (the app takes 8 MB)
  await p.click('.tab[data-tab="save"]');
  await load(r5); await p.click('.tab[data-tab="save"]');
  { const dlp = p.waitForEvent('download', { timeout: 60000 }).catch(() => null); await p.click('#btnEmuOut'); const dl = await dlp; const f = dl && out + '/x-t18.bin'; if (dl) await dl.saveAs(f);
    ok(dl && fs.statSync(f).size === 0x800000 && Buffer.from(fs.readFileSync(f)).equals(Buffer.from(flash)), 'T18Emu export: 8 MB as the app imports it'); }
  // 4. no T7Emu anywhere: a clear message
  await load(land); s = await imp(t7s); ok(/Tab Emulator/.test(s), 'no T7Emu: says where to add it (' + s.slice(0, 80) + ')');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  ok(!dialogs.length, 'no questions ' + dialogs.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
