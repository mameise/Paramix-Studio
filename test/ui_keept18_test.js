// 1.6.10 test5: start device with a further T18Emu: removing the main program (and others) keeps all of T18Emu's
// blocks, spares, program and save. node test/ui_keept18_test.js <start device ROM with T18Emu in the menu> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, out, broken] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const st = new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin'));
  const a = new Uint8Array(fs.readFileSync(rom));
  const t18 = E.appsRead(a, F.feldApps(a, st).apps).find((x) => x.kind === 't18');
  ok(!!t18, 'T18Emu in the menu');
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(1500);
  ok(!(await p.evaluate(() => !!document.querySelector('#plan .rule.warn'))), 'no damage note for an intact T18Emu');
  if (broken) {
    await p.setInputFiles('#fileRom', broken); await p.waitForTimeout(3000);
    ok(/Tama Meets ist beschädigt/.test(await p.evaluate(() => (document.querySelector('#plan .rule.warn') || {}).textContent || '')), 'damage note for emptied T18Emu blocks');
    await p.setInputFiles('#fileRom', rom); await p.waitForTimeout(3000);
  }
  await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(300);
  // the main program (first row) out, and every further one that is not T18Emu
  const n = await p.evaluate(() => { let k = 0; for (const r of [...document.querySelectorAll('#appsList .prow')]) { const nm = r.querySelector('.pname').textContent.trim(); if (/TAMA MEETS|WOLFENSTEIN/.test(nm)) continue; const d = [...r.querySelectorAll('button')].find((x) => x.textContent.trim() === '×'); if (d) { d.click(); k++; break; } } return k; });
  ok(n === 1, 'main program removed');
  await p.waitForTimeout(1500);
  const dlp = p.waitForEvent('download', { timeout: 180000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const o = new Uint8Array(fs.readFileSync(r1));
  const c = E.t18Cfg(a, t18.off); let ch = 0;
  for (const blk of c.map.concat(c.spare)) { if (blk === 0xFFFF) continue; const s = blk << 16; if (!a.subarray(s, s + 0x10000).every((v, k) => v === o[s + k])) ch++; }
  ok(ch === 0, 'all T18Emu blocks unchanged (' + ch + ' changed)');
  ok(a.subarray(c.saveOff, c.saveOff + 0x24000).every((v, k) => v === o[c.saveOff + k]), 'T18Emu save unchanged');
  ok(!!E.appsRead(o, F.feldApps(o, st).apps).find((x) => x.kind === 't18' && x.off === t18.off), 'T18Emu still in the menu at its place');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  fs.unlinkSync(r1);
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
