// 1.6.10 test6: start device: the damaged further T18Emu out, the firmware in again and made the main program;
// the old further programs no longer block its room. node test/ui_t18main_test.js <start device ROM> <TG18 firmware> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, fwf, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const st = new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')), fw = new Uint8Array(fs.readFileSync(fwf));
  const a = new Uint8Array(fs.readFileSync(rom)), n0 = E.appsRead(a, F.feldApps(a, st).apps).filter((x) => x.kind !== 't18').length;
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2000);
  await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(300);
  await p.evaluate(() => { const r = [...document.querySelectorAll('#appsList .prow')].find((x) => /TAMA MEETS/.test(x.querySelector('.pname').textContent)); [...r.querySelectorAll('button')].find((x) => x.textContent.trim() === '×').click(); });
  await p.waitForTimeout(1500);
  await p.setInputFiles('#fileEmu', fwf); await p.waitForTimeout(6000);
  await p.evaluate(() => { const r = [...document.querySelectorAll('#appsList .prow')].find((x) => /TAMA MEETS/.test(x.querySelector('.pname').textContent)); const s = [...r.querySelectorAll('button')].find((x) => x.textContent.trim() === '☆'); if (s) s.click(); });
  await p.waitForTimeout(6000);
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const dl = await Promise.race([dlp, new Promise((r) => setTimeout(() => r(null), 90000))]);
  ok(!!dl, 'saved (' + await p.evaluate(() => document.getElementById('toast').textContent) + ')');
  if (dl) {
    const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
    const o = new Uint8Array(fs.readFileSync(r1));
    ok(F.emuType(o) === 't18', 'T18Emu is the main program');
    const back = E.t18FromRom(o); ok(!!back && Buffer.from(back).equals(Buffer.from(fw)), 'its firmware complete');
    const xs = E.appsRead(o, F.feldApps(o, st).apps); ok(xs.length === n0, 'the other programs still in the menu (' + xs.length + ')');
    const c = E.t18Cfg(o), t = new Set(c.map.concat(c.spare).filter((x) => x !== 0xFFFF)); let clash = 0;
    for (const x of xs) { const cc = E.extraCfg(o, x.off), rs = [[x.off, x.off + 0x10000]]; if (cc.romOff !== undefined) rs.push([cc.romOff, cc.romOff + (cc.romLen || 1)]); if (cc.saveOff) rs.push([cc.saveOff, cc.saveOff + 0x10000]); for (const [s, e] of rs) for (let k = s >> 16; k <= (e - 1) >> 16; k++) if (t.has(k)) clash++; }
    ok(clash === 0, 'no program on T18Emu blocks');
    fs.unlinkSync(r1);
  }
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
