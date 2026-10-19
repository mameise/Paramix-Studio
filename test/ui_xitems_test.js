// 1.6.8: figures and download items for the programs in the menu (start device)
// node test/ui_xitems_test.js <paradise.bin> <friends.bin> <tama-go.bin> <fig.bin> <idl.bin> <item.jpg> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, fr, go, fig, idl, jpg, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileEmu', fr); await p.waitForTimeout(300);
  await p.setInputFiles('#fileEmu', go); await p.waitForTimeout(300);
  await p.setInputFiles('#fileEmu', idl); await p.waitForTimeout(800);
  const rows = p.locator('#appsList .prow');
  ok(await rows.count() === 3, 'three rows: ' + (await p.textContent('#appsList')));
  // figures on the Tama-Go row
  const fcs = p.waitForEvent('filechooser'); await rows.nth(1).locator('button', { hasText: 'Figuren' }).click(); (await fcs).setFiles(fig); await p.waitForTimeout(500);
  ok(/Figuren hinzufügen \(1\)/.test(await rows.nth(1).textContent()), 'Tama-Go row: one figure');
  // download item on the iD L row
  const fcd = p.waitForEvent('filechooser'); await rows.nth(2).locator('button', { hasText: 'Download' }).click(); (await fcd).setFiles(jpg); await p.waitForTimeout(800);
  await rows.nth(2).locator('.ptog').click(); await p.waitForTimeout(300);
  const dl = await p.textContent('.pdet');
  ok( /1\/|せつぶん/.test(dl), 'item listed for the iD L: ' + dl.slice(0, 80));
  // clock per program: off for the iD L only
  await p.locator('.pdet input[type=checkbox]').first().uncheck(); await p.waitForTimeout(300);
  const plan = await p.textContent('#plan');
  ok(/Emulatoren FRIENDS, TAMA-GO, ID L/.test(plan) && (await p.$eval('#plan strong', (e) => e.textContent)) === 'FRIENDS', 'plan: one chip, main in bold: ' + plan.slice(0, 120));
  const [d1] = await Promise.all([p.waitForEvent('download', { timeout: 180000 }), p.click('#btnSave')]);
  const r1 = out + '/xitems.bin'; await d1.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), xs = E.appsRead(img, F.feldApps(img, fe.stub).apps);
  const tg = xs.find((x) => !x.kind), ti = xs.find((x) => x.kind === 'tama');
  ok(tg && tg.figs.length === 1, 'saved: Tama-Go with ' + (tg && tg.figs.length) + ' figure');
  ok(ti && E.dlcStores(ti.guest).some((k) => k.slots.some((y) => y.name)), 'saved: iD L with the item');
  ok(ti && ti.clock === false && tg.clock === true, 'clock per program: iD L off, Tama-Go on');
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(2500);
  await p.click('.tab[data-tab="emu"]');
  ok(/Figuren hinzufügen \(1\)/.test(await p.textContent('#appsList')), 'loaded again: figure count shown');
  // language twice: the list stays (1.6.8 test25 lost it)
  await p.click('[data-lang="en"]'); await p.waitForTimeout(300); await p.click('[data-lang="de"]'); await p.waitForTimeout(300);
  ok(await p.locator('#appsList .prow').count() === 3, 'language switched twice: still three rows');
  // another main program with ★
  const stars = await p.$$eval('#appsList .prow .pstar', (l) => l.map((e) => e.textContent + (e.disabled ? '-' : '+')));
  ok(stars.join(' ') === '★- ☆+ ☆+', 'stars: filled for the main one, empty for the others: ' + stars.join(' '));
  await p.locator('#appsList .prow').nth(1).locator('button.pstar').click(); await p.waitForTimeout(1500);
  const first = await p.locator('#appsList .prow .pname').first().textContent();
  ok(first === 'TAMA-GO' && (await p.$eval('#plan strong', (e) => e.textContent)) === 'TAMA-GO', '★: TAMA-GO is the main program now (' + first + ')');
  ok(await p.locator('#appsList .prow').count() === 3, '★: still three programs');
  ok((await p.$$eval('#appsList .prow .pstar', (l) => l.map((e) => e.textContent).join(''))) === '★☆☆', 'stars after the swap');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
