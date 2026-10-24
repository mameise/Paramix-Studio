// 1.6.8 (test5): Gotchi Points of a Tama-Go (T7Emu as the main program) in the Emulator tab: EEPROM 0x030.., BCD.
// node test/ui_t7gp_test.js <rom_with_t7emu_and_save.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const SV = require('../src/saves.js');
(async () => {
  const [rom, out] = process.argv.slice(2); fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  // a save with 500 points
  let r = new Uint8Array(fs.readFileSync(rom)); const s0 = SV.t7SaveOut(r); ok(!!s0, 'the ROM has a T7Emu save');
  s0.set([0x00, 0x05, 0, 0, 0], 0x32); r = SV.t7SaveIn(r, s0); fs.writeFileSync(out + '/gp_in.bin', r);
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 1000 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', out + '/gp_in.bin'); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(1500);
  await p.click('.tab[data-tab="emu"]'); await p.locator('#appsList .prow .ptog').first().click(); await p.waitForTimeout(400);
  const inp = p.locator('.pdet input[type=number]').first();
  ok((await inp.inputValue()) === '500', 'shows 500');
  await inp.fill('123456'); await inp.dispatchEvent('change'); await p.waitForTimeout(400);
  const dl = p.waitForEvent('download'); await p.click('#btnSave'); const d = await dl; await d.saveAs(out + '/gp_out.bin');
  const o = new Uint8Array(fs.readFileSync(out + '/gp_out.bin')), s = SV.t7SaveOut(o);
  ok(s && [...s.subarray(0x32, 0x37)].join() === [0x56, 0x34, 0x12, 0, 0].join(), 'EEPROM holds 123456 (BCD)');
  ok(o.subarray(0xC0000, 0xC0010).every((v) => v === 0xFF), 'running state dropped');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
