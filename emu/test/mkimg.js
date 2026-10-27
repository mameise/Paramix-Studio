// node mkimg.js <paradise.bin> <guest firmware> <emu.bin> <out.bin> [lang en|de]
// The Paradise image with TamaEmu as the main program (as the Studio builds it), for devemu.py
const fs = require('fs');
const E = require('../../src/emu.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [rom, fw, app, out, lang] = process.argv.slice(2);
const r = E.emuPatch(rd(rom), rd(fw), rd(app), rd(__dirname + '/../stub_tpl.bin'), { lang: lang || 'de' });
fs.writeFileSync(out, r.rom);
console.log('device', r.device, 'blocks', r.blocks, 'big', r.big);
