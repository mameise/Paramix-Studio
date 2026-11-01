// ROM without the Paradise from a start device whose main program is at 0x0A0000 (1.7.2 test4): one star only, the main
// program first in the list; another program gets the star, the old main program stays as a normal entry.
// node test/ui_clean_star_test.js <start device rom> <tg18.bin> <DOOM.WAD or -> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, tg18, wad, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message)); p.on('dialog', (d) => d.accept());
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const rows = async () => p.$$eval('#appsList .prow', (l) => l.map((r) => ({ n: r.querySelector('.pname').textContent, s: [...r.querySelectorAll('button.pstar')].map((x) => x.textContent).join('') })));
  const star = async (nm) => { const rs = p.locator('#appsList .prow'); const n = await rs.count(); for (let i = 0; i < n; i++) if ((await rs.nth(i).locator('.pname').textContent()) === nm) { await rs.nth(i).locator('button.pstar').click(); await p.waitForTimeout(2000); return true; } return false; };
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="feld"]'); if (!(await p.isChecked('#launchOn'))) { await p.click('#launchOn'); await p.waitForTimeout(1500); }
  await p.click('#cleanOn'); await p.waitForTimeout(3000);
  await p.click('.tab[data-tab="emu"]');
  let r = await rows(); console.log('rows', JSON.stringify(r));
  ok(r.filter((x) => x.s === '★').length === 1 && r[0].s === '★', 'one star, the main program first: ' + r[0].n);
  const main0 = r[0].n;
  await p.setInputFiles('#fileApp', tg18); await p.waitForTimeout(2000);
  if (wad !== '-') { await p.click('.tab[data-tab="doom"]'); await p.setInputFiles('#fileWad', wad); await p.waitForFunction(() => !document.getElementById('busy').classList.contains('on') && /E\d/.test(document.getElementById('doomStat').innerText), null, { timeout: 180000 }); await p.click('.tab[data-tab="emu"]'); }
  r = await rows(); console.log('rows', JSON.stringify(r));
  ok(r[0].n === main0 && (wad === '-' || r[r.length - 1].n === 'DOOM'), 'DOOM is not put first');
  ok(await star('TAMA MEETS'), 'star on TAMA MEETS');
  r = await rows(); console.log('rows', JSON.stringify(r));
  ok(r.filter((x) => x.s === '★').length === 1 && r[0].n === 'TAMA MEETS' && r[0].s === '★', 'one star only, TAMA MEETS first');
  ok(r.some((x) => x.n === main0 && x.s === '☆'), main0 + ' is a normal entry now');
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave'); await p.waitForTimeout(8000); console.log('toast:', await p.textContent('#toast'));
  const dl = await dlp; const f = out + '/' + dl.suggestedFilename(); await dl.saveAs(f);
  const img = new Uint8Array(fs.readFileSync(f)), fa = F.feldApps(img, fe.stub), xs = E.appsRead(img, fa.apps), t18 = xs.find((x) => x.kind === 't18');
  console.log('menu', fa.apps.map((a) => a.name).join(', '));
  ok(F.isClean(img) && t18 && fa.mainSrc === 0x10000000 + t18.off - 0x11000, 'saved: TAMA MEETS is the main program');
  ok(fa.apps.filter((a) => a.name === main0).length === 1, 'saved: ' + main0 + ' once in the menu');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
