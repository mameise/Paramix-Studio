// 1.7.5 PACK_WARN in the browser: TamaEmu unpacked saves without a question; with "Pack the firmware" the Studio asks
// first (cancel: nothing saved, OK: saved).
// node test/ui_packwarn_test.js <rom> <idl firmware>
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const [rom, fw] = process.argv.slice(2);
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  let asked = [], answer = false; p.on('dialog', async (d) => { asked.push(d.message()); if (answer) await d.accept(); else await d.dismiss(); });
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(300);
  await p.setInputFiles('#fileEmu', fw); await p.waitForTimeout(1500);
  const st0 = await p.evaluate(() => document.getElementById('emuStat').innerText);
  ok(/teilweise gepackt/.test(st0), 'main program: note on the partly packed firmware');
  let lastDl = null; const save = async (ms) => { let got = false; const w = p.waitForEvent('download', { timeout: ms }).then(async (d) => { got = true; lastDl = await d.path(); }).catch(() => {}); await p.click('#btnSave'); await w; return got; };
  asked = []; ok(await save(120000) && asked.length === 0, 'unpacked: saved without a question'); const raw = lastDl ? require('fs').copyFileSync(lastDl, require('path').join(require('os').tmpdir(), 'pw_raw.bin')) || require('path').join(require('os').tmpdir(), 'pw_raw.bin') : null;
  // tick "Firmware packen" in the row of TamaEmu
  await p.evaluate(() => { const t = [...document.querySelectorAll('#appsList .prow .ptog')]; t.forEach((x) => x.click()); });
  await p.waitForTimeout(500);
  const ticked = await p.evaluate(() => { const l = [...document.querySelectorAll('label.check')].find((x) => /Firmware packen/.test(x.textContent)); if (!l) return false; l.querySelector('input').click(); return true; });
  ok(ticked, 'checkbox "Firmware packen" found');
  const hint = await p.evaluate(() => [...document.querySelectorAll('.hint')].map((h) => h.textContent).find((x) => /Platz/.test(x)) || '');
  ok(/langsamer/.test(hint), 'hint: ' + hint);
  await p.waitForTimeout(1500);
  asked = []; answer = false; ok(!(await save(20000)) && asked.length === 1 && /gepackt/.test(asked[0]), 'packed, cancel: asked, nothing saved');
  console.log('   ' + (asked[0] || '').replace(/\n/g, ' / '));
  asked = []; answer = true; ok(await save(120000) && asked.length === 1, 'packed, OK: saved');
  // the unpacked ROM read back: "Firmware packen" stays off (the 2 or 3 blocks of the main program are not "packed")
  if (raw) { await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html')); await p.setInputFiles('#fileRom', raw); await p.waitForSelector('#work.on', { timeout: 60000 });
    await p.click('.tab[data-tab="emu"]'); await p.waitForTimeout(500);
    await p.evaluate(() => { [...document.querySelectorAll('#appsList .prow .ptog')].forEach((x) => x.click()); }); await p.waitForTimeout(500);
    const st = await p.evaluate(() => { const l = [...document.querySelectorAll('label.check')].find((x) => /Firmware packen/.test(x.textContent)); return { box: l ? l.querySelector('input').checked : null, txt: document.getElementById('emuStat').innerText }; });
    ok(st.box === false && /teilweise gepackt/.test(st.txt), 'read back: "Firmware packen" off, note shown'); }
  ok(!errors.length, 'no page errors ' + errors.join(' | '));
  await b.close(); console.log(bad ? bad + ' failed' : 'all good'); process.exit(bad ? 1 : 0);
})();
