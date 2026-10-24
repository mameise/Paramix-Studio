// 1.6.8 (test2): T18Emu as a further program in the menu of a start device stays where it is when the ROM is saved
// again (items, settings, another program removed). Its save grows while it runs, so placing it anew could fail
// with "too little room" on a full ROM.
// node test/ui_t18keep_test.js <paradise.bin> <v1_rom.bin> <second_v1_rom.bin> <tg18_with_save.bin> <item_dir> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, v1, v1b, tg18, idir, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const stub = new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin'));
  const files = fs.readdirSync(idir).filter((f) => /^tg18_.*\.txt$/.test(f)).slice(0, 6).map((f) => path.join(idir, f));
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const apps = (r) => E.appsRead(r, F.feldApps(r, stub).apps);
  const t18of = (r) => apps(r).find((x) => x.kind === 't18');
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (n) => { const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave'); const dl = await Promise.race([dlp, p.waitForTimeout(240000).then(() => null)]); if (!dl) { console.log('toast:', await p.textContent('#toast')); return null; } const f = out + '/' + n; await dl.saveAs(f); return new Uint8Array(fs.readFileSync(f)); };
  const load = async (f) => { await p.setInputFiles('#fileRom', f); await p.waitForTimeout(3000); await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(500); };
  const open = async (name) => { const row = p.locator('#appsList .prow', { hasText: name }); await row.locator('.ptog').click(); await p.waitForTimeout(500); };
  // start device like Bernd's: a V1Emu first, a second one and T18Emu with its save as further programs
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', v1); await p.waitForTimeout(1500); await p.setInputFiles('#fileEmu', v1b); await p.waitForTimeout(1500);
  await p.setInputFiles('#fileEmu', tg18); await p.waitForTimeout(2500); console.log('after T18:', await p.textContent('#toast'));
  const a = await save('k1.bin'); ok(!!a, 'start device saved');
  const t1 = a && t18of(a); ok(!!t1, 'T18Emu in the menu at ' + (t1 && t1.off.toString(16)));
  // items into the program in the menu
  await load(out + '/k1.bin'); await open('TAMA MEETS');
  const fc = p.waitForEvent('filechooser'); await p.click('text=MyMeets-Dateien wählen'); await (await fc).setFiles(files); await p.waitForTimeout(1500);
  const c = await save('k2.bin'); ok(!!c, 'saved with items');
  const t2 = c && t18of(c);
  ok(t2 && t1 && t2.off === t1.off, 'T18Emu stayed at ' + (t2 && t2.off.toString(16)));
  const names = t2 ? E.t18Items(t2.fw).areas.flatMap((x) => x.slots.filter((s) => s.rec).map((s) => s.rec.name)) : [];
  ok(names.length >= 4, 'items in its save: ' + names.join(', '));
  const pa = c && t2 && new DataView(c.buffer).getUint32(t2.off + 0x140 + 428, true);
  ok(t2 && pa === E.t18Addrs(t2.fw).param, 'its Param address renewed: ' + (pa >>> 0).toString(16));
  // a setting only: the save stays as it is (the session too)
  await load(out + '/k2.bin'); await open('TAMA MEETS');
  await p.locator('#appsList .pdet').last().locator('select').nth(1).selectOption({ index: 3 }); await p.waitForTimeout(300);
  const d = await save('k3.bin'); ok(!!d, 'saved with a new light setting');
  const t3 = d && t18of(d), cf = d && t3 && E.extraCfg(d, t3.off);
  ok(t3 && t3.off === t2.off, 'T18Emu still at its place');
  ok(cf && c && d.subarray(cf.saveOff, cf.saveOff + 0x10000).every((v, i) => v === c[cf.saveOff + i]), 'its save untouched');
  // the second program out: T18Emu stays
  await load(out + '/k3.bin');
  await p.locator('#appsList .prow').nth(1).locator('button:has-text("×")').click(); await p.waitForTimeout(500);
  const e = await save('k4.bin'); ok(!!e, 'saved without the second program');
  const t4 = e && t18of(e); ok(t4 && t4.off === t2.off && apps(e).length === apps(d).length - 1, 'T18Emu kept, the second program gone');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
