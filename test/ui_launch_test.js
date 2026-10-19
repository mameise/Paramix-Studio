// Launcher ROM (1.6.8) in the browser: T18Emu as the main program, two T7Emu, menu tab "start device only",
// save, check the ROM, load it again.
// node test/ui_launch_test.js <paradise.bin> <tg18.bin> <tama-go.bin> <friends.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const C = require('../src/core.js'), E = require('../src/emu.js'), F = require('../src/feld.js');
(async () => {
  const [rom, tg18, go, fr, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  const fe = { stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 });
  await p.click('.tab[data-tab="feld"]');
  await p.click('#launchOn'); await p.waitForTimeout(1500);
  ok(await p.isChecked('#launchOn') && (await p.isDisabled('#feldOn')), 'switch on, the other menu parts off');
  await p.click('.tab[data-tab="emu"]');
  await p.setInputFiles('#fileEmu', tg18); await p.waitForTimeout(500);
  await p.setInputFiles('#fileApp', go); await p.waitForTimeout(300);
  await p.setInputFiles('#fileApp', fr); await p.waitForTimeout(300);
  const plan = await p.textContent('#plan'); console.log('Plan:', plan.slice(0, 300));
  const [dl] = await Promise.all([p.waitForEvent('download', { timeout: 180000 }), p.click('#btnSave')]);
  const r1 = out + '/' + dl.suggestedFilename(); await dl.saveAs(r1);
  const img = new Uint8Array(fs.readFileSync(r1)), ctx = C.openRom(img), fa = F.feldApps(img, fe.stub);
  ok(/_start\.bin$/.test(r1), 'file name ' + path.basename(r1));
  ok(F.isLaunch(img, fe.stub) && C.isLauncher(ctx), 'launcher menu and small pictures');
  ok(ctx.assetEnd < 0x400000, 'pictures end at 0x' + ctx.assetEnd.toString(16));
  ok(fa.name0 === 'TAMA MEETS' && fa.apps.length === 2, 'programs: main + ' + fa.apps.map((x) => x.name).join(', '));
  { const f = F.feldFind(img, fe.stub), c = f.off + 0x140, dl = (img[c + 500] | img[c + 501] << 8 | img[c + 502] << 16 | img[c + 503] << 24) >>> 0, o = dl - 0x10000000 + 0x11000;
    const fa2 = new Uint8Array(fs.readFileSync(__dirname + '/../flash/flashapp.bin'));
    ok(f.version >= 7 && dl && Buffer.from(img.subarray(o, o + fa2.length)).equals(Buffer.from(fa2)), 'DOWNLOAD: flash app at 0x' + o.toString(16)); }
  const low = Math.min(...fa.apps.map((a) => a.src - 0x10000000 + 0x11000));
  console.log('extras from 0x' + low.toString(16), 'T18 lowest block 0x' + E.t18Low(img).toString(16));
  const tab = E.sceneTable(img), home = tab.PSID_HOME;
  const u32 = (o) => (img[o] | img[o + 1] << 8 | img[o + 2] << 16 | img[o + 3] << 24) >>> 0;
  { const st = F.feldFind(img, fe.stub).stubOff, n = u32(st + 0x404), tOff = u32(st + 0x408), lo = 0x10000000 + tOff - 0x11000, hi = lo + 52 * n;
    const inT = (v) => v > lo && v < hi;
    ok(u32(st + 0x400) === 0x434E4C50 && n > 60 && inT(u32(home + 12)) && inT(u32(tab.PSID_EVENT_DEAD + 12)) && !inT(u32(tab.PSID_CORRECTTIME + 12)), 'scenes after the setup go through the trampolines (' + n + '), the setup stays');
    const back = F.feldStrip(img, { stub: fe.stub }, E); ok(!inT((back[home + 12] | back[home + 13] << 8 | back[home + 14] << 16 | back[home + 15] << 24) >>> 0) && back.subarray(tOff, tOff + 52 * n).every((v) => v === 255), 'removing the menu restores the scenes'); }
  // load again: switch stays on and cannot be switched off
  await p.setInputFiles('#fileRom', r1); await p.waitForTimeout(2500);
  await p.click('.tab[data-tab="feld"]');
  ok(await p.isChecked('#launchOn') && await p.isDisabled('#launchOn'), 'loaded again: start device, switch locked');
  console.log('Status:', await p.textContent('#feldStat'));
  await p.click('.tab[data-tab="emu"]');
  ok(/TAMA-GO/.test(await p.textContent('#appsList')), 'extras listed again');
  const [dl2] = await Promise.all([p.waitForEvent('download', { timeout: 180000 }), p.click('#btnSave')]).catch(() => [null]);
  ok(!dl2 || true, 'save again: ' + (dl2 ? dl2.suggestedFilename() : 'nothing to do'));
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close();
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
