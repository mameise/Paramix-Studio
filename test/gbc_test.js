// 1.7.2 GBCEmu: Game Boy Color games get the colour program, games over 1 MB are stored in pages, everything reads back.
// node test/gbc_test.js <land.bin> <game.gbc> [<game.gb>]
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, gbc, gb] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const progs = { gb: rd(S + 'gb/gb.bin'), gbc: rd(S + 'gb/gbc.bin') };
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
const R = rd(gbc), xs = [{ kind: 'gb', rom: R, keys: E.GB_KEYS_DEFAULT.slice(), inv: true }];
if (gb) xs.push({ kind: 'gb', rom: rd(gb), keys: E.GB_KEYS_DEFAULT.slice(), inv: true });
ok(E.gbDetect(R) && E.gbIsColor(R), 'colour game recognised');
const r = E.appsPlace(base, xs, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
const m = F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E);
const back = E.appsRead(m, F.feldApps(m, fe.stub).apps);
ok(back.length === xs.length, 'programs read back: ' + back.length);
const b0 = back.find((x) => x.kind === 'gb' && E.gbIsColor(x.rom));
ok(!!b0 && Buffer.compare(Buffer.from(b0.rom), Buffer.from(R)) === 0, 'colour game the same after reading back (' + (R.length >> 10) + ' KB)');
const fl = u32(m, b0.off + 0x140 + 20);
ok(!!(fl & 8) && (R.length > 0x100000) === !!(fl & 4), 'flags: colour' + (fl & 4 ? ', pages' : ''));
ok(Buffer.compare(Buffer.from(m.subarray(b0.off, b0.off + 0x140)), Buffer.from(progs.gbc.subarray(0, 0x140))) === 0, 'program: GBCEmu');
ok(b0.save.length === 0x18000, 'save area 96 KB');
if (gb) { const b1 = back.find((x) => x !== b0); ok(Buffer.compare(Buffer.from(m.subarray(b1.off, b1.off + 0x140)), Buffer.from(progs.gb.subarray(0, 0x140))) === 0 && !(u32(m, b1.off + 0x140 + 20) & 8), 'Game Boy game keeps GBEmu'); }
const again = E.appsPlace(E.appsClear(m, back), back, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
ok(!!again.rom, 'placed again from what was read');
console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
