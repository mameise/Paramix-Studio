// node upg.js <image with TamaEmu> <emu.bin> <out>: the program replaced as the Studio does (emuUpgrade), session kept
const fs = require('fs');
const E = require('../../src/emu.js');
const [img, app, out] = process.argv.slice(2);
const r = E.emuUpgrade(new Uint8Array(fs.readFileSync(img)), new Uint8Array(fs.readFileSync(app)));
fs.writeFileSync(out, r || fs.readFileSync(img)); console.log(r ? 'upgraded' : 'same');
