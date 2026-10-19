// 1.6.8: a TamaSma card in the Card tab: its characters, one of them onto a figure, saved.
// node test/ui_sma_test.js <paradise.bin> <tamasma card.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const C = require('../src/core.js');
(async () => {
  const [rom, card, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 1000 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2000);
  await p.click('.tab[data-tab="dim"]');
  await p.setInputFiles('#fileDim', card); await p.waitForTimeout(1500);
  const n = await p.$$eval('#dimArea .dimgrid .tile', (l) => l.length);
  ok(n > 5, 'card characters shown: ' + n);
  await p.screenshot({ path: out + '/sma.png' });
  await p.click('#dimArea .assign .btn.primary'); await p.waitForTimeout(3000);
  ok(/Figur|Karte/.test(await p.textContent('#toast')), 'applied: ' + (await p.textContent('#toast')).slice(0, 80));
  const [dl] = await Promise.all([p.waitForEvent('download', { timeout: 300000 }), p.click('#btnSave')]);
  const f = out + '/sma.bin'; await dl.saveAs(f);
  const r = new Uint8Array(fs.readFileSync(f)), c0 = C.openRom(new Uint8Array(fs.readFileSync(rom))), c1 = C.openRom(r);
  ok(c1.entries[c0.adult].some((v, i) => v !== c0.entries[c0.adult][i]) || c1.entries[c0.adult].length !== c0.entries[c0.adult].length, 'adult sprite changed');
  C.verifyRom(r, [c0.adult, c0.adult + 3]); ok(true, 'ROM checks out');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
