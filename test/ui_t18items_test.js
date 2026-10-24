// 1.6.8 (test47): T18Emu items in the browser. A start device with a TG18 firmware that has run once (its save filled) as the main program,
// MyMeets files into its save, Tamapoints; saved ROM: the items are in the firmware read back from it; loaded again:
// the installed T18Emu shows them, a further change goes in too.
// Pictures for grow up: the set of the Android T18Emu (pictures and erwachsen.json) go in as 10 pictures.
// node test/ui_t18items_test.js <paradise.bin> <tg18_with_save.bin> <item_dir> <out_dir> [<picture_dir>]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js');
(async () => {
  const [rom, tg18, idir, out, pdir] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const files = fs.readdirSync(idir).filter((f) => /^tg18_.*\.txt$/.test(f)).map((f) => path.join(idir, f));
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const save = async (n) => { const dlp = p.waitForEvent('download', { timeout: 300000 }); setTimeout(async () => { try { console.log('toast:', await p.textContent('#toast')); } catch (e) {} }, 60000); await p.click('#btnSave'); const dl = await dlp; const f = out + '/' + n; await dl.saveAs(f); return new Uint8Array(fs.readFileSync(f)); };
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);   // start device: room for the firmware with its save
  await p.click('.tab[data-tab="emu"]'); await p.setInputFiles('#fileEmu', tg18); await p.waitForTimeout(1500);
  await p.locator('#appsList .prow').first().locator('.ptog').click(); await p.waitForTimeout(500);
  ok(await p.isVisible('text=MyMeets-Dateien wählen'), 'items section shown');
  await p.click('text=MyMeets-Dateien wählen').catch(() => {});
  await p.setInputFiles('#fileT18Item', files); await p.waitForTimeout(1500);
  const shown = await p.$$eval('.pdet .itemname', (l) => l.map((x) => x.textContent.trim()));
  ok(shown.length >= 5, 'items listed: ' + shown.join(', '));
  if (pdir) { await p.setInputFiles('#fileT18Pic', fs.readdirSync(pdir).map((f) => path.join(pdir, f))); await p.waitForTimeout(2000);
    ok((await p.$$('.picgrid figure')).length >= 10, 'pictures shown: ' + (await p.$$('.picgrid figure')).length); }
  await p.fill('.pdet input[type=number]', '1234'); await p.dispatchEvent('.pdet input[type=number]', 'change'); await p.waitForTimeout(300);
  const a = await save('i1.bin');
  const fw = E.t18FromRom(a), it = fw && E.t18Items(fw);
  ok(!!it, 'firmware read back');
  const names = it ? it.areas.flatMap((x) => x.slots.filter((s) => s.rec).map((s) => s.rec.name)) : [];
  ok(names.includes('WASABI') && names.includes('MAGIC WAN') && names.includes('PINK CLOV'), 'items in the save: ' + names.join(', '));
  ok(it && it.room && it.room.name === 'SUNSET', 'room: ' + (it && it.room && it.room.name));
  ok(it && it.points === 1234, 'Tamapoints ' + (it && it.points));
  if (pdir) { const pm = E.t18PicsRead(a); ok(pm.size === 10 && pm.has(16) && pm.has(35), 'pictures in the ROM: ' + [...pm.keys()].join(',')); }
  // load again: the installed program shows them; remove one, save
  await p.setInputFiles('#fileRom', out + '/i1.bin'); await p.waitForTimeout(3000); await p.click('.tab[data-tab="emu"]');
  if (!(await p.$('.pdet'))) await p.locator('#appsList .prow .ptog').first().click(); await p.waitForTimeout(800);
  const shown2 = await p.$$eval('.pdet .itemname', (l) => l.map((x) => x.textContent.trim()));
  ok(shown2.some((x) => /WASABI/.test(x)), 'installed: items shown (' + shown2.length + ')');
  await p.locator('.pdet .dlcrow', { hasText: 'WASABI' }).locator('button').click(); await p.waitForTimeout(500);
  const c = await save('i2.bin');
  const it2 = E.t18Items(E.t18FromRom(c));
  const names2 = it2.areas.flatMap((x) => x.slots.filter((s) => s.rec).map((s) => s.rec.name));
  ok(!names2.includes('WASABI') && names2.includes('HOT COCOA'), 'removed on the installed one: ' + names2.join(', '));
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
