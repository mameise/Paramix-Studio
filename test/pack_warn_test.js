// 1.7.5 PACK_WARN: a packed colour Tama is reported (the Studio asks before saving or writing); the 2 or 3 cold blocks
// the main TamaEmu always packs for its spare pages do not count.
// node test/pack_warn_test.js <land.bin> <idl.bin>
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js'), SV = require(S + 'src/saves.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, idl] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const progs = { t7: rd(S + 't7/t7.bin'), tama: rd(S + 'emu/emu.bin'), t18: rd(S + 't18/t18.bin'), v1: rd(S + 'v1/v1.bin') }; progs.t18.xip = rd(S + 't18/t18x.bin');
const fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
const G = rd(idl), dev = E.emuDetect(G), stub = rd(S + 'emu/stub_tpl.bin');
// main TamaEmu
for (const pack of [false, true]) {
  const r = E.emuPatch(base, G, progs.tama, stub, { pack }).rom, c = SV.emuCfg(r.subarray(0xA0000, 0xA1000)), n = E.lzCount(r, c.lzOff);
  ok((n > E.PACK_FEW) === pack, 'main, pack ' + pack + ': ' + n + ' blocks packed, ' + (n > E.PACK_FEW ? 'warns' : 'no warning'));
}
// TamaEmu in the menu
for (const pack of [false, true]) {
  const r = E.appsPlace(base, [{ kind: 'tama', guest: G, dev, pack }], progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
  const rom = F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E), l = E.appsPacked(rom, F.feldApps(rom, fe.stub).apps);
  ok(l.length === (pack ? 1 : 0), 'menu, pack ' + pack + ': ' + (l.length ? 'warns for ' + l.join(', ') : 'no warning'));
}
// PACK_ASK: a long save in the menu only fits packed: not packed by itself, asked for; with "pack" it goes in
{ const g = new Uint8Array(G.length).fill(0xFF); g.set(G); const ff = (a) => g.subarray(a, a + 0x10000).every((v) => v === 0xFF);
  let used = 0; for (let b = 0; b < 128; b++) if (!ff(b << 16)) used++;
  let x = 7; for (let b = 1; b < 128 && used < 92; b++) if (ff(b << 16)) { for (let i = 0; i < 0x10000; i++) { x = (Math.imul(x, 1103515245) + 12345) >>> 0; g[(b << 16) + i] = x >>> 24; } used++; }
  let msg = '', need = false; try { E.appsPlace(base, [{ kind: 'tama', guest: g, dev, pack: false }], progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') }); } catch (e) { msg = e.message; need = !!e.needPack; }
  ok(need, 'menu, long save (' + used + ' blocks) without pack: ' + msg);
  let r2 = null; try { r2 = E.appsPlace(base, [{ kind: 'tama', guest: g, dev, pack: true }], progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') }); } catch (e) { console.log(e.message); }
  ok(!!r2, 'menu, long save with pack: placed'); }
console.log(bad ? bad + ' failed' : 'all good'); process.exit(bad ? 1 : 0);
