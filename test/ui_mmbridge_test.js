// 1.6.8 (test6): the MixingMeets bridge (bookmarklet) with a pretend dongle: the page's Web Bluetooth calls (as in
// mixing-meets.com relay.js) go out as frames, hello / acks / notifications come back.
// node test/ui_mmbridge_test.js
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
  const b = await chromium.launch(); const p = await b.newPage();
  const errors = []; p.on('pageerror', (e) => errors.push(e.message));
  await p.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  const href = await p.evaluate(() => pmxBridgeHref());
  ok(href.startsWith('javascript:'), 'bookmarklet built (' + href.length + ' chars)');
  const r = await p.evaluate(async (href) => {
    // pretend dongle: answers like T18Emu (main.c T18_LINK)
    const sent = []; let push = null;
    const fr = (ty, d) => { const a = [0xA5, ty, d.length, ...d]; a.push(a.slice(1).reduce((x, y) => x + y, 0) & 255); return new Uint8Array(a); };
    const port = { open: async () => {}, readable: { getReader: () => ({ read: () => new Promise((res) => { push = res; }) }) },
      writable: { getWriter: () => ({ write: async (u) => { sent.push(Array.from(u)); const ty = u[1];
        setTimeout(() => { if (ty === 0x57) push({ value: fr(0x4B, []), done: false }); }, 5); } }) } };
    Object.defineProperty(navigator, 'serial', { configurable: true, value: { requestPort: async () => port } });
    eval(decodeURIComponent(href.slice('javascript:'.length)));
    setTimeout(() => push({ value: fr(0x48, [1]), done: false }), 300);
    // as relay.js
    const D = await navigator.bluetooth.requestDevice({ acceptAllDevices: true, optionalServices: ['0000fff0-0000-1000-8000-00805f9b34fb'] });
    let gone = 0; D.addEventListener('gattserverdisconnected', () => gone++);
    const g = await D.gatt.connect(); const v = await g.getPrimaryService('0000fff0-0000-1000-8000-00805f9b34fb');
    const R = await v.getCharacteristic('0000fff1-0000-1000-8000-00805f9b34fb'), T = await v.getCharacteristic('0000fff2-0000-1000-8000-00805f9b34fb');
    const notes = []; R.addEventListener('characteristicvaluechanged', (e) => { const b = e.target.value; notes.push(Array.from(new Uint8Array(b.buffer, b.byteOffset, b.byteLength))); });
    await R.startNotifications();
    const q = T.properties, m = q.writeWithoutResponse !== false && T.writeValueWithoutResponse ? 'without' : q.write !== false && T.writeValueWithResponse ? 'with' : 'legacy';
    await T.writeValueWithResponse(Uint8Array.from([240, 141]));
    const big = new Uint8Array(100); big[0] = 0x54; await T.writeValueWithResponse(big.subarray(0, 20));
    push({ value: fr(0x4E, [0xF0, 0x0C, 0, 0, 0x13, 0x13, 0x13, 0]), done: false });
    await new Promise((r) => setTimeout(r, 50));
    D.gatt.disconnect();
    return { name: D.name, m, sent, notes, gone, connected: D.gatt.connected };
  }, href);
  ok(r.name === 'TMGC_meets', 'device name ' + r.name);
  ok(r.m === 'with', 'writes go as writes with response (' + r.m + ')');
  ok(JSON.stringify(r.sent[0]) === JSON.stringify([0x5A, 0x43, 1, 1, 0x45]), 'connect frame ' + JSON.stringify(r.sent[0]));
  const w = r.sent.filter((f) => f[1] === 0x57);
  ok(w.length === 2 && w[0][2] === 2 && w[0][3] === 0xF0 && w[1][2] === 20, 'write frames ' + w.map((f) => f[2]).join(','));
  ok(r.notes.length === 1 && r.notes[0][1] === 0x0C, 'notification ' + JSON.stringify(r.notes));
  ok(r.sent.some((f) => f[1] === 0x43 && f[3] === 0) && r.gone === 1 && !r.connected, 'disconnect');
  // test7: the dongle still held by the Studio
  const busy = await p.evaluate(async (href) => {
    delete window.__pmxBridge; const al = []; window.alert = (m) => al.push(m);
    const port = { open: async () => { throw new DOMException('Failed to open serial port.', 'NetworkError'); } };
    Object.defineProperty(navigator, 'serial', { configurable: true, value: { requestPort: async () => port } });
    eval(decodeURIComponent(href.slice('javascript:'.length)));
    let err = ''; try { await navigator.bluetooth.requestDevice({ acceptAllDevices: true }); } catch (e) { err = e.name; }
    return { err, al };
  }, href);
  ok(busy.err === 'NetworkError' && /UART freigeben/.test(busy.al.join()), 'busy dongle: says to release it (' + busy.al.length + ' alert)');
  // test7: the warning in the Studio and "UART freigeben"
  const fr2 = await p.evaluate(async () => {
    const box = document.createElement('div'); document.body.appendChild(box); const X = window.pmxTest, dv = X.dv(); X.uartWarnInto(box);
    let closed = 0; dv.io = { close: async () => { closed++; } }; dv.d = null; X.devRender();
    const shown = !box.querySelector('.uartwarn').hidden;
    await X.devFree();
    return { shown, closed, after: !box.querySelector('.uartwarn').hidden, io: dv.io };
  });
  ok(fr2.shown && fr2.closed === 1 && !fr2.after && fr2.io === null, 'warning shown while held, gone after releasing');
  ok(!errors.length, 'no page errors ' + errors.join('; '));
  await b.close(); console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
