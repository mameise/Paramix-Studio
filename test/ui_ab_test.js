// 1.7.3: tab Arduboy: games from .hex and a .zip into the menu, keys changed, saved, read back.
// node test/ui_ab_test.js <paradise.bin> <game.hex> <game2.hex> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
const C = require('../src/core.js');
(async () => {
  const [rom, hex1, hex2, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) }, abapp = fs.readFileSync(__dirname + '/../ab/ab.bin');
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  // the second game in a zip
  const zip = C.zipBuild([{ name: "games/" + path.basename(hex2), data: new Uint8Array(fs.readFileSync(hex2)) }]);
  if (zip) fs.writeFileSync(out + '/games.zip', zip);
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#bagOn'); await p.waitForTimeout(800);
  await p.click('.tab[data-tab="ab"]');
  ok(await p.evaluate(() => !document.getElementById('pane-ab').hidden), 'tab Arduboy shown');
  ok(await p.evaluate(() => document.querySelectorAll('#abKeyGrid select').length === 9), 'nine key lines');
  fs.writeFileSync(out + '/bad.hex', 'not a hex file\n');
  await p.setInputFiles('#fileAb', out + '/bad.hex'); await p.waitForTimeout(400);
  ok(/kein Arduboy-Spiel/.test(await p.evaluate(() => document.getElementById('toast').textContent)), 'bad file refused');
  await p.setInputFiles('#fileAb', hex1); await p.waitForTimeout(1500);
  if (zip) { await p.setInputFiles('#fileAb', out + '/games.zip'); await p.waitForTimeout(1500); }
  const names = await p.evaluate(() => document.getElementById('abList').innerText);
  const n1 = E.abTitle(path.basename(hex1)), n2 = E.abTitle(path.basename(hex2));
  ok(names.includes(n1), 'game in the list: ' + n1);
  if (zip) ok(names.includes(n2), 'game from the zip in the list: ' + n2);
  await p.evaluate(() => { const s = document.querySelectorAll('#abKeyGrid select')[8]; s.value = '16'; s.dispatchEvent(new Event('change')); });
  await p.waitForTimeout(200);
  ok(/Arduboy/.test(await p.evaluate(() => document.querySelector('#plan').innerText)), 'own part in "Dein neues ROM"');
  const dlp = p.waitForEvent('download', { timeout: 120000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1));
  const xs = E.appsRead(img, F.feldApps(img, fe.stub).apps).filter((y) => y.kind === 'ab');
  ok(xs.length === (zip ? 2 : 1), xs.length + ' Arduboy games in the menu');
  const x = xs[0];
  ok(x && Buffer.from(x.rom).equals(Buffer.from(E.abHex(fs.readFileSync(hex1, 'latin1')))), 'the game in the ROM');
  ok(x && x.keys[8] === 16 && x.keys[0] === 4 && x.inv, 'keys in the settings block');
  ok(x && Buffer.from(img.subarray(x.off + 0x300, x.off + abapp.length)).equals(abapp.subarray(0x300)), 'ArduboyEmu program in place');
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="ab"]');
  ok((await p.evaluate(() => document.getElementById('abList').innerText)).includes(n1), 'read back: game listed');
  ok(await p.evaluate(() => document.querySelectorAll('#abKeyGrid select')[8].value === '16'), 'read back: keys');
  await p.screenshot({ path: out + '/ab_tab.png', fullPage: false });
  // 1.7.3 test5: an older ArduboyEmu in the ROM (OLDAB=<old ab.bin>) is written anew on saving
  if (process.env.OLDAB) {
    const old = new Uint8Array(fs.readFileSync(process.env.OLDAB)), o = new Uint8Array(img);
    for (let i = 0x800; i < 0x10000; i++) o[x.off + i] = i < old.length ? old[i] : 0xFF;
    fs.writeFileSync(out + '/old_ab.bin', o);
    await p.setInputFiles('#fileRom', out + '/old_ab.bin'); await p.waitForTimeout(3000);
    const plan = await p.evaluate(() => Array.from(document.querySelectorAll('#plan .chip, #plan span, #plan div')).map((e) => e.innerText).filter((t) => /·/.test(t) && t.length < 200));
    console.log('     plan: ' + JSON.stringify(plan));
    ok(plan.some((t) => /^Arduboy/.test(t) && /wird aktualisiert/.test(t)), 'older ArduboyEmu: shown as updated at Arduboy');
    ok(!plan.some((t) => /^Emulator/.test(t) && /wird aktualisiert/.test(t)), 'older ArduboyEmu: not shown at the emulators');
    const dlo = p.waitForEvent('download', { timeout: 120000 }); await p.click('#btnSave');
    const d2 = await dlo.catch(() => null); ok(!!d2, 'older ArduboyEmu: there is something to write');
    if (d2) { const r2 = out + '/up_' + d2.suggestedFilename(); await d2.saveAs(r2); const im2 = new Uint8Array(fs.readFileSync(r2));
      const y = E.appsRead(im2, F.feldApps(im2, fe.stub).apps).find((z) => z.kind === 'ab');
      ok(y && Buffer.from(im2.subarray(y.off + 0x800, y.off + abapp.length)).equals(abapp.subarray(0x800)), 'older ArduboyEmu: the new program in place');
      ok(y && y.keys[8] === 16, 'older ArduboyEmu: keys kept'); }
  }
  // 1.7.3 test7: "remember where you left off" off for the first game (tab Emulator, its row opened)
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(300);
  await p.evaluate(() => { const r = Array.from(document.querySelectorAll('#appsList .prow')).find((e) => e.querySelector('.ptog')); r.querySelector('.ptog').click(); });
  await p.waitForTimeout(300);
  const had = await p.evaluate(() => { const l = Array.from(document.querySelectorAll('#appsList .pdet label')).find((e) => /Spielstand beim Verlassen/.test(e.textContent)); if (!l) return false; const c = l.querySelector('input'); c.checked = false; c.dispatchEvent(new Event('change')); return true; });
  ok(had, 'checkbox "Spielstand beim Verlassen merken" in the row');
  const dls = p.waitForEvent('download', { timeout: 120000 }); await p.click('#btnSave');
  const d3 = await dls.catch(() => null); ok(!!d3, 'something to write');
  if (d3) { const r3 = out + '/sess_' + d3.suggestedFilename(); await d3.saveAs(r3); const im3 = new Uint8Array(fs.readFileSync(r3));
    const ys = E.appsRead(im3, F.feldApps(im3, fe.stub).apps).filter((z) => z.kind === 'ab');
    ok(ys.length === 2 && ys.filter((z) => z.sess === false).length === 1, 'one game without remembering, one with'); }
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
