// 1.6.11 test2: an old T7Emu (save written 2 bytes off) is named in red; after saving with this Studio the note is gone.
// node test/ui_t7old_test.js <ROM with an old T7Emu> <ROM with a current T7Emu> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [oldr, newr, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const warn = () => p.evaluate(() => [...document.querySelectorAll('#plan .rule.warn')].map((x) => x.textContent).join(' | '));
  await p.setInputFiles('#fileRom', newr); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2000);
  ok(!/veraltete Version/.test(await warn()), 'current T7Emu: no note');
  await p.setInputFiles('#fileRom', oldr); await p.waitForTimeout(3000);
  const w = await warn(); ok(/TAMA-GO: veraltete Version/.test(w), 'old T7Emu named: ' + w);
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(3000);
  ok(!/veraltete Version/.test(await warn()), 'written anew: note gone');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  fs.unlinkSync(r1);
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
