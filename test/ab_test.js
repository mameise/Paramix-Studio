// 1.7.3 ArduboyEmu: Arduboy games (.hex) into the menu; everything reads back; the program image in the flash has its
// settings block filled in; the game and its save share one 64 KB block.
// node test/ab_test.js <land.bin> <game.hex> [<game2.hex>]
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, game, game2] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const progs = { ab: rd(S + 'ab/ab.bin') };
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
const R = E.abHex(fs.readFileSync(game, 'latin1'));
ok(!!R && R.length > 1000 && R.length <= 0x8000, 'game read from the .hex (' + (R ? R.length : 0) + ' bytes)');
ok(!E.abHex('hello') && !E.abHex(':1000000000'), 'other text refused');
const xs = [{ kind: 'ab', rom: R, title: E.abTitle(game.split('/').pop()), keys: E.AB_KEYS_DEFAULT.slice(), inv: true }];
if (game2) xs.push({ kind: 'ab', rom: E.abHex(fs.readFileSync(game2, 'latin1')), title: E.abTitle(game2.split('/').pop()), keys: [16, 32, 8, 4, 1, 2, 0, 0, 48], inv: false });
const r = E.appsPlace(base, xs, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
const m = F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E);
const back = E.appsRead(m, F.feldApps(m, fe.stub).apps).filter((x) => x.kind === 'ab');
ok(back.length === xs.length, back.length + ' Arduboy games read back');
const b0 = back[0];
ok(!!b0 && Buffer.compare(Buffer.from(b0.rom), Buffer.from(R)) === 0, 'game the same after reading back');
const c = b0.off + 0x140, ro = u32(m, c + 8), so = u32(m, c + 16);
ok(u32(m, c) === E.AB_MAGIC && ro % 0x10000 === 0 && so === ro + 0x8000, 'settings block, game 64 KB aligned at ' + ro.toString(16) + ', save behind it');
ok(m.subarray(so, so + E.AB_SAVE).every((v) => v === 0xFF), 'fresh save empty');
ok(b0.title === xs[0].title, 'name ' + b0.title);
ok(JSON.stringify(b0.keys) === JSON.stringify(E.AB_KEYS_DEFAULT), 'keys ' + b0.keys.join(','));
if (game2) ok(JSON.stringify(back[1].keys) === '[16,32,8,4,1,2,0,0,48]' && !back[1].inv, 'second game: own keys, wheel not turned');
ok(Buffer.compare(Buffer.from(m.subarray(b0.off, b0.off + 0x140)), Buffer.from(progs.ab.subarray(0, 0x140))) === 0, 'program: ArduboyEmu');
// a save goes along when placed again
b0.save = new Uint8Array(E.AB_SAVE).fill(0xFF); b0.save.set([1, 2, 3, 4], 0x1000);
const again = E.appsPlace(E.appsClear(m, back), back, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
const back2 = E.appsRead(again.rom, again.list).filter((x) => x.kind === 'ab');
ok(back2.length === xs.length && back2[0].save[0x1000] === 1 && back2[0].save[0x1003] === 4, 'EEPROM kept when placed again');
console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
