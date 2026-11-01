// 1.7.2 test14 GPLAY: checkbox in the Menu tab sets F_GPLAY in the menu settings; off for a start device.
// node test/ui_gplay_test.js <paradise.bin with a save> <out_dir> [tester binary for para_points]
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path'), cp = require('child_process');
const F = require('../src/feld.js');
(async () => {
  const [rom, out, tester] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const tpl = new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin'));
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 1000 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="feld"]'); await p.waitForTimeout(300);
  ok(!(await p.isDisabled('#gplayOn')), 'checkbox usable in a Paradise ROM');
  await p.check('#gplayOn'); await p.waitForTimeout(300);
  await p.screenshot({ path: out + '/gplay.png' });
  const [dl] = await Promise.all([p.waitForEvent('download', { timeout: 300000 }), p.click('#btnSave')]);
  const f = out + '/gplay.bin'; await dl.saveAs(f);
  const r = new Uint8Array(fs.readFileSync(f)), ff = F.feldFind(r, tpl);
  ok(ff && (ff.flags & F.F_GPLAY), 'flag in the menu settings: ' + (ff && ff.flags));
  // read back: checkbox stays on
  await p.setInputFiles('#fileRom', f); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="feld"]'); await p.waitForTimeout(300);
  ok(await p.isChecked('#gplayOn'), 'read back: on');
  await p.check('#launchOn'); await p.waitForTimeout(300);
  ok(await p.isDisabled('#gplayOn') && !(await p.isChecked('#gplayOn')), 'start device: off and greyed');
  if (tester) {
    const o2 = out + '/after.bin'; const money = (x) => { const c = ff.off + 0x140, mo = x[c + 260] | x[c + 261] << 8; return (x[0xEFE000 + mo] | x[0xEFE000 + mo + 1] << 8 | x[0xEFE000 + mo + 2] << 16) >>> 0; };
    const m0 = money(r);
    console.log(cp.execFileSync(tester, [f, '185', o2]).toString().trim());
    const r2 = new Uint8Array(fs.readFileSync(o2));
    ok(money(r2) === Math.min(99999, m0 + 30), 'points ' + m0 + ' -> ' + money(r2));
    ok(Buffer.from(r2.subarray(0xEFE000, 0xEFF000)).equals(Buffer.from(r2.subarray(0xEFF000, 0xF00000))), 'both copies the same');
    const SV = require('../src/saves.js'), crc = SV.crc16arc(r2.subarray(0xEFE000, 0xEFF000).subarray(4), 0);
    let diff = 0; for (let i = 0xEFE004; i < 0xEFF000; i++) if (r2[i] !== r[i]) diff++;
    ok(diff <= 4, 'only the money changed (' + diff + ' bytes)');
  }
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
