// 1.6.10 test1: tab Wolf3D: the user's game files into the menu, keys changed, saved, read back; the program and its
// settings block at their place. node test/ui_wolf_test.js <paradise.bin> <folder with .WL1 or .WL6 (full version first)> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js'), W = require('../src/wolf.js');
(async () => {
  const [rom, dir, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) }, wapp = fs.readFileSync(__dirname + '/../wolf/wolf.bin');
  const all = fs.readdirSync(dir), full = all.some((n) => /\.WL6$/i.test(n));
  const names = all.filter((n) => (full ? /\.WL6$/i : /\.WL1$/i).test(n)).map((n) => path.join(dir, n));
  const set = {}; for (const n of names) set[path.basename(n)] = new Uint8Array(fs.readFileSync(n));
  const want = W.build(set).pack;
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#bagOn'); await p.waitForTimeout(800);
  await p.click('.tab[data-tab="wolf"]');
  ok(await p.evaluate(() => !document.getElementById('pane-wolf').hidden), 'tab Wolf3D shown');
  ok(/Noch keine/.test(await p.evaluate(() => document.getElementById('wolfList').innerText)), 'empty at first');
  // only some files: the missing ones are named
  await p.setInputFiles('#fileWolf', names.filter((n) => !/VGADICT/i.test(n))); await p.waitForTimeout(800);
  ok(/VGADICT/.test(await p.evaluate(() => document.getElementById('toast').textContent)), 'missing file named');
  await p.setInputFiles('#fileWolf', names); await p.waitForTimeout(4000);
  const txt = await p.evaluate(() => document.getElementById('wolfList').innerText);
  ok(/WOLFENSTEIN 3D/.test(txt) && (full ? /Vollversion/.test(txt) && /6 Episoden/.test(txt) : /Shareware/.test(txt) && /1 Episode/.test(txt)), 'listed: ' + txt.replace(/\s+/g, ' '));
  { const plan = await p.evaluate(() => document.getElementById('plan').innerText);
    ok(/Wolfenstein 3D\s*(neu|NEU)/i.test(plan) && /Wolfenstein 3D\s+[0-9.]+ MB/.test(plan), 'own chip and own part of the flash bar: ' + plan.replace(/\s+/g, ' ').slice(0, 300)); }
  await p.selectOption('#wkTurn', '20'); await p.selectOption('#wkFire', '2'); await p.selectOption('#wkFwd', '1');
  await p.waitForTimeout(200);
  const dlp = p.waitForEvent('download', { timeout: 180000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1));
  const xs = E.appsRead(img, F.feldApps(img, fe.stub).apps), x = xs.find((y) => y.kind === 'wolf');
  ok(!!x && Buffer.from(x.pack).equals(Buffer.from(want)), 'the pack in the menu, as built from the files');
  ok(x && x.keys.turn === 20 && x.keys.fire === 2 && x.keys.fwd === 1 && x.keys.back === 4 && x.keys.use === 8, 'keys in the settings block');
  const c = x ? x.off + 0x140 : 0;
  ok(x && Buffer.from(img.subarray(x.off + 0x300, x.off + wapp.length)).equals(wapp.subarray(0x300)), 'program (both parts) in place');
  ok(x && E.u32 === undefined && (img[c + 48] | img[c + 49] << 8 | img[c + 50] << 16 | img[c + 51] << 24) === x.off, 'program knows its place');
  ok(x && x.saveOff === x.off + 0x20000 && img.subarray(x.saveOff, x.saveOff + 0x10000).every((v) => v === 0xFF), 'empty save area behind the program');
  fs.writeFileSync(out + '/wolf_off.txt', String(x ? x.off : 0));
  // read back into the Studio: listed again, keys back
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="wolf"]');
  ok(/WOLFENSTEIN 3D/.test(await p.evaluate(() => document.getElementById('wolfList').innerText)), 'read back: listed');
  ok(await p.evaluate(() => document.getElementById('wkTurn').value === '20' && document.getElementById('wkFire').value === '2'), 'read back: keys');
  await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(300);
  ok(await p.evaluate(() => [...document.querySelectorAll('#appsList .pname')].some((n) => /WOLFENSTEIN/.test(n.textContent))), 'in the list of programs');
  await p.click('.tab[data-tab="wolf"]');
  await p.screenshot({ path: out + '/wolf_tab.png', fullPage: false });
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
