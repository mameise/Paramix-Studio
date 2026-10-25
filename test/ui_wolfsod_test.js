// 1.6.11 test2: Spear of Destiny next to Wolfenstein 3D: both in the menu, each with its program; a new SOD replaces only
// the SOD. node test/ui_wolfsod_test.js <paradise.bin> <folder .WL6> <folder .SOD> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js'), W = require('../src/wolf.js');
(async () => {
  const [rom, d6, ds, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  const wapp = fs.readFileSync(__dirname + '/../wolf/wolf.bin'), sapp = fs.readFileSync(__dirname + '/../wolf/wolfsod.bin');
  const files = (d, re) => fs.readdirSync(d).filter((n) => re.test(n)).map((n) => path.join(d, n));
  const n6 = files(d6, /\.WL6$/i), ns = files(ds, /\.SOD$/i);
  const pk = (ns_) => { const s = {}; for (const n of ns_) s[path.basename(n)] = new Uint8Array(fs.readFileSync(n)); return W.build(s); };
  const want6 = pk(n6).pack, wants = pk(ns).pack;
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  ok(pk(ns).sod && pk(ns).maps === 21 && pk(ns).episodes === 1 && (wants[8] & 2), 'SOD pack: 21 floors, one episode, flag');
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#bagOn'); await p.waitForTimeout(800);
  await p.click('.tab[data-tab="wolf"]');
  await p.setInputFiles('#fileWolf', n6); await p.waitForTimeout(4000);
  await p.setInputFiles('#fileWolf', ns); await p.waitForTimeout(5000);
  let txt = await p.evaluate(() => document.getElementById('wolfList').innerText);
  ok(/WOLFENSTEIN 3D/.test(txt) && /SPEAR OF DESTINY\s+21 Level/.test(txt), 'both listed: ' + txt.replace(/\s+/g, ' '));
  { const plan = await p.evaluate(() => document.getElementById('plan').innerText); ok(/Spear of Destiny/.test(plan), 'plan names SOD'); }
  // the SOD again: replaces only the SOD
  await p.setInputFiles('#fileWolf', ns); await p.waitForTimeout(5000);
  txt = await p.evaluate(() => document.getElementById('wolfList').innerText);
  ok((txt.match(/SPEAR OF DESTINY/g) || []).length === 1 && /WOLFENSTEIN 3D/.test(txt), 'a new SOD replaces only the SOD');
  await p.selectOption('#wkTurn', '20'); await p.waitForTimeout(200);
  const dlp = p.waitForEvent('download', { timeout: 240000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1));
  const xs = E.appsRead(img, F.feldApps(img, fe.stub).apps).filter((y) => y.kind === 'wolf');
  const x6 = xs.find((y) => !E.wolfSod(y.pack)), xs_ = xs.find((y) => E.wolfSod(y.pack));
  ok(xs.length === 2 && x6 && xs_, 'two in the menu');
  ok(x6 && Buffer.from(x6.pack).equals(Buffer.from(want6)) && xs_ && Buffer.from(xs_.pack).equals(Buffer.from(wants)), 'packs as built');
  ok(x6 && Buffer.from(img.subarray(x6.off + 0x300, x6.off + wapp.length)).equals(wapp.subarray(0x300)), 'Wolf3D program');
  ok(xs_ && Buffer.from(img.subarray(xs_.off + 0x300, xs_.off + sapp.length)).equals(sapp.subarray(0x300)), 'SOD program');
  ok(xs_ && Buffer.from(img.subarray(xs_.off + 0x140 + 24, xs_.off + 0x140 + 40)).toString().startsWith('SPEAR OF DESTINY'), 'SOD name in its settings block');
  ok(xs.every((y) => y.keys.turn === 20), 'keys for both');
  const names = F.feldApps(img, fe.stub).apps.map((a) => a.name);
  ok(names.some((n) => /WOLF3D SPEAR/.test(n)) && names.some((n) => /WOLFENSTEIN/.test(n)), 'menu names: ' + names.join(', '));
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="wolf"]');
  txt = await p.evaluate(() => document.getElementById('wolfList').innerText);
  ok(/WOLFENSTEIN 3D/.test(txt) && /SPEAR OF DESTINY/.test(txt), 'read back: both listed');
  await p.screenshot({ path: out + '/wolfsod_tab.png', fullPage: false });
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
