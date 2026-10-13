// Comparison: emulator install in JS against the Python builder (same result)
const fs = require('fs');
const E = require('../src/emu.js');
const rom = new Uint8Array(fs.readFileSync(process.argv[2]));
const guest = new Uint8Array(fs.readFileSync(process.argv[3]));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../emu/emu.bin'));
const stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
console.log('erkannt:', E.emuDetect(guest));
const r = E.emuPatch(rom, guest, app, stub);
console.log('Bloecke', r.blocks);
if (process.argv[4]) {
  const ref = new Uint8Array(fs.readFileSync(process.argv[4]));
  let diff = 0, first = -1; for (let i = 0; i < ref.length; i++) if (ref[i] !== r.rom[i]) { diff++; if (first < 0) first = i; }
  console.log('Unterschiede zum Python-Bauer:', diff, first >= 0 ? '0x' + first.toString(16) : '');
}
// install again (replace) and remove again
const r2 = E.emuPatch(r.rom, guest, app, stub);
let d2 = 0; for (let i = 0; i < r2.rom.length; i++) if (r2.rom[i] !== r.rom[i]) d2++;
console.log('zweiter Einbau gleich:', d2 === 0);
const back = E.emuRemove(r.rom);
let d3 = 0; for (let i = 0; i < back.length; i++) if (back[i] !== rom[i]) d3++;
console.log('Entfernen stellt Original her:', d3 === 0, d3);
// add a download game, against the Python builder
if (process.argv[5]) {
  const rec = E.dlcRecord(new Uint8Array(fs.readFileSync(process.argv[5])));
  const g2 = E.dlcPut(guest, rec, 1);
  console.log('Plaetze vorher', E.dlcSlots(guest).map(x => x.name), 'nachher', E.dlcSlots(g2).map(x => x.name));
  if (process.argv[6]) { const ref = fs.readFileSync(process.argv[6]); console.log('wie dlc.py:', Buffer.from(g2).equals(ref)); }
}
