// 1.6.9: Connection V3 ROM in the Emulator tab: V3Emu as the main program, a P2 (V1Emu) in the menu; read back.
// node test/ui_v3_test.js <paradise.bin> <v3 rom> <p2.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, v3, p2, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) }, v3app = fs.readFileSync(__dirname + '/../v3/v3.bin'), v1app = fs.readFileSync(__dirname + '/../v1/v1.bin');
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]'); await p.click('#launchOn'); await p.waitForTimeout(1500);   // the menu, for the P2
  await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileEmu', v3); await p.waitForTimeout(800);
  const stat = await p.evaluate(() => document.getElementById('emuStat') ? document.getElementById('emuStat').innerText : document.body.innerText);
  ok(/Connection V3 erkannt/.test(stat) && /Rad kurz: Einstellungen/.test(stat) && !/schütteln/.test(stat), 'status text for the V3');
  await p.setInputFiles('#fileApp', p2); await p.waitForTimeout(500);
  const rows = async () => p.evaluate(() => [...document.querySelectorAll('#appsList .prow')].map((r) => r.querySelector('.pname').textContent + '|' + (r.querySelector('.pstar.on') ? 'main' : '')));
  let l = await rows(); console.log('Liste:', l);
  ok(l.includes('TAMA V3|main') && l.some((x) => x.startsWith('TAMA P2|')), 'TAMA V3 main, P2 in the list');
  // 1.6.9 test20: start when switched on (main program)
  await p.evaluate(() => { const r = [...document.querySelectorAll('#appsList .prow')].find((x) => x.querySelector('.pname').textContent === 'TAMA V3'); const k = r && r.querySelector('.ptog'); if (k) k.click(); });
  await p.waitForTimeout(300);
  ok(await p.evaluate(() => { const c = [...document.querySelectorAll('#appsList .pdet label.check')].find((x) => /Einschalten/.test(x.textContent)); if (!c) return false; if (!c.querySelector('input').checked) c.querySelector('input').click(); return true; }), 'autostart box at TAMA V3');
  ok(await p.evaluate(() => [...document.querySelectorAll('#appsList .pdet label.check')].some((x) => /Uhr/.test(x.textContent))), 'clock box at TAMA V3');
  ok(await p.evaluate(() => /an beiden V3Emu starten/.test(document.getElementById('appsList').innerText)), 'link hint at TAMA V3');
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1));
  { const fa = F.feldApps(img, fe.stub); ok(fa.v9 ? !!fa.auto && fa.mainSrc === 0 : true, 'menu: autostart of the main program (v9 ' + fa.v9 + ', auto ' + fa.auto + ')'); }
  ok((img[0xA0154] & 10) === 10, 'clock flags in the settings block (' + img[0xA0154] + ')');
  ok(Buffer.from(img.subarray(0xA0300, 0xA0000 + v3app.length)).equals(v3app.subarray(0x300)), 'V3Emu at 0x0A0000');
  const m = E.v1FromRom(img); ok(m && m.length === 0x80000 && Buffer.from(m).equals(fs.readFileSync(v3)), 'the V3 ROM as the main program');
  const xs = E.appsRead(img, F.feldApps(img, fe.stub).apps), x = xs.find((y) => y.kind === 'v1' && y.rom.length !== 0x80000);
  ok(x && x.rom.length !== 0x80000 && Buffer.from(img.subarray(x.off + 0x300, x.off + 0x1300)).equals(v1app.subarray(0x300, 0x1300)), 'P2 in the menu with V1Emu');
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="emu"]');
  l = await rows(); console.log('Liste neu:', l);
  ok(l.includes('TAMA V3|main'), 'read back: TAMA V3 main');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
