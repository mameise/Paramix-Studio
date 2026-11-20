const fs = require('fs'), os = require('os'), path = require('path');
const { execFileSync } = require('child_process');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'paramix-tama-'));
try {
  const bin = path.join(tmp, 'tama-test');
  execFileSync(process.env.CC || 'cc', ['-w', '-fsanitize=address,undefined', '-g',
    path.join(__dirname, 'feld_tama_test.c'), '-o', bin], { stdio: 'inherit' });
  const args=[tmp];
  if (process.argv[2]) {
    const F = require('../src/feld.js'), E = require('../src/emu.js');
    const local = (name) => fs.readFileSync(path.join(__dirname, '..', name));
    const emb = { stub: local('feld/stub_feld_tpl.bin'), feld: local('feld/feld.bin'), oldStub: local('emu/stub_tpl.bin') };
    const input = fs.readFileSync(path.resolve(process.argv[2]));
    const rom = F.feldApply(F.feldStrip(input, emb, E), { fields: true, bag: true, tama: true, paramixShortcut: false }, F.feldSlots(input, emb.stub), emb, E);
    const f = F.feldFind(rom, emb.stub);
    const file = path.join(tmp, 'updated.bin');
    fs.writeFileSync(file, rom);
    args.push(file, String(f.off + 0x140));
  }
  execFileSync(bin, args, { stdio: 'inherit', timeout: 30000 });
} finally { fs.rmSync(tmp, { recursive: true, force: true }); }
