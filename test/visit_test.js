// Simulated playdate partner (src/visit.js) against a scripted initiator. Test key and random data only.
const assert = require('assert');
const V = require('../src/visit.js');
const key = V.parseKey('TestKeyOnly-NotTheReal-0123456789');
assert(key, 'key');
assert.strictEqual(V.parseKey('short'), null);
const enc = (s) => new TextEncoder().encode(s);

async function chunk(sid, type, idx, data) {
  const p = new Uint8Array(12 + data.length);
  p[0] = sid & 255; p[1] = sid >> 8 & 255; p[2] = sid >> 16 & 255; p[3] = sid >>> 24;
  p[4] = 0x54; p[5] = 0x43; p[6] = 0x50; p[7] = type; p[8] = idx; p[9] = 255 - idx;
  const c = V.crc16(data); p[10] = c & 255; p[11] = c >> 8; p.set(data, 12);
  const n = new Uint8Array([idx, 7, 9, 11]), e = await V.crypt(n, key, p), all = new Uint8Array(4 + e.length);
  all.set(n); all.set(e, 4); return all;
}

async function run(breed) {
  const lines = [], bin = [];
  let rxbuf = new Uint8Array(0), want = 0, waiters = [];
  const log = [];
  const v = V.createVisitor({ edition: 'sky', key, log: (e, i) => log.push([e, i]),
    send: async (b) => { const a = new Uint8Array(rxbuf.length + b.length); a.set(rxbuf); a.set(b, rxbuf.length); rxbuf = a; drain(); } });
  function drain() {
    for (;;) {
      if (want) { if (rxbuf.length < want) return; bin.push(rxbuf.slice(0, want)); rxbuf = rxbuf.slice(want); want = 0; continue; }
      let j = -1; for (let i = 0; i + 1 < rxbuf.length; i++) if (rxbuf[i] === 13 && rxbuf[i + 1] === 10) { j = i; break; }
      if (j < 0) return;
      lines.push(new TextDecoder().decode(rxbuf.subarray(0, j))); rxbuf = rxbuf.slice(j + 2);
      const w = waiters; waiters = []; w.forEach((f) => f());
    }
  }
  const tick = () => new Promise((r) => setTimeout(r, 5));
  async function expect(s) { for (let i = 0; i < 400; i++) { const k = lines.indexOf(s); if (k >= 0) { lines.splice(k, 1); return; } await tick(); } throw new Error('missing ' + s + ' have ' + lines.join('|')); }
  const sid = 0xEB24098E;
  async function sendPacket(type, data) {
    await v.feed(enc('PKT ' + data.length + '\r\n')); await expect('ACK');
    for (let i = 0; i * 0x1000 < data.length; i++) { await v.feed(await chunk(sid, type, i, data.subarray(i * 0x1000, (i + 1) * 0x1000))); await expect('ACK'); }
  }
  await sendPacket(0x11, new Uint8Array([0xAB, 0xAB]));
  const mine = new Uint8Array(0x20014); for (let i = 0; i < mine.length; i++) mine[i] = (i * 31 + 7) & 255;
  mine[0x20002] = 1; mine[0x20004] = 0x8E;
  await sendPacket(0x01, mine);
  // the visitor answers with its own packet
  await expect('PKT 131092');
  const got = new Uint8Array(0x20014); let off = 0;
  want = 16 + 0x1000; await v.feed(enc('ACK\r\n'));
  for (let i = 0; i < 33; i++) {
    for (let k = 0; k < 400 && !bin.length; k++) await tick();
    const c = bin.shift(); assert(c, 'chunk ' + i);
    const p = await V.crypt(c.subarray(0, 4), key, c.subarray(4));
    assert.strictEqual(String.fromCharCode(p[4], p[5], p[6]), 'TCP');
    assert.strictEqual((p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24) >>> 0, sid, 'session id');
    assert.strictEqual(p[8], i); assert.strictEqual(p[9], 255 - i);
    const d = p.subarray(12); assert.strictEqual(V.crc16(d), p[10] | p[11] << 8, 'crc');
    got.set(d, off); off += d.length;
    const left = 0x20014 - off; want = left ? 16 + Math.min(0x1000, left) : 0;
    await v.feed(enc('ACK\r\n'));
  }
  assert.strictEqual(off, 0x20014);
  assert.deepStrictEqual(got.subarray(0, 0x20000), mine.subarray(0, 0x20000), 'ghost and screenshot are the player\'s own');
  assert.strictEqual(got[0x20002], 3, 'rom_type Sky');
  assert.strictEqual(got[0x20004] | got[0x20005] << 8 | got[0x20006] << 16 | got[0x20007] << 24, 0x504D5803, 'own device UID');
  for (let k = 0; k < 100 && !log.some((x) => x[0] === 'sent'); k++) await tick();
  await v.feed(enc('ECHO REQ\r\n')); await expect('ECHO REP');
  await sendPacket(0x01, new Uint8Array([4, 0]));
  await sendPacket(0x01, new Uint8Array([0, 0, breed ? 1 : 0, 0]));
  if (breed) { await expect('BREED 0'); await expect('SYNC 1'); }
  else assert(log.some((x) => x[0] === 'done'), 'done');
  assert(log.some((x) => x[0] === 'result'));
  // a chunk with the wrong key is refused
  v.stop();
}

(async () => {
  await run(false);
  await run(true);
  console.log('visit_test ok');
})().catch((e) => { console.error(e); process.exit(1); });
