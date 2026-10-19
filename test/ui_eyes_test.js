// 1.6.8: move the eyes of a figure in the drawer; the composite entries of the saved ROM follow.
// node test/ui_eyes_test.js <paradise.bin> <out_dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const C = require('../src/core.js');
(async () => {
  const [rom, out] = process.argv.slice(2);
  fs.mkdirSync(out, { recursive: true });
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('paramix-lang', 'de'); } catch (e) {} });
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await p.setInputFiles('#fileRom', rom); await p.waitForSelector('#work.on', { timeout: 60000 }); await p.waitForTimeout(2000);
  const ctx = C.openRom(new Uint8Array(fs.readFileSync(rom))), g = ctx.adult;
  const before = C.eyeRelGet(ctx.files[0], g, g + 1, 64, 32), beforeS = C.eyeRelGet(ctx.files[0], g + 3, g + 4, 32, 16);
  await p.click('#gridFigs .tile[data-idx="' + g + '"]'); await p.waitForTimeout(500);
  ok(!(await p.isHidden('#dEyes')), 'eye row in the drawer');
  await p.click('[data-eye="1,0"]'); await p.click('[data-eye="1,0"]'); await p.click('[data-eye="0,-1"]');
  const mBefore = C.eyeRelGet(ctx.files[0], g, g + 2, 64, 32);
  await p.click('[data-mouth="0,1"]'); await p.click('[data-mouth="0,1"]'); await p.click('[data-mouth="-1,0"]');
  ok((await p.textContent('#dMouthVal')) === '-1 / +2', 'mouth shift shown: ' + await p.textContent('#dMouthVal'));
  ok((await p.textContent('#dEyeVal')) === '+2 / -1', 'shift shown: ' + await p.textContent('#dEyeVal'));
  // poses: step through, then move the eyes of one pose only
  const nr0 = await p.textContent('#dPoseNr'); ok(/^Pose \d+ von \d+$/.test(nr0), 'pose counter ' + nr0);
  await p.click('#dPoseNext'); const nr1 = await p.textContent('#dPoseNr'); ok(nr1 !== nr0, 'next pose ' + nr1);
  const poses = C.facePoses(ctx.files[0], g, g + 1, g + 2), pi = +nr1.split(' ')[1] - 1, pl = poses[pi].eye;
  await p.check('#dPoseOnly'); await p.click('[data-eye="0,1"]'); await p.click('[data-eye="0,1"]'); await p.click('[data-eye="0,1"]');
  ok((await p.textContent('#dEyeVal')) === '0 / +3', 'pose only value: ' + await p.textContent('#dEyeVal'));
  await p.uncheck('#dPoseOnly'); ok((await p.textContent('#dEyeVal')) === '+2 / -1', 'all poses value back');
  await p.click('#dFaceSmall'); ok(/^Pose 1? ?\d* ?/.test(await p.textContent('#dPoseNr')) && await p.$eval('#dFaceSmall', (e) => e.classList.contains('on')), 'small figure view');
  await p.click('#dFaceBig');
  await p.screenshot({ path: out + '/eyes.png' });
  await p.click('#dClose');
  const [dl] = await Promise.all([p.waitForEvent('download', { timeout: 300000 }), p.click('#btnSave')]);
  const f = out + '/eyes.bin'; await dl.saveAs(f);
  const c2 = C.openRom(new Uint8Array(fs.readFileSync(f)));
  const after = C.eyeRelGet(c2.files[0], g, g + 1, 64, 32), afterS = C.eyeRelGet(c2.files[0], g + 3, g + 4, 32, 16);
  ok(afterS[0] === beforeS[0] + 1 && afterS[1] === beforeS[1] - 1, 'small eyes moved ' + beforeS + ' -> ' + afterS);
  ok(C.eyeRelGet(c2.files[0], g + 6, g + 7, 64, 32) + '' === C.eyeRelGet(ctx.files[0], g + 6, g + 7, 64, 32) + '', 'next figure unchanged');
  { const P2 = C.paraLayers(c2.files[0]), P1 = C.paraLayers(ctx.files[0]);
    const at = (P, o) => P.layers.find((l) => l.oy === o);
    const yb = at(P1, pl.oy).y, ya = at(P2, pl.oy).y, others = poses.filter((q, k) => k !== pi && q.eye.oy !== pl.oy);
    ok(ya === yb - 1 + 3, 'single pose: eyes ' + yb + ' -> ' + ya + ' (all -1, this pose +3)');
    ok(others.every((q) => at(P2, q.eye.oy).y === at(P1, q.eye.oy).y - 1 && P2.layers.find((l) => l.ox === q.eye.ox).x === P1.layers.find((l) => l.ox === q.eye.ox).x + 2), 'big eyes moved +2 / -1 in the other ' + others.length + ' poses'); }
  const mAfter = C.eyeRelGet(c2.files[0], g, g + 2, 64, 32);
  ok(mAfter[0] === mBefore[0] - 1 && mAfter[1] === mBefore[1] + 2, 'mouth moved ' + mBefore + ' -> ' + mAfter);
  const mouth = (cx) => C.paraLayers(cx.files[0]).layers.filter((l) => l.img === g + 2).map((l) => l.x + ',' + l.y).join(' ');
  ok(mouth(c2) !== mouth(ctx), 'mouth layer moved by its own buttons only');
  const pv = await p.evaluate(() => 1); ok(pv === 1, 'page alive');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
