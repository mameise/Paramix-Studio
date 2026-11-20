// Host-test the actual on-device menu with synthetic save pages and button input.
const fs = require('fs'), os = require('os'), path = require('path');
const { execFileSync } = require('child_process');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'paramix-bulk-'));
try {
  const bin = path.join(tmp, 'bulk-test');
  execFileSync(process.env.CC || 'cc', ['-w', '-fsanitize=address,undefined', '-g',
    path.join(__dirname, 'feld_bulk_test.c'), '-o', bin], { stdio: 'inherit' });
  execFileSync(bin, [tmp], { stdio: 'inherit' });
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
