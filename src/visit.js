/*
 * Paramix Studio 1.6.9: simulated playdate partner for the Tamagotchi Paradise.
 *
 * The Paradise talks on its contacts (UART, 460800 8-N-1) with the "TCP" protocol described by GMMan
 * (tama-para-research, protocols/tcp.md and playdate.md). The Studio plays the other device: the player
 * starts a playdate on the Paradise and presses B there, so the Paradise is the initiator and we answer.
 *
 * Flow as recipient (measured with two emulated consoles):
 *   < PKT 2        session ID (type bit 4), 2 random bytes
 *   < PKT 131092   ghost package + planet screenshot + playdate data (0x20014 bytes)
 *   > PKT 131092   our answer: their own package with the chosen edition (rom_type) and our own IDs
 *   < PKT 2        friendship level
 *     ECHO REQ / ECHO REP while the animation runs
 *   < PKT 4        play result (u16 result, bit 0 of the next u16: breeding possible)
 *     with breeding: we answer "BREED 0" and "SYNC 1" (we never breed)
 *
 * No game data is in here: the visitor is a copy of the player's own Tama, only the edition and the IDs
 * are ours. The chunks use the same link key as the flash app loader (src/flash.js). Transport free:
 * send(bytes) out, feed(bytes) in, so the same code runs in the test.
 */
(function (root) {
  'use strict';
  const ED = { land: 1, water: 2, sky: 3, jade: 4, tropics: 5, glacier: 6 };
  const PD = 0x20000;                 // playdate data in the phase 1 packet
  const enc = (s) => new TextEncoder().encode(s);

  function crc16(d) {
    let c = 0;
    for (let i = 0; i < d.length; i++) { c ^= d[i]; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1; }
    return c;
  }
  async function sha256(u) {
    const c = (root.crypto && root.crypto.subtle) ? root.crypto : (typeof require === 'function' ? require('crypto').webcrypto : null);
    return new Uint8Array(await c.subtle.digest('SHA-256', u));
  }
  async function keystream(nonce, key) {
    const m = new Uint8Array(4 + key.length); m.set(nonce, 0); m.set(key, 4);
    return sha256(m);
  }
  /* encrypt and decrypt are the same: XOR with the keystream, each key byte becomes 2k+1 after use */
  async function crypt(nonce, key, data) {
    const k = await keystream(nonce, key), out = new Uint8Array(data.length);
    for (let i = 0; i < data.length; i++) { const j = i & 31; out[i] = data[i] ^ k[j]; k[j] = (k[j] * 2 + 1) & 255; }
    return out;
  }
  function rnd4() {
    const n = new Uint8Array(4);
    if (root.crypto && root.crypto.getRandomValues) root.crypto.getRandomValues(n);
    else for (let i = 0; i < 4; i++) n[i] = Math.random() * 256 | 0;
    return n;
  }
  const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
  const w16 = (b, o, v) => { b[o] = v & 255; b[o + 1] = v >> 8 & 255; };
  const w32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = v >> 8 & 255; b[o + 2] = v >> 16 & 255; b[o + 3] = v >>> 24; };

  /* the link key: 32 characters, as text or as the bytes of the key file */
  function parseKey(x) {
    let s = typeof x === 'string' ? x : new TextDecoder('latin1').decode(x);
    s = s.trim();
    if (!/^[\x21-\x7e]{16,64}$/.test(s)) return null;
    return enc(s);
  }

  /*
   * opts: { edition: 'water' | 1..6, key: Uint8Array, send: async (Uint8Array) => {}, log: (event, info) => {} }
   * Returns { feed(Uint8Array), stop(), state }.
   * log events: 'start', 'packet' {kind}, 'sent', 'result' {result, breed}, 'done', 'error' {text}
   */
  function createVisitor(opts) {
    const ed = typeof opts.edition === 'number' ? opts.edition : ED[String(opts.edition).toLowerCase()];
    if (!ed) throw new Error('unknown edition');
    const key = opts.key, log = opts.log || (() => {});
    const st = { phase: 'wait', sid: 0, got: 0, stopped: false, result: null };
    let buf = new Uint8Array(0);
    let need = 0;                     // > 0: a chunk of this size is expected
    let rx = null;                    // packet coming in
    let tx = null;                    // packet going out
    let echo = null, queue = Promise.resolve();
    const out = (s) => opts.send(typeof s === 'string' ? enc(s) : s);
    const line = (s) => out(s + '\r\n');

    function idle(on) {
      clearInterval(echo); echo = null;
      // the other device pings every 3 to 4 s while nothing is sent; we do the same so it sees us
      if (on && !st.stopped) echo = setInterval(() => { if (!tx && !rx && !need) line('ECHO REQ').catch(() => {}); }, 3000);
    }
    async function sendPacket(data, type) {
      tx = { data, type, idx: 0, n: Math.ceil(data.length / 0x1000), tries: 0, done: null };
      const p = new Promise((res, rej) => { tx.done = res; tx.fail = rej; });
      await line('PKT ' + data.length);
      tx.timer = setTimeout(retryPkt, 2000);
      return p;
    }
    function retryPkt() {
      if (!tx || st.stopped) return;
      if (++tx.tries >= 3) { const f = tx.fail; tx = null; line('CAN').catch(() => {}); f(new Error('no ACK')); return; }
      line('PKT ' + tx.data.length).catch(() => {}); tx.timer = setTimeout(retryPkt, 2000);
    }
    async function sendChunk() {
      const i = tx.idx, d = tx.data.subarray(i * 0x1000, Math.min(tx.data.length, (i + 1) * 0x1000));
      const p = new Uint8Array(12 + d.length);
      w32(p, 0, st.sid); p[4] = 0x54; p[5] = 0x43; p[6] = 0x50; p[7] = tx.type; p[8] = i; p[9] = 255 - i; w16(p, 10, crc16(d)); p.set(d, 12);
      const n = rnd4(), c = await crypt(n, key, p), all = new Uint8Array(4 + c.length);
      all.set(n, 0); all.set(c, 4);
      clearTimeout(tx.timer); tx.timer = setTimeout(() => { if (tx && ++tx.tries < 3) sendChunk().catch(() => {}); else if (tx) { const f = tx.fail; tx = null; f(new Error('no ACK for a chunk')); } }, 5000);
      await out(all);
    }
    function onAck() {
      if (!tx) return;
      tx.tries = 0;
      if (tx.started) tx.idx++;
      tx.started = true;
      if (tx.idx >= tx.n) { clearTimeout(tx.timer); const d = tx.done; tx = null; d(); return; }
      sendChunk().catch((e) => fail(e));
    }

    function answer(their) {
      if (their.length < PD + 0x14) return null;
      const a = their.slice();
      w16(a, PD, 0);                                  // force_send_away: never
      w16(a, PD + 2, ed);                             // rom_type: the chosen edition
      w32(a, PD + 4, 0x504D5800 | ed);                // device UID of the simulated device ("PMX" + edition)
      w32(a, PD + 8, (u32(their, PD + 8) ^ 0x5A5A0000 ^ ed) >>> 0);   // character UID: theirs, changed
      return a;
    }
    async function packet(d, type) {
      st.got++;
      if (type & 0x10) { log('packet', { kind: 'session' }); return; }
      if (d.length === 0x20014) {
        log('packet', { kind: 'data', ed: u32(d, PD) >>> 16 });
        st.phase = 'send';
        const a = answer(d);
        await new Promise((r) => setTimeout(r, 150));   // the other device also waits a moment before it answers
        await sendPacket(a, type & 15);
        st.phase = 'play'; log('sent', {}); idle(true);
        return;
      }
      if (d.length === 2) { log('packet', { kind: 'friendship', level: d[0] | d[1] << 8 }); return; }
      if (d.length === 4) {
        const result = d[0] | d[1] << 8, breed = !!(d[2] & 1);
        st.result = result; log('result', { result, breed });
        if (breed) { await line('BREED 0'); await line('SYNC 1'); return; }
        finish();
        return;
      }
      log('packet', { kind: 'other', n: d.length });
    }
    function finish() { if (st.phase === 'done') return; st.phase = 'done'; idle(false); log('done', { result: st.result }); }
    function fail(e) { if (st.stopped) return; idle(false); st.phase = 'error'; log('error', { text: e && e.message || String(e) }); }

    async function chunk(c) {
      const n = c.subarray(0, 4), p = await crypt(n, key, c.subarray(4));
      const sid = u32(p, 0), type = p[7], idx = p[8], crc = p[10] | p[11] << 8, d = p.subarray(12);
      const ok = p[4] === 0x54 && p[5] === 0x43 && p[6] === 0x50 && p[9] === 255 - idx && crc16(d) === crc && (!st.sid || (type & 0x10) || sid === st.sid);
      if (!ok) { need = c.length; await line('NAK'); if (!st.badKey && p[4] !== 0x54) { st.badKey = true; log('error', { text: 'key' }); } return; }
      if (idx !== rx.idx) { need = c.length; await line('ENQ ' + rx.idx); return; }
      if (type & 0x10) st.sid = sid;
      rx.data.set(d, rx.off); rx.off += d.length; rx.idx++;
      const left = rx.n - rx.off;
      need = left > 0 ? 16 + Math.min(0x1000, left) : 0;
      await line('ACK');
      if (!left) { const r = rx; rx = null; packet(r.data, type).catch(fail); }
    }
    async function command(s) {
      const w = s.trim().split(/\s+/);
      if (!w[0]) return;
      if (w[0] === 'PKT') {
        const n = +w[1];
        if (!(n > 0 && n <= 0x100000)) return;
        if (st.phase === 'wait' && !st.started) { st.started = true; log('start', {}); }
        rx = { n, off: 0, idx: 0, data: new Uint8Array(n) };
        need = 16 + Math.min(0x1000, n);
        await line('ACK');
        return;
      }
      if (w[0] === 'ACK') { onAck(); return; }
      if (w[0] === 'NAK') { if (tx && tx.started) sendChunk().catch(fail); return; }
      if (w[0] === 'ENQ') { if (tx) { tx.idx = +w[1] || 0; tx.started = true; sendChunk().catch(fail); } return; }
      if (w[0] === 'CAN') { if (tx) { const f = tx.fail; tx = null; f(new Error('cancelled by the Paradise')); } else if (st.phase !== 'done') fail(new Error('cancelled by the Paradise')); return; }
      if (w[0] === 'ECHO') { if (w[1] === 'REQ') await line('ECHO REP'); return; }
      if (w[0] === 'SYNC') return;     // we sent ours with BREED 0
    }
    async function pump() {
      for (;;) {
        if (need) {
          if (buf.length < need) return;
          const c = buf.slice(0, need); buf = buf.slice(need); need = 0;
          await chunk(c);
          continue;
        }
        let j = -1;
        for (let i = 0; i + 1 < buf.length; i++) if (buf[i] === 13 && buf[i + 1] === 10) { j = i; break; }
        if (j < 0) { if (buf.length > 256) buf = buf.slice(buf.length - 64); return; }
        const s = new TextDecoder('latin1').decode(buf.subarray(0, j)); buf = buf.slice(j + 2);
        await command(s);
      }
    }
    return {
      state: st,
      feed(bytes) {
        if (st.stopped) return queue;
        const b = new Uint8Array(buf.length + bytes.length); b.set(buf, 0); b.set(bytes, buf.length); buf = b;
        queue = queue.then(pump).catch(fail);
        return queue;
      },
      stop() { st.stopped = true; idle(false); if (tx) clearTimeout(tx.timer); tx = null; rx = null; },
    };
  }

  const api = { ED, createVisitor, parseKey, crypt, crc16 };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.ParamixVisit = api;
})(typeof window !== 'undefined' ? window : globalThis);
