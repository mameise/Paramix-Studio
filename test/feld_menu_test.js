const fs = require('fs'), os = require('os'), path = require('path');
const { execFileSync } = require('child_process');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'paramix-menu-'));
try {
  const bin = path.join(tmp, 'menu-test');
  execFileSync(process.env.CC || 'cc', ['-w', '-DFELD_MENU7', '-fsanitize=address,undefined', '-g',
    path.join(__dirname, 'feld_menu_test.c'), '-o', bin], { stdio: 'inherit' });
  execFileSync(bin, [tmp], { stdio: 'inherit', timeout: 120000 });
} finally { fs.rmSync(tmp, { recursive: true, force: true }); }
