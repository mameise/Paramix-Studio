// 1.6.8: a T7Emu or V1Emu that changes its place (main program <-> menu) takes its save along when writing.
// node test/emu_movesave_test.js <land.bin> <tama-go.bin> <p2.bin>
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js'), FL = require(S + 'src/flash.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, go, p2] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const progs = { t7: rd(S + 't7/t7.bin'), tama: rd(S + 'emu/emu.bin'), t18: rd(S + 't18/t18.bin'), v1: rd(S + 'v1/v1.bin') }; progs.t18.xip = rd(S + 't18/t18x.bin');
const stub = rd(S + 'emu/stub_tpl.bin'), fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
const G = rd(go), P = rd(p2);
function build(main, xs) {
  let rom = main === 'go' ? E.t7Patch(base, G, [], progs.t7, stub, {}) : E.v1Patch(base, P, progs.v1, stub, {});
  const r = E.appsPlace(rom, xs, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
  return F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E);
}
(async () => {
  const a = build('go', [{ kind: 'v1', rom: P }]);
  const xa = E.appsRead(a, F.feldApps(a, fe.stub).apps)[0];
  const device = new Uint8Array(a);
  device.fill(0x11, 0xC0000, 0xC6000);               // Tama-Go as the main program: its save
  device.fill(0x22, xa.saveOff, xa.saveOff + 0x2000);  // P2 in the menu: its session and settings
  const b = build('p2', [{ rom: G }]);
  const xb = E.appsRead(b, F.feldApps(b, fe.stub).apps)[0];
  const changed = new Set(); for (let s = 0; s < 0x1000000; s += 0x1000) if (!Buffer.from(device.subarray(s, s + 0x1000)).equals(Buffer.from(b.subarray(s, s + 0x1000)))) changed.add(s);
  const dev = { info: { edition: F.edition(b).id }, read: async (x, n) => device.slice(x, x + n), crc: async (x, n) => FL.crc32(device, x, n) };
  const plan = await FL.keepPlan(F, E, { stub: fe.stub }, {})(dev, b, changed);
  ok(plan.image.subarray(0xC0000, 0xC2000).every((v) => v === 0x22), 'P2 now the main program: its save at 0x0C0000');
  ok(plan.image.subarray(xb.saveOff, xb.saveOff + 0x6000).every((v) => v === 0x11), 'Tama-Go now in the menu: its save at 0x' + xb.saveOff.toString(16));
  ok(plan.notes.filter((n) => n.code === 'noteSaveMoved').length === 2, 'notes: ' + plan.notes.map((n) => n.code).join(','));
  console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
