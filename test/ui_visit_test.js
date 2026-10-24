// Device panel: "Simulate a visit" in the browser against a scripted Paradise (random data).
// node test/ui_visit_test.js <out-dir>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const V = require('../src/visit.js');
const enc = (s) => new TextEncoder().encode(s);
(async () => {
  const out = process.argv[2] || '/tmp/uivisit'; fs.mkdirSync(out, { recursive: true });
  const key = V.parseKey(require('../src/flash.js').linkKey);
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 1000 } });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  // the scripted Paradise: what the Studio writes lands here
  let rx = new Uint8Array(0), want = 0; const lines = [], bins = [];
  const drain = () => {
    for (;;) {
      if (want) { if (rx.length < want) return; bins.push(rx.slice(0, want)); rx = rx.slice(want); want = 0; continue; }
      let j = -1; for (let i = 0; i + 1 < rx.length; i++) if (rx[i] === 13 && rx[i + 1] === 10) { j = i; break; }
      if (j < 0) return; lines.push(new TextDecoder().decode(rx.subarray(0, j))); rx = rx.slice(j + 2);
    }
  };
  await page.exposeFunction('__tx', (a) => { const b = new Uint8Array(rx.length + a.length); b.set(rx); b.set(a, rx.length); rx = b; drain(); });
  await page.addInitScript(() => {
    const q = [], waiting = [];
    window.__rx = (a) => { const v = new Uint8Array(a); if (waiting.length) waiting.shift()({ value: v, done: false }); else q.push(v); };
    const port = { open: async () => {}, close: async () => {},
      writable: { getWriter: () => ({ write: (d) => window.__tx(Array.from(d)), releaseLock() {} }) },
      readable: { getReader: () => ({ read: () => q.length ? Promise.resolve({ value: q.shift(), done: false }) : new Promise((r) => waiting.push(r)), cancel: async () => { waiting.forEach((r) => r({ done: true })); }, releaseLock() {} }) } };
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port } });
  });
  const send = (b) => page.evaluate((a) => window.__rx(a), Array.from(b));
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  async function expect(s) { for (let i = 0; i < 600; i++) { const k = lines.indexOf(s); if (k >= 0) { lines.splice(k, 1); return; } await sleep(10); } throw new Error('missing ' + s + ' | ' + lines.join(',')); }
  const sid = 0x12345678;
  async function chunk(type, idx, data) {
    const p = new Uint8Array(12 + data.length);
    p[0] = 0x78; p[1] = 0x56; p[2] = 0x34; p[3] = 0x12; p[4] = 0x54; p[5] = 0x43; p[6] = 0x50; p[7] = type; p[8] = idx; p[9] = 255 - idx;
    const c = V.crc16(data); p[10] = c & 255; p[11] = c >> 8; p.set(data, 12);
    const n = new Uint8Array([1, 2, 3, idx]), e = await V.crypt(n, key, p), all = new Uint8Array(4 + e.length); all.set(n); all.set(e, 4); return all;
  }
  async function packet(type, data) {
    await send(enc('PKT ' + data.length + '\r\n')); await expect('ACK');
    for (let i = 0; i * 0x1000 < data.length; i++) { await send(await chunk(type, i, data.subarray(i * 0x1000, (i + 1) * 0x1000))); await expect('ACK'); }
  }
  let ok = true; const fail = (m) => { ok = false; console.log('FAIL', m); };
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.click('[data-lang="de"]').catch(() => {});
  await page.click('#btnDevice');
  if ((await page.$$eval('#visEd option', (o) => o.length)) !== 6) fail('six editions');
  await page.selectOption('#visEd', 'glacier');
  await page.click('#btnVis');
  await page.waitForFunction(() => /Glacier/.test(document.getElementById('visStat').textContent));
  if ((await page.textContent('#btnVis')) !== 'Beenden') fail('button Beenden');
  await page.screenshot({ path: out + '/visit_1.png' });
  await packet(0x11, new Uint8Array([9, 9]));
  const mine = new Uint8Array(0x20014); for (let i = 0; i < mine.length; i++) mine[i] = (i * 13) & 255;
  await packet(0x01, mine);
  await expect('PKT 131092');
  let off = 0; const got = new Uint8Array(0x20014);
  want = 16 + 0x1000; await send(enc('ACK\r\n'));
  for (let i = 0; i < 33; i++) {
    for (let k = 0; k < 600 && !bins.length; k++) await sleep(10);
    const c = bins.shift(); if (!c) { fail('chunk ' + i); break; }
    const p = await V.crypt(c.subarray(0, 4), key, c.subarray(4));
    if (V.crc16(p.subarray(12)) !== (p[10] | p[11] << 8)) fail('crc ' + i);
    got.set(p.subarray(12), off); off += p.length - 12;
    const left = 0x20014 - off; want = left ? 16 + Math.min(0x1000, left) : 0;
    await send(enc('ACK\r\n'));
  }
  if (got[0x20002] !== 6) fail('rom_type Glacier ' + got[0x20002]);
  await page.waitForFunction(() => /spielen/.test(document.getElementById('visStat').textContent));
  await packet(0x01, new Uint8Array([2, 0]));
  await packet(0x01, new Uint8Array([0, 0, 0, 0]));
  await page.waitForFunction(() => /fertig/.test(document.getElementById('visStat').textContent));
  await page.screenshot({ path: out + '/visit_2.png' });
  await page.click('#btnVis');
  if ((await page.textContent('#btnVis')) !== 'Starten') fail('button Starten after stop');
  if (errors.length) fail('page errors ' + errors.join(' | '));
  await browser.close();
  console.log(ok ? 'all good' : 'FAIL');
  process.exit(ok ? 0 : 1);
})().catch((e) => { console.log('FAIL', e.message); process.exit(1); });
