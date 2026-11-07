// 1.7.3 test11: edition from the ID page with its footer, Tropical/Glacier first (a Glacier was read as Land when its
// save data at 0xD49000 began with 1..6). node test/edition_test.js <rom.bin...> (the edition is the expected one by name)
const fs = require('fs'), C = require('../src/core.js'), F = require('../src/feld.js'), FL = require('../src/flash.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
(async () => {
  for (const f of process.argv.slice(2)) {
    const b = new Uint8Array(fs.readFileSync(f)), want = C.openRom(b).editionId, nm = f.split('/').pop();
    for (const g of [0, 1, 6]) {
      const r = new Uint8Array(b); if (g && (want === 5 || want === 6)) { r.fill(0, 0xD49000, 0xD49004); r[0xD49000] = g; }
      const p = Object.create(FL.Paradise.prototype), pay = new Uint8Array(44); pay[8] = 1;
      p.link = { request: async () => ({ payload: pay }) }; p.read = async (o, n) => r.slice(o, o + n);
      const i = await p.hello(), e = F.edition(r);
      ok(C.openRom(r).editionId === want && e && e.id === want && i.edition === want, nm + (g ? ' with ' + g + ' at 0xD49000' : '') + ': ' + FL.EDITIONS[want]);
    }
  }
  // 1.7.3 test13: a clean ROM keeps its ID page at 0xEFD000
  for (const f of process.argv.slice(2)) {
    const src = new Uint8Array(fs.readFileSync(f)), want = C.openRom(src).editionId, b = F.cleanBase(src), e = F.edition(b);
    const p = Object.create(FL.Paradise.prototype), pay = new Uint8Array(44); pay[8] = 1;
    p.link = { request: async () => ({ payload: pay }) }; p.read = async (o, n) => b.slice(o, o + n);
    const i = await p.hello();
    ok(e && e.id === want && e.at === 0xEFD000 && i.edition === want && b.subarray(0xD29000, 0xD4A000).every((v) => v === 0xFF), f.split('/').pop() + ' clean: ID page at 0xEFD000, ' + FL.EDITIONS[want]);
  }
  console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
