// 1.6.9 test36: tab Game Boy: a game into the menu, keys changed, saved, read back; the program runs from its place.
// node test/ui_gb_test.js <paradise.bin> <game.gb> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, game, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) }, gbapp = fs.readFileSync(__dirname + '/../gb/gb.bin');
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#bagOn'); await p.waitForTimeout(800);   // the menu (items)
  await p.click('.tab[data-tab="gb"]');
  ok(await p.evaluate(() => !document.getElementById('pane-gb').hidden), 'tab Game Boy shown');
  ok(await p.evaluate(() => document.querySelectorAll('#gbKeyGrid select').length === 9), 'nine key lines');
  // a file that is not a Game Boy game
  fs.writeFileSync(out + '/bad.gb', Buffer.alloc(0x8000, 1));
  await p.setInputFiles('#fileGb', out + '/bad.gb'); await p.waitForTimeout(400);
  ok(/kein Game-Boy-Spiel/.test(await p.evaluate(() => document.getElementById('toast').textContent)), 'bad file refused');
  await p.setInputFiles('#fileGb', game); await p.waitForTimeout(1500);
  const names = await p.evaluate(() => document.getElementById('gbList').innerText);
  const title = E.gbTitle(fs.readFileSync(game));
  ok(names.includes(title), 'game in the list (' + title + ')');
  // key: wheel press = B instead of A, wheel the other way round
  await p.evaluate(() => { const s = document.querySelectorAll('#gbKeyGrid select')[3]; s.value = '2'; s.dispatchEvent(new Event('change')); });
  await p.evaluate(() => { const c = document.getElementById('gbInv'); c.checked = true; c.dispatchEvent(new Event('change')); });
  await p.waitForTimeout(200);
  ok(/Nicht belegt: A/.test(await p.evaluate(() => document.getElementById('gbKeysStat').textContent)), 'note: A not assigned');
  const dlp = p.waitForEvent('download', { timeout: 120000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1));
  const xs = E.appsRead(img, F.feldApps(img, fe.stub).apps), x = xs.find((y) => y.kind === 'gb');
  ok(!!x && Buffer.from(x.rom).equals(fs.readFileSync(game)), 'the game in the menu');
  ok(x && x.keys[3] === 2 && x.keys[0] === 32 && x.keys[2] === 16 && x.keys[7] === 0 && x.inv, 'keys and wheel direction in the settings block');
  ok(x && Buffer.from(img.subarray(x.off + 0x300, x.off + gbapp.length)).equals(gbapp.subarray(0x300)), 'GBEmu program in place');
  fs.writeFileSync(out + '/gb_off.txt', String(x ? x.off : 0));
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="gb"]');
  ok((await p.evaluate(() => document.getElementById('gbList').innerText)).includes(title), 'read back: game listed');
  ok(await p.evaluate(() => document.querySelectorAll('#gbKeyGrid select')[3].value === '2' && document.getElementById('gbInv').checked), 'read back: keys');
  await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(300);
  await p.evaluate(() => { const r = [...document.querySelectorAll('#appsList .prow')].find((x) => /CPU_INSTRS|MARIO|KIRBY/.test(x.querySelector('.pname').textContent)); const k = r && r.querySelector('.ptog'); if (k) k.click(); });
  await p.waitForTimeout(300);
  ok(await p.evaluate(() => ![...document.querySelectorAll('#appsList .pdet label.check')].some((x) => /Uhr/.test(x.textContent))), 'no clock box for the Game Boy');
  await p.click('.tab[data-tab="gb"]');
  await p.screenshot({ path: out + '/gb_tab.png', fullPage: false });
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
