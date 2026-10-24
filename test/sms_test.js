// 1.6.10 test2: Master System detection and placing with a made-up ROM (no game): header, Game Gear, file name, copier
// header, the settings block, read back. node test/sms_test.js
const E = require('../src/emu.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const mk = (n, region) => { const r = new Uint8Array(n); if (region !== undefined) { r.set(Buffer.from('TMR SEGA'), 0x7FF0); r[0x7FFF] = region << 4 | 0xC; } return r; };
ok(E.smsDetect(mk(0x20000, 4), 'x.bin'), 'export header');
ok(E.smsDetect(mk(0x20000, 3), 'x.bin'), 'Japanese header');
ok(!E.smsDetect(mk(0x20000, 6), 'x.gg') && E.smsGameGear(mk(0x20000, 6)), 'Game Gear refused');
ok(!E.smsDetect(mk(0x20000), 'x.bin') && E.smsDetect(mk(0x20000), 'x.sms'), 'no header: by the name');
ok(!E.smsDetect(mk(0x1000), 'x.sms') && !E.smsDetect(mk(0x200000), 'x.sms'), 'too small, too big');
{ const r = new Uint8Array(0x20000 + 512); r.set(Buffer.from('TMR SEGA'), 512 + 0x7FF0); r[512 + 0x7FFF] = 0x4C; ok(E.smsDetect(r, 'x.bin'), 'copier header in front'); }
ok(E.smsTitle('Alex Kidd in Miracle World (U) (V1.0).sms') === 'ALEX KIDD MIRAC', 'name from the file');
// place it like the Studio does (into an empty ROM with an app at 0xA0000 is not needed: an empty flash is enough)
const app = E.smsEmbedded() || new Uint8Array(require('fs').readFileSync(__dirname + '/../sms/sms.bin'));
if (app) {
  const rom = new Uint8Array(0x1000000).fill(0xFF), g = mk(0x20000, 4); g[0] = 0xF3;
  const res = E.appsPlace(rom, [{ kind: 'sms', rom: g, title: 'TEST', keys: E.SMS_KEYS_DEFAULT.slice(), inv: true }], { sms: app }, { launch: true });
  const off = res.list[0].off, c = E.extraCfg(res.rom, off);
  ok(c && c.kind === 'sms' && c.romLen === g.length && c.saveOff === off + 0x10000, 'settings block');
  const back = E.appsRead(res.rom, res.list)[0];
  ok(back && back.kind === 'sms' && back.title === 'TEST' && back.inv && back.keys[6] === 64, 'read back');
} else console.log('(no embedded program: placing skipped)');
console.log(bad ? 'FAIL ' + bad : 'all good');
process.exit(bad ? 1 : 0);
