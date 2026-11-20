// 1.7.6 NEON_PET: Neon Planets: the Tama of the save in the ROM is changed in the Save tab (settings ring).
// node test/ui_neon_pet_test.js <neon.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const SV = require('../src/saves.js');
(async () => {
  const [rom, out] = process.argv.slice(2); fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="save"]');
  await p.click('#btnPetRom'); await p.waitForTimeout(800);
  const st = await p.evaluate(() => document.getElementById('petStat').textContent);
  ok(/Stufe/.test(st) && !(await p.isHidden('#petRow')), 'save opened: ' + st);
  const opt = await p.evaluate(() => { const o = [...document.querySelectorAll('#petChar option')].find((x) => x.value.startsWith('4018:')); return o ? o.value : ''; });
  ok(!!opt, 'Tama list has Meowtchi (4018)');
  await p.selectOption('#petChar', opt); await p.selectOption('#petLevel', '7'); await p.click('#btnPetSave'); await p.waitForTimeout(800);
  const dlp = p.waitForEvent('download', { timeout: 300000 }); await p.click('#btnSave');
  const dl = await dlp; const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1));
  const pet = SV.paraPet({ pages: SV.paraFromImage(img, 8), edition: 8 }, 8);
  ok(pet && pet.id === 4018 && pet.level === 7, 'ROM save: Tama ' + (pet && pet.id) + ', level ' + (pet && pet.level));
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? 'FAIL ' + bad : 'all good'); process.exit(bad ? 1 : 0);
})();
