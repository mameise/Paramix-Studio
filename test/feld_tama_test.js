const fs = require('fs'), os = require('os'), path = require('path');
const { execFileSync } = require('child_process');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'paramix-tama-'));
try {
  const bin = path.join(tmp, 'tama-test');
  execFileSync(process.env.CC || 'cc', ['-w', '-fsanitize=address,undefined', '-g',
    path.join(__dirname, 'feld_tama_test.c'), '-o', bin], { stdio: 'inherit' });
  execFileSync(bin, [tmp], { stdio: 'inherit', timeout: 30000 });
} finally { fs.rmSync(tmp, { recursive: true, force: true }); }
