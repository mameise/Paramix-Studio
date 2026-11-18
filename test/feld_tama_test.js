const fs = require('fs'), os = require('os'), path = require('path');
const { execFileSync } = require('child_process');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'paramix-tama-'));
try {
  const bin = path.join(tmp, 'tama-test');
  execFileSync(process.env.CC || 'cc', ['-w', '-fsanitize=address,undefined', '-g',
    path.join(__dirname, 'feld_tama_test.c'), '-o', bin], { stdio: 'inherit' });
  const args=[tmp];
  if(process.argv[2]){const rom=path.resolve(process.argv[2]),F=require('../src/feld.js'),f=F.feldFind(fs.readFileSync(rom),fs.readFileSync(path.join(__dirname,'../feld/stub_feld_tpl.bin')));if(!f)throw Error('no installed menu');args.push(rom,String(f.off+0x140));}
  execFileSync(bin, args, { stdio: 'inherit', timeout: 30000 });
} finally { fs.rmSync(tmp, { recursive: true, force: true }); }
