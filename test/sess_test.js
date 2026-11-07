// 1.7.3 test7: "remember where you left off" (flags bit 12) per program and no clock from the Paradise (clean ROM, clock
// not set at the start): written by appsPlace, read back by appsRead.
// node test/sess_test.js <land.bin> <game.hex> <game.gb> <game.sms> [<v1 rom.bin>]
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, hex, gb, sms, v1] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const progs = { ab: rd(S + 'ab/ab.bin'), gb: rd(S + 'gb/gb.bin'), sms: rd(S + 'sms/sms.bin'), v1: rd(S + 'v1/v1.bin') };
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
const xs = [
  { kind: 'ab', rom: E.abHex(fs.readFileSync(hex, 'latin1')), title: 'A', keys: E.AB_KEYS_DEFAULT.slice(), inv: true, sess: false },
  { kind: 'ab', rom: E.abHex(fs.readFileSync(hex, 'latin1')), title: 'B', keys: E.AB_KEYS_DEFAULT.slice(), inv: true },
  { kind: 'gb', rom: rd(gb), keys: E.GB_KEYS_DEFAULT.slice(), inv: true, sess: false },
  { kind: 'sms', rom: rd(sms), title: 'S', keys: E.SMS_KEYS_DEFAULT.slice(), inv: true, sess: false },
];
if (v1) xs.push({ kind: 'v1', rom: rd(v1), clock: true, sess: false });
for (const noClock of [false, true]) {
  const r = E.appsPlace(base, xs, progs, { launch: true, noClock, dlApp: rd(S + 'flash/flashapp.bin') });
  const m = F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E);
  const back = E.appsRead(m, F.feldApps(m, fe.stub).apps);
  ok(back.length === xs.length, back.length + ' programs read back');
  xs.forEach((x, i) => ok(back[i] && back[i].sess === (x.sess !== false), x.kind + ' ' + i + ': remember ' + (x.sess !== false)));
  ok(back[2].inv && back[3].inv, 'other flags kept (wheel turned)');
  if (v1) { const b = back.find((y) => y.kind === 'v1'); ok(b && b.clock === !noClock, 'V1 clock from the Paradise ' + !noClock); }
}
console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
