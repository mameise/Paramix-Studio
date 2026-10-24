// V1Emu: names and flags from the ROM (test31: the English P1, Yasashii's segment order).
// node test/v1_names_test.js <VintageTamas folder> <land.bin>
const fs = require('fs'), E = require('../src/emu.js');
const dir = process.argv[2], land = new Uint8Array(fs.readFileSync(process.argv[3]));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../v1/v1.bin')), stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
let bad = 0; const ok = (c, m) => { if (!c) { bad++; console.log('FAIL', m); } else console.log('ok', m); };
const want = { 'tama.bin': ['TAMA P1 EN', 8, 0], 'TamagotchiP1J.bin': ['TAMA P1', 8, 0], 'TamagotchiYasashii.bin': ['YASASHII', 0, 128], 'DigimonV1.bin': ['DIGIMON V1', 8, 0] };
for (const [f, [nm, clk, dir7]] of Object.entries(want)) {
  const p = dir + '/' + f; if (!fs.existsSync(p)) { console.log('skip', f); continue; }
  const r = new Uint8Array(fs.readFileSync(p)); const a = E.v1Patch(land, r, app, stub, {}); const o = a.rom || a;
  const dv = new DataView(o.buffer, o.byteOffset), fl = dv.getUint32(0xA0000 + 0x140 + 20, true);
  const name = Buffer.from(o.slice(0xA0000 + 0x158, 0xA0000 + 0x168)).toString().replace(/\0.*$/, '');
  ok(E.v1Name(r) === nm && name === nm, f + ' name ' + name);
  ok((fl & 8) === clk && (fl & 128) === dir7, f + ' flags ' + fl.toString(2));
}
console.log(bad ? 'FAILED ' + bad : 'all good');
