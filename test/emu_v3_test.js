// 1.6.9: V3Emu (Tamagotchi Connection V3) as the main program and in the menu, handled as a V1Emu.
// node test/emu_v3_test.js <land.bin> <v3 rom 512 KB> <p2.bin> <tama-go.bin>
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, v3p, p2, go] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const progs = { t7: rd(S + 't7/t7.bin'), tama: rd(S + 'emu/emu.bin'), t18: rd(S + 't18/t18.bin'), v1: rd(S + 'v1/v1.bin') }; progs.t18.xip = rd(S + 't18/t18x.bin');
const v3app = rd(S + 'v3/v3.bin'); progs.v1.v3 = v3app;
const stub = rd(S + 'emu/stub_tpl.bin'), fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const V3 = rd(v3p), P = rd(p2), G = rd(go);
ok(E.v3Detect(V3) && E.v1Detect(V3), 'V3 ROM found');
ok(!E.v3Detect(P) && !E.v3Detect(G), 'P2 and Tama-Go are no V3');
ok(E.v1Name(V3) === 'TAMA V3', 'name ' + E.v1Name(V3));
ok(u32(v3app, 0x140) === E.V1_MAGIC, 'V3Emu has the V1Emu settings block');
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
// main program
const m = E.v1Patch(base, V3, progs.v1, stub, { lang: 'de' });
const prog = (rom, off) => rom.subarray(off + 0x300, off + v3app.length);
ok(Buffer.from(prog(m, 0xA0000)).equals(Buffer.from(v3app.subarray(0x300))), 'main: the V3 program at 0x0A0000');
const ro = u32(m, 0xA0148), rl = u32(m, 0xA014C);
ok(rl === 0x80000 && ro % 0x10000 === 0 && Buffer.from(m.subarray(ro, ro + rl)).equals(Buffer.from(V3)), 'main: the ROM at 0x' + ro.toString(16));
ok(E.v1FromRom(m) && E.v1FromRom(m).length === 0x80000, 'main: the ROM read back');
ok(E.v1Upgrade(m, progs.v1) === null, 'main: nothing to upgrade with the same program');
{ const old = new Uint8Array(m); old.fill(0, 0xA0400, 0xA0500); const u = E.v1Upgrade(old, progs.v1); ok(u && Buffer.from(prog(u, 0xA0000)).equals(Buffer.from(v3app.subarray(0x300))), 'main: an old V3 program is renewed with V3Emu, not V1Emu'); }
// a P2 as main program still gets V1Emu
{ const p = E.v1Patch(base, P, progs.v1, stub, {}); ok(Buffer.from(p.subarray(0xA0300, 0xA0300 + 0x1000)).equals(Buffer.from(progs.v1.subarray(0x300, 0x1300))), 'P2: still V1Emu'); }
// in the menu, next to a P2
const before = E.t7Patch(base, G, [], progs.t7, stub, {});
const r = E.appsPlace(before, [{ kind: 'v1', rom: V3 }, { kind: 'v1', rom: P }], progs, { launch: true });
const img = F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E);
const xs = E.appsRead(img, F.feldApps(img, fe.stub).apps);
const x3 = xs.find((x) => x.rom.length === 0x80000), xp = xs.find((x) => x.rom.length !== 0x80000);
ok(x3 && Buffer.from(x3.rom).equals(Buffer.from(V3)) && E.extraName(x3) === 'TAMA V3', 'menu: V3 read back');
ok(xp && Buffer.from(xp.rom).equals(Buffer.from(P)), 'menu: P2 read back');
{ const c = E.extraCfg(img, x3.off); ok(c.romOff % 0x10000 === 0 && c.romOff + 0x80000 === x3.off && c.saveOff === x3.off + 0x10000, 'menu: ROM 64 KB aligned in front of the program, save behind');
  ok(Buffer.from(prog(img, x3.off)).equals(Buffer.from(v3app.subarray(0x300))), 'menu: the V3 program'); }
// clearing frees exactly the V3's ROM, program and save
{ const cl = E.appsClear(img, [x3]), c = E.extraCfg(img, x3.off);
  let outside = 0; for (let a = 0; a < img.length; a += 0x1000) { const inV3 = (a >= c.romOff && a < c.romOff + 0x80000) || (a >= x3.off && a < x3.off + 0x12000); if (!inV3 && !Buffer.from(cl.subarray(a, a + 0x1000)).equals(Buffer.from(img.subarray(a, a + 0x1000)))) outside++; }
  ok(outside === 0 && cl.subarray(c.romOff, c.romOff + 0x80000).every((v) => v === 0xFF) && cl.subarray(x3.off, x3.off + 0x12000).every((v) => v === 0xFF), 'menu: clearing frees only the V3 (' + outside + ' other sectors touched)');
  ok(Buffer.from(cl.subarray(xp.off, xp.off + 0x10000)).equals(Buffer.from(img.subarray(xp.off, xp.off + 0x10000))), 'menu: the P2 stays'); }
ok(E.extraSize({ kind: 'v1', rom: V3 }) === 0x92000, 'size of the V3 in the menu');
if (process.env.OUT) { fs.writeFileSync(process.env.OUT + '/v3main.bin', m); fs.writeFileSync(process.env.OUT + '/v3menu.bin', img); console.log('menu V3 at 0x' + x3.off.toString(16)); }
console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
