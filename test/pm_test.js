// 1.7.3 PokeMiniEmu: Pokemon mini games into the menu, with and without an own BIOS; everything reads back; the
// program image in the flash has its settings block filled in.
// node test/pm_test.js <land.bin> <game.min> [<bios.min>]
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, game, bios] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const progs = { pm: rd(S + 'pm/pm.bin') };
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
const R = rd(game), B = bios ? rd(bios) : null;
ok(E.pmDetect(R) && !E.gbDetect(R), 'Pokemon mini game recognised');
ok(!B || E.pmBiosDetect(B), 'BIOS recognised');
const xs = [{ kind: 'pm', rom: R, title: E.pmTitle('Pokemon Pinball Mini (USA, Europe).min'), keys: E.PM_KEYS_DEFAULT.slice(), inv: true, bios: B }];
const r = E.appsPlace(base, xs, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
const m = F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E);
const back = E.appsRead(m, F.feldApps(m, fe.stub).apps);
const b0 = back.find((x) => x.kind === 'pm');
ok(!!b0 && Buffer.compare(Buffer.from(b0.rom), Buffer.from(R)) === 0, 'game the same after reading back');
const c = b0.off + 0x140;
ok(u32(m, c) === E.PM_MAGIC && u32(m, c + 8) % 0x10000 === 0, 'settings block, game 64 KB aligned at ' + u32(m, c + 8).toString(16));
ok(b0.title === 'PINBALL MINI', 'name ' + b0.title);
ok(JSON.stringify(b0.keys) === JSON.stringify(E.PM_KEYS_DEFAULT), 'keys ' + b0.keys.join(','));
const fl = u32(m, c + 20);
if (B) ok((fl & 4) && Buffer.compare(Buffer.from(m.subarray(u32(m, c + 64), u32(m, c + 64) + 0x1000)), Buffer.from(B)) === 0 && b0.bios && b0.bios.length === 0x1000, 'own BIOS in the save area');
else ok(!(fl & 4) && u32(m, c + 64) === 0, 'free BIOS');
ok(Buffer.compare(Buffer.from(m.subarray(b0.off, b0.off + 0x140)), Buffer.from(progs.pm.subarray(0, 0x140))) === 0, 'program: PokeMiniEmu');
const again = E.appsPlace(E.appsClear(m, back), back, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
ok(!!again.rom, 'placed again from what was read');
console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
