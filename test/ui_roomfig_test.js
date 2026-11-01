// 1.7.2 ROOM_FIGS and DL_SIG: in a start device the overview's real free room follows added Tama-Go figures
// ("reserviert" stays, "frei" goes down), and copies of older flash app versions left by earlier writes are removed.
// node test/ui_roomfig_test.js <start device rom with TAMA-GO> <folder with figures> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [rom, dir, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const figs = fs.readdirSync(dir).filter((f) => /\.bin$/i.test(f)).sort().map((f) => dir + '/' + f);
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  const mb = (s) => { const m = /([\d.]+) MB/.exec(s || ''); return m ? +m[1] : 0; };
  const seg = async () => { const l = await p.$$eval('#plan .legend > *', (a) => a.map((x) => x.textContent)); return { resv: mb(l.find((x) => /reserviert/.test(x))), free: mb(l.find((x) => /^frei/.test(x))), txt: l.join(' / ') }; };
  await p.waitForTimeout(8000);
  const s0 = await seg(); console.log('load:', s0.txt);
  ok(s0.resv > 0 && s0.resv < 1, 'reserved shown after loading, less than 1 MB (no old flash app copies)');
  await p.click('.tab[data-tab="emu"]');
  let last = s0;
  for (let i = 0; i < figs.length; i += 3) {
    const btn = p.locator('#appsList button', { hasText: 'Figuren hinzufügen' }).first();
    const [fc] = await Promise.all([p.waitForEvent('filechooser'), btn.click()]);
    await fc.setFiles(figs.slice(i, i + 3)); await p.waitForTimeout(8000);
    const s = await seg(); console.log('figures', Math.min(i + 3, figs.length) + ':', s.txt);
    ok(Math.abs(s.resv - s0.resv) <= 0.15, 'reserved stays (' + s.resv + ' MB)');
    ok(s.free < last.free, 'free goes down (' + s.free + ' MB)');
    last = s;
  }
  const [dl] = await Promise.all([p.waitForEvent('download', { timeout: 300000 }), p.click('#btnSave')]);
  const f1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(f1);
  const img = fs.readFileSync(f1), fa = fs.readFileSync(path.join(__dirname, '../flash/flashapp.bin'));
  let n = 0; for (let o = 0x100000; o < 0xFF0000; o += 0x10000) if (img.subarray(o, o + 64).equals(fa.subarray(0, 64))) n++;
  ok(n === 0, 'saved: no flash app copies left (' + n + ')');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? 'FAIL ' + bad : 'all good'); process.exit(bad ? 1 : 0);
})();
