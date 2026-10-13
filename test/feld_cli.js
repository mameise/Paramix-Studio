// Install the field switch into a finished ROM, as the studio does when saving.
// node test/feld_cli.js <rom_in> <rom_out> [aus|felder|items]   (aus = off, felder = fields only, items = items only; default: fields and items)
const fs = require('fs');
const E = require('../src/emu.js'), F = require('../src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const emb = { feld: rd(__dirname + '/../feld/feld.bin'), stub: rd(process.env.STUBTPL || (__dirname + '/../feld/stub_feld_tpl.bin')), oldStub: rd(__dirname + '/../emu/stub_tpl.bin') };
let rom = rd(process.argv[2]);
const saved = F.feldSlots(rom, emb.stub);
rom = F.feldStrip(rom, emb, E);
const w = process.argv[4] || '';
if (w !== 'aus') rom = F.feldApply(rom, { fields: w !== 'items', bag: w !== 'felder' }, saved, emb, E);
fs.writeFileSync(process.argv[3], rom);
console.log(process.argv[3], JSON.stringify(F.feldFind(rom, emb.stub)), 'Programm', ['keins', 'DOOM', 'Emulator'][F.appKind(rom)]);
