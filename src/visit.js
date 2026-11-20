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
 * The visitor uses the player's Tama by default. The caller can provide a full ghost and screenshot.
 * The chunks use the same link key as the flash app loader (src/flash.js). Transport free:
 * send(bytes) out, feed(bytes) in, so the same code runs in the test.
 */
(function (root) {
  'use strict';
  const ED = { land: 1, water: 2, sky: 3, jade: 4, tropics: 5, glacier: 6, neon: 7, 'my-lab': 254 };
  const GHOST_SIZE = 0x20000;
  const PACKET_SIZE = GHOST_SIZE + 0x14;
  const SPECIAL_FOOD_AREA = 0x4000;
  const SPECIAL_FOOD_REQUEST = 0x1b008;
  const SPECIAL_FOOD_PAYLOAD = 0x1f002;
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
  const u16 = (b, o) => b[o] | b[o + 1] << 8;
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
   * opts: { edition: string | number, ghost?: Uint8Array, key: Uint8Array,
   *         send: async (Uint8Array) => {}, log: (event, info) => {} }
   * Returns { feed(Uint8Array), stop(), state }.
   * log events: 'start', 'packet' {kind}, 'sent', 'result' {result, breed}, 'done', 'error' {text}
   */
  function createVisitor(opts) { return createSession(opts); }

  function createLabRelease(opts) {
    return createSession(opts, buildLabRelease(opts.ghost, opts.core));
  }

  function checkSpecialFood(bytes, core) {
    if (bytes.length !== SPECIAL_FOOD_PAYLOAD || u32(bytes, 0) !== 0x32435241) {
      throw new Error('Expected a decoded My Lab Special Food payload with food data');
    }
    const length = u32(bytes, 8), end = 16 + length;
    if (u32(bytes, 12) !== 3 || end > SPECIAL_FOOD_AREA || end < 64 ||
        core.arc2Checksum(bytes, 0, length) !== u32(bytes, 4) ||
        core.arc2Parse(bytes, 0).ents.some(e => e.offset < 64 || e.offset + e.clen > end)) {
      throw new Error('Invalid Special Food archive');
    }
  }

  function createSpecialFood(opts) {
    checkSpecialFood(opts.payload, opts.core);
    return createSession(opts, null, opts.payload.slice());
  }

  function createSession(opts, release, specialFood) {
    const ed = typeof opts.edition === 'number' ? opts.edition : ED[String(opts.edition).toLowerCase()];
    if (!release && !specialFood && !ed) throw new Error('unknown edition');
    if (!release && opts.ghost && opts.ghost.length !== GHOST_SIZE) throw new Error('invalid ghost size');
    const key = opts.key, log = opts.log || (() => {});
    const st = { phase: 'wait', sid: 0, got: 0, stopped: false, result: null };
    let buf = new Uint8Array(0);
    let need = 0;                     // > 0: a chunk of this size is expected
    let rx = null;                    // packet coming in
    let tx = null;                    // packet going out
    let echo = null, heartbeat = null, cooldown = null, foodTimeout = null, queue = Promise.resolve();
    const out = (s) => opts.send(typeof s === 'string' ? enc(s) : s);
    const line = (s) => out(s + '\r\n');

    function idle(on) {
      clearInterval(echo); echo = null;
      // the other device pings every 3 to 4 s while nothing is sent; we do the same so it sees us
      if (on && !st.stopped) echo = setInterval(() => { if (!tx && !rx && !need) line('ECHO REQ').catch(() => {}); }, 3000);
    }
    async function sendPacket(data, type) {
      if (st.stopped || tx) throw new Error('session is unavailable');
      const current = tx = { data, type, idx: 0, n: Math.ceil(data.length / 0x1000), tries: 0 };
      return new Promise((resolve, reject) => {
        current.done = resolve;
        current.fail = reject;
        current.timer = setTimeout(retryPkt, 2000);
        Promise.resolve().then(() => {
          if (tx === current && !st.stopped) return line('PKT ' + data.length);
        }).catch(error => { if (tx === current) cancelPacket(error); });
      });
    }
    function cancelPacket(error) {
      if (!tx) return;
      const current = tx;
      tx = null;
      clearTimeout(current.timer);
      current.fail(error);
    }
    function retryPkt() {
      if (!tx || st.stopped) return;
      if (++tx.tries >= 3) { const f = tx.fail; tx = null; line('CAN').catch(() => {}); f(new Error('no ACK')); return; }
      line('PKT ' + tx.data.length).catch(() => {}); tx.timer = setTimeout(retryPkt, 2000);
    }
    async function sendChunk() {
      const current = tx;
      const i = current.idx, d = current.data.subarray(i * 0x1000, Math.min(current.data.length, (i + 1) * 0x1000));
      const p = new Uint8Array(12 + d.length);
      w32(p, 0, st.sid); p[4] = 0x54; p[5] = 0x43; p[6] = 0x50; p[7] = tx.type; p[8] = i; p[9] = 255 - i; w16(p, 10, crc16(d)); p.set(d, 12);
      const n = rnd4(), c = await crypt(n, key, p), all = new Uint8Array(4 + c.length);
      all.set(n, 0); all.set(c, 4);
      if (tx !== current || current.idx !== i || st.stopped) return;
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
      const a = their.slice();
      if (opts.ghost) a.set(opts.ghost);
      w16(a, GHOST_SIZE, 0);                          // force_send_away
      w16(a, GHOST_SIZE + 2, ed);
      w32(a, GHOST_SIZE + 4, 0x504D5800 | ed);
      const characterId = opts.ghost ? u32(opts.ghost, 0) : u32(their, GHOST_SIZE + 8);
      w32(a, GHOST_SIZE + 8, (characterId ^ 0x5A5A0000 ^ ed) >>> 0);
      return writePlaydateFields(a, opts.playdateFields);
    }
    async function packet(d, type) {
      st.got++;
      if (release) return;
      if (specialFood) {
        if (st.phase === 'wait' && type === 0x35 && d.length === 2) {
          st.phase = 'food-request';
          foodTimeout = setTimeout(() => fail(new Error('Paradise did not send a Special Food request')), 20000);
          return;
        }
        if (st.phase === 'food-request' && type === 0x25 && d.length === SPECIAL_FOOD_REQUEST) {
          clearTimeout(foodTimeout);
          st.phase = 'send';
          await releaseDelay(150);
          await sendPacket(specialFood, 0x25);
          st.phase = 'food-confirm';
          log('sent', {});
          idle(true);
          foodTimeout = setTimeout(() => fail(new Error('Paradise did not confirm the Special Food transfer')), 45000);
          return;
        }
        if (st.phase === 'food-confirm' && type === 0x25 && d.length === 2) {
          st.result = u16(d, 0);
          finish();
          return;
        }
        if (st.phase !== 'done' && st.phase !== 'error') throw new Error('Unexpected packet. Open Special Food → Get Food on Paradise.');
        return;
      }
      if (type & 0x10) { log('packet', { kind: 'session' }); return; }
      if (d.length === PACKET_SIZE) {
        log('packet', { kind: 'data', ed: u32(d, GHOST_SIZE) >>> 16, playdate: d });
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
    function finish() { if (st.phase === 'done') return; st.phase = 'done'; clearTimeout(foodTimeout); idle(false); log('done', { result: st.result }); }
    function fail(e) {
      if (st.stopped || st.phase === 'error' || st.phase === 'done') return;
      clearTimeout(foodTimeout);
      idle(false);
      st.phase = 'error';
      cancelPacket(e);
      if (heartbeat) heartbeat(e);
      if (cooldown) cooldown(e);
      log('error', { text: e && e.message || String(e) });
    }

    function echoRound() {
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => heartbeat(new Error('My Lab stopped responding')), 5000);
        heartbeat = error => {
          clearTimeout(timer);
          heartbeat = null;
          if (error) reject(error);
          else resolve();
        };
        Promise.resolve().then(() => {
          if (!st.stopped && heartbeat) return line('ECHO REQ');
        }).catch(error => heartbeat?.(error));
      });
    }

    function releaseDelay(ms) {
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { cooldown = null; resolve(); }, ms);
        cooldown = error => { clearTimeout(timer); cooldown = null; reject(error); };
      });
    }

    async function startRelease() {
      if (!release || st.phase !== 'wait' || st.stopped) return;
      st.phase = 'send';
      st.sid = u32(rnd4(), 0) || 1;
      const challenge = rnd4()[0];
      log('start', {});
      try {
        // Timing follows the native Neon trace at a 96 MHz emulated clock.
        await sendPacket(Uint8Array.of(challenge, challenge), 0x36);
        await releaseDelay(850);
        await sendPacket(release, 0x26);
        log('sent', {});
        await releaseDelay(2100);
        for (let i = 0; i < 6; i++) {
          await echoRound();
          await releaseDelay(i === 5 ? 1500 : 2100);
        }
        await sendPacket(new Uint8Array(2), 0x26);
        finish();
      } catch (e) { fail(e); }
    }

    async function chunk(c) {
      const n = c.subarray(0, 4), p = await crypt(n, key, c.subarray(4));
      const sid = u32(p, 0), type = p[7], idx = p[8], crc = p[10] | p[11] << 8, d = p.subarray(12);
      const ok = p[4] === 0x54 && p[5] === 0x43 && p[6] === 0x50 && p[9] === 255 - idx && crc16(d) === crc && (!st.sid || (type & 0x10) || sid === st.sid);
      if (!ok) { need = c.length; await line('NAK'); if (!st.badKey && p[4] !== 0x54) { st.badKey = true; log('error', { text: 'key' }); } return; }
      if (idx !== rx.idx) { need = c.length; await line('ENQ ' + rx.idx); return; }
      if (!release && (type & 0x10)) st.sid = sid;
      rx.data.set(d, rx.off); rx.off += d.length; rx.idx++;
      const left = rx.n - rx.off;
      need = left > 0 ? 16 + Math.min(0x1000, left) : 0;
      await line('ACK');
      if (!left) { const r = rx; rx = null; packet(r.data, type).catch(fail); }
    }
    async function command(s) {
      // A physical My Lab can prefix its first ACK with a NUL byte.
      const w = s.replace(/^[\x00\s]+|[\x00\s]+$/g, '').split(/\s+/);
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
      if (w[0] === 'ENQ') {
        const index = Number(w[1]);
        if (tx && Number.isInteger(index) && index >= 0 && index < tx.n) {
          tx.idx = index; tx.started = true; sendChunk().catch(fail);
        }
        return;
      }
      if (w[0] === 'CAN') { fail(new Error('cancelled by the connected device')); return; }
      if (w[0] === 'ECHO') {
        if (w[1] === 'REQ') await line('ECHO REP');
        if (w[1] === 'REP') heartbeat?.();
        return;
      }
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
      ...(release ? { start: startRelease } : {}),
      feed(bytes) {
        if (st.stopped) return queue;
        const b = new Uint8Array(buf.length + bytes.length); b.set(buf, 0); b.set(bytes, buf.length); buf = b;
        queue = queue.then(pump).catch(fail);
        return queue;
      },
      async stop() {
        const notify = (release || specialFood) && st.phase !== 'wait' && st.phase !== 'done' && st.phase !== 'error';
        st.stopped = true;
        clearTimeout(foodTimeout);
        idle(false);
        const error = new Error('session stopped');
        cancelPacket(error);
        if (heartbeat) heartbeat(error);
        if (cooldown) cooldown(error);
        rx = null;
        if (notify) await line('CAN');
      },
    };
  }

  const GHOST_COMPOSITES = 0x600;
  const GHOST_SPRITES = 0x1d34;
  const GHOST_PLANET = 0x1b000;
  const PLANET_SPRITE = GHOST_PLANET + 0x200;
  const PLAYDATE_FIELDS = [
    ['visDeviceUid', 4, 4], ['visCharacterUid', 8, 4],
    ['visFriendship', 12, 2], ['visPreviousResult', 14, 2],
    ['visHunger', 16, 1], ['visHappiness', 17, 1], ['visLove', 18, 0],
  ];
  function playdateFields(packet, ghost, edition) {
    if (packet?.length !== PACKET_SIZE) return {
      visDeviceUid: (0x504D5800 | edition) >>> 0,
      visCharacterUid: (u32(ghost, 0) ^ 0x5A5A0000 ^ edition) >>> 0,
      visFriendship: 0, visPreviousResult: 0, visHunger: 3, visHappiness: 3, visLove: 0,
    };
    return Object.fromEntries(PLAYDATE_FIELDS.map(([key, offset, width]) => {
      const at = GHOST_SIZE + offset;
      return [key, width === 4 ? u32(packet, at) : width === 2 ? u16(packet, at) :
        width === 1 ? packet[at] : packet[at] & 1];
    }));
  }

  function writePlaydateFields(packet, fields) {
    if (!fields) return packet;
    for (const [key, offset, width] of PLAYDATE_FIELDS) {
      const value = fields[key];
      const max = width ? 2 ** (8 * width) - 1 : 1;
      if (!Number.isInteger(value) || value < 0 || value > max) throw new Error('invalid Playdate field: ' + key);
      const at = GHOST_SIZE + offset;
      if (width === 4) w32(packet, at, value);
      else if (width === 2) w16(packet, at, value);
      else packet[at] = width === 1 ? value : (packet[at] & ~1) | value;
    }
    return packet;
  }
  const GHOST_POSES = [20, 24];
  const alignWord = (n) => (n + 3) & ~3;
  function collectRomVisitors(ctx, core) {
    if (!ctx?.files?.[0]) return [];
    const pack = ctx.files[0], poses = core.paraLayers(pack), text = core.textTablesOf(ctx.rom);
    if (!poses || !text) return [];
    const groups = new Map();
    const namesFor = character => text.tabs.slice(0, 9).map(table =>
      core.txtWords(table, character.name).slice(0, 12));
    const visitors = core.characterSprites(ctx).map((character) => {
      const { group } = character;
      if (!groups.has(group)) {
        const frames = GHOST_POSES.map(count => Array(count).fill(null));
        groups.set(group, frames[0]); groups.set(group + 3, frames[1]);
      }
      const frames = [groups.get(group), groups.get(group + 3)];
      const sprites = Array.from({ length: 6 }, (_, i) => group + i);
      return { character, sprites, frames, names: namesFor(character), pack, entries: ctx.entries };
    });
    for (const frame of poses.frames) {
      const body = frame.find(layer => layer.type === 0 && groups.has(layer.img));
      const frames = groups.get(body?.img);
      if (frames && body.sub < frames.length && !frames[body.sub]) frames[body.sub] = frame;
    }
    if (ctx.editionId !== 8) {
      // Transplanted figures can share individual sprites with other species.
      // Read their own animation rather than assuming six consecutive sprites.
      for (const character of core.charTable(pack)) {
        if (character.id >= 9000 || character.field < 7 || character.field > 9 ||
            character.stage < 3 || character.stage > 5) continue;
        const frames = GHOST_POSES.map((count, zoom) =>
          Array.from({ length: count }, (_, i) => poses.frameAt(54 * character.animation + (zoom ? 30 : 1) + i)));
        if (!frames.every(group => group.every(Boolean))) continue;
        const sprites = frames.flatMap(group => [0, 1, 15].map(type =>
          group[0].find(layer => layer.type === type)?.img));
        if (!sprites.every(sprite => ctx.entries[sprite])) continue;
        visitors.push({ character, sprites, frames, names: namesFor(character), pack, entries: ctx.entries });
      }
    }
    return visitors.filter((visitor) => visitor.frames.every((frames) => frames.every(Boolean)));
  }

  function ghostName(out, offset, text) {
    text.slice(0, 12).forEach((code, i) => w16(out, offset + 2 * i, code));
  }

  function packedLayerWord(pack, layer, bit, fallback) {
    if (!(layer.m & (1 << bit))) return fallback;
    let at = layer.offset + 2;
    for (let k = 0; k < bit; k++) if (layer.m & (1 << k)) at += 2;
    return pack[at] | pack[at + 1] << 8;
  }
  function writeRomPose(out, frame, pose, visitor, firstPart) {
    const start = GHOST_COMPOSITES + 110 * pose;
    for (const layer of frame) {
      const part = visitor.sprites.indexOf(layer.img, firstPart);
      const own = part >= firstPart && part < firstPart + 3;
      const slot = own ? part - firstPart : layer.type === 17 ? 4 : 3;
      const at = start + 22 * slot;
      if (out[at + 7]) continue;
      w16(out, at, own ? part : layer.img);
      w16(out, at + 2, layer.x);
      w16(out, at + 4, layer.y);
      w16(out, at + 6, 0x1000);
      w16(out, at + 10, packedLayerWord(visitor.pack, layer, 6, 64));
      w16(out, at + 12, layer.sub);
      w16(out, at + 20, layer.type);
    }
  }

  function wordSum(bytes, start, end) {
    let sum = 0;
    for (let at = start; at + 4 <= end; at += 4) sum = (sum + u32(bytes, at)) >>> 0;
    return sum;
  }

  function buildRomGhost(visitor, core) {
    const { character, sprites, frames, entries, names } = visitor;
    const out = new Uint8Array(GHOST_SIZE);
    const { id, stage, field, rank, flags } = character;
    // Live Paradise packets use the generic adult definition while retaining the actual species ID.
    w16(out, 0x0c, stage === 5 ? 4017 : id);
    w16(out, 0x0e, id);
    out[0x10] = 0xff;
    names.forEach((codes, slot) => ghostName(out, 0x12 + 26 * slot, codes));
    w16(out, 0xfc, stage); w16(out, 0xfe, rank);
    w16(out, 0x100, field); w16(out, 0x102, flags);
    w16(out, 0x10a, id);
    frames[0].forEach((frame, i) => writeRomPose(out, frame, i + 1, visitor, 0));
    frames[1].forEach((frame, i) => writeRomPose(out, frame, i + 30, visitor, 3));

    let end = GHOST_SPRITES;
    for (let part = 0; part < 6; part++) {
      const sprite = entries[sprites[part]];
      const length = alignWord(sprite.length);
      if (sprite.length < 24 || end + length > GHOST_PLANET) throw new Error('character sprites do not fit the Playdate packet');
      const at = 0x110 + 8 * part;
      w32(out, at, end); w32(out, at + 4, length);
      out.set(sprite, end);
      end += length;
      if (part === 0 || part === 2) {
        const h = core.spriteHeader(sprite);
        if (h.bpp !== 2 || h.palOffset + 32 > sprite.length) throw new Error('unsupported character palette');
        out.set(sprite.subarray(h.palOffset, h.palOffset + 32), part === 0 ? 0x140 : 0x160);
      }
    }
    w32(out, 0x10c, end);
    writeGhostChecksum(out, ghostParts(out));
    return out;
  }

  const EYE_PARTS = [1, 4];
  function ghostParts(ghost) {
    if (ghost.length !== GHOST_SIZE || (u32(ghost, 8) & 3)) return null;
    const parts = [];
    for (let part = 0; part < 6; part++) {
      const entry = 0x110 + 8 * part;
      const start = u32(ghost, entry), length = u32(ghost, entry + 4);
      if (!start && !length) { parts.push(null); continue; }
      if (start < GHOST_SPRITES || length < 24 || start + length > GHOST_PLANET) return null;
      parts.push({ start, length, bytes: ghost.subarray(start, start + length) });
    }
    return parts;
  }

  function ghostSpriteGeometry(bytes, core) {
    if (!bytes || bytes.length < 24) return null;
    const h = core.spriteHeader(bytes);
    if (h.bpp !== 2 || !h.iw || !h.ih || h.palOffset + 32 > bytes.length) return null;
    return { width: h.sw * h.iw, height: h.sh * h.ih, frames: Math.floor(h.numSprites / (h.iw * h.ih)) };
  }

  function ghostAppearanceOptions(ghost, core) {
    const parts = ghostParts(ghost);
    if (!parts) return { tint: false, eyes: null };
    const body = parts[0] || parts[3];
    const tint = !!(body && ghostSpriteGeometry(body.bytes, core));
    const eyes = EYE_PARTS.map(part => ghostSpriteGeometry(parts[part]?.bytes, core));
    if (!eyes.every(Boolean)) return { tint, eyes: null };
    const required = [0, 0];
    for (let pose = 0; pose < 54; pose++) {
      const zoom = pose >= 30 ? 1 : 0, at = GHOST_COMPOSITES + 110 * pose + 22;
      if (u16(ghost, at) === EYE_PARTS[zoom]) required[zoom] = Math.max(required[zoom], u16(ghost, at + 12) + 1);
    }
    if (!required.every((frames, zoom) => frames > 0 && frames <= eyes[zoom].frames)) return { tint, eyes: null };
    return {
      tint, eyes: { stage: u16(ghost, 0xfc), sheets: eyes.map((eye, zoom) => ({ ...eye, frames: required[zoom] })) },
    };
  }

  function romEyes(visitor, target, core) {
    if (!target || visitor.character.stage !== target.stage) return null;
    const sprites = EYE_PARTS.map(part => visitor.entries[visitor.sprites[part]]);
    const compatible = sprites.every((sprite, zoom) => {
      const h = ghostSpriteGeometry(sprite, core), want = target.sheets[zoom];
      return h && h.width === want.width && h.height === want.height && h.frames >= want.frames;
    });
    return compatible ? { id: visitor.character.id, sprites } : null;
  }

  function writeGhostChecksum(ghost, parts) {
    let sum = (wordSum(ghost, 8, 0x180) + wordSum(ghost, GHOST_COMPOSITES, GHOST_SPRITES)) >>> 0;
    for (const part of parts) if (part) sum = (sum + wordSum(ghost, part.start, part.start + part.length)) >>> 0;
    w32(ghost, 0, sum); w32(ghost, 4, -sum);
  }

  function releaseSprite(sprite, core) {
    const h = core.spriteHeader(sprite);
    const count = h.numSprites;
    const pixels = h.sw * h.sh;
    const compression = h.flags & 0x68;
    const xor = h.flags & 0x80 ? 0x53 : 0;
    const cellBytes = Math.ceil(pixels / 8) * 4;
    const limit = Math.min(h.dataLength, sprite.length);
    const tableEnd = h.pixOffset + (compression ? 8 * count : Math.ceil(pixels / 2) * count);
    if (h.bpp !== 2 || !count || !pixels || pixels * count > 0x1000000 ||
        h.palOffset < 24 || h.palOffset + 32 * Math.max(1, h.numPal) > h.pixOffset || tableEnd > limit ||
        ![0, 0x08, 0x20, 0x40].includes(compression)) throw new Error('invalid Lab Invite sprite');
    let transcode = compression === 8;
    if (compression) {
      for (let i = 0; i < count; i++) {
        const at = h.pixOffset + 8 * i, address = u32(sprite, at);
        const start = h.pixOffset + (address & 0x7fffffff), length = u32(sprite, at + 4);
        if (start < tableEnd || start + (address >>> 31 ? cellBytes : length) > limit)
          throw new Error('invalid Lab Invite sprite cell');
        if (compression === 0x20 && xor && !(address >>> 31) && length >= core.LEGACY_LZ4_TAG.length &&
            core.LEGACY_LZ4_TAG.every((value, index) => (sprite[start + index] ^ xor) === value)) transcode = true;
      }
    }
    if (!transcode) {
      if (h.dataLength === limit) return sprite;
      const out = Uint8Array.from(sprite);
      w32(out, 0, limit);
      return out;
    }
    const cells = core.spriteDecode(sprite, 0, true).cells.map(indices => {
      const raw = new Uint8Array(cellBytes);
      for (let i = 0; i < indices.length; i++) raw[i >> 1] |= indices[i] << ((i & 1) * 4);
      const encoded = core.rleEncode(raw);
      return (encoded.length < raw.length ? encoded : raw).map(byte => byte ^ xor);
    });
    const out = new Uint8Array(cells.reduce((size, cell) => size + cell.length, tableEnd));
    out.set(sprite.subarray(0, h.pixOffset));
    out[4] = (h.flags & ~0x68) | 0x20;
    w32(out, 0, out.length);
    let offset = tableEnd;
    for (const [i, cell] of cells.entries()) {
      w32(out, h.pixOffset + 8 * i, (offset - h.pixOffset) | (cell.length === cellBytes ? 0x80000000 : 0));
      w32(out, h.pixOffset + 8 * i + 4, cell.length);
      out.set(cell, offset);
      offset += cell.length;
    }
    return out;
  }

  function buildLabRelease(input, core) {
    if (![GHOST_SIZE, PACKET_SIZE].includes(input?.length)) throw new Error('choose a ghost or Playdate character');
    const ghost = input.subarray(0, GHOST_SIZE);
    if (u16(ghost, 0xfc) !== 5 || !u16(ghost, 0x10a)) throw new Error('Lab Invite needs an adult Tama');
    const parts = ghostParts(ghost);
    const end = u32(ghost, 0x10c);
    if (!parts || !parts[0] || !parts[3] || end > GHOST_PLANET ||
        parts.some(part => part && ((part.start | part.length) & 3 || part.start + part.length > end)) ||
        ((u32(ghost, 0) + u32(ghost, 4)) >>> 0) !== 0) throw new Error('invalid Lab Invite ghost');
    const sorted = parts.filter(Boolean).sort((a, b) => a.start - b.start);
    if (sorted.some((part, i) => i && part.start < sorted[i - 1].start + sorted[i - 1].length))
      throw new Error('overlapping Lab Invite sprite ranges');
    const out = new Uint8Array(GHOST_SIZE);
    out.set(ghost.subarray(0, GHOST_SPRITES));
    // Shared effects live in the receiver's archive. Translate the sender's
    // indices to the same effects in Neon, leaving the six bundled layers alone.
    for (let at = GHOST_COMPOSITES; at < GHOST_SPRITES; at += 22) {
      if (u16(out, at) < 6 || !u16(out, at + 6)) continue;
      const type = u16(out, at + 20);
      if (type === 16) w16(out, at, 222);
      if (type === 17) w16(out, at, at < GHOST_COMPOSITES + 30 * 110 ? 224 : 223);
    }
    let offset = GHOST_SPRITES;
    for (const [index, part] of parts.entries()) {
      if (!part) continue;
      const sprite = releaseSprite(part.bytes, core);
      const length = alignWord(sprite.length);
      if (offset + length > GHOST_PLANET) throw new Error('character sprites do not fit the Lab Invite packet');
      out.set(sprite, offset);
      w32(out, 0x110 + index * 8, offset);
      w32(out, 0x114 + index * 8, length);
      offset += length;
    }
    w32(out, 0x10c, offset);
    writeGhostChecksum(out, ghostParts(out));
    return out.subarray(0, GHOST_PLANET);
  }

  function editGhostAppearance(original, { color, palette, eyes }) {
    if (color === undefined && !eyes) return original;
    const parts = ghostParts(original);
    if (!parts) throw new Error('invalid Playdate sprite table');
    const out = Uint8Array.from(original);

    if (eyes) {
      const sprites = parts.map((part, index) => index === 1 ? eyes.sprites[0] : index === 4 ? eyes.sprites[1] : part?.bytes);
      const end = sprites.reduce((at, bytes) => at + (bytes ? alignWord(bytes.length) : 0), GHOST_SPRITES);
      if (end > GHOST_PLANET) throw new Error('eye sprites do not fit the Playdate packet');
      const oldEnd = Math.max(GHOST_SPRITES, ...parts.filter(Boolean).map(part => part.start + part.length));
      out.fill(0, GHOST_SPRITES, Math.max(end, oldEnd));
      let offset = GHOST_SPRITES;
      sprites.forEach((sprite, part) => {
        const at = 0x110 + part * 8, length = sprite ? alignWord(sprite.length) : 0;
        w32(out, at, sprite ? offset : 0); w32(out, at + 4, length);
        if (sprite) out.set(sprite, offset);
        offset += length;
      });
      w16(out, 0x0e, eyes.id);
      w32(out, 0x10c, end);
    }

    if (color !== undefined) {
      if (!Number.isInteger(color) || color < 0 || color > 15 || palette?.length !== 512) throw new Error('invalid ROM palette');
      out[0x10] = color;
      // Eyes carry their own palette. Body and mouth recolor the six designated entries only.
      for (const base of [0x140, 0x160]) for (let entry = 2; entry <= 7; entry++) {
        const at = base + entry * 2;
        if (u16(out, at)) w16(out, at, u16(palette, color * 32 + entry * 2));
      }
    }
    writeGhostChecksum(out, ghostParts(out));
    return out;
  }

  function setPlanetNameAndChecksum(out, core) {
    const size = u32(out, GHOST_PLANET + 12);
    if (u32(out, GHOST_PLANET + 8) !== 0x54485353 || size < 0x216 || size > 0x5000 ||
        size % 4 || GHOST_PLANET + size > out.length) return false;
    const checksum = wordSum(out, PLANET_SPRITE, GHOST_PLANET + size);
    w32(out, GHOST_PLANET, checksum); w32(out, GHOST_PLANET + 4, ~checksum);
    const label = Array.from('PARAMIX', c => core.TXT_ENC[c]);
    for (let slot = 0; slot < 9; slot++) {
      const at = GHOST_PLANET + 16 + 26 * slot;
      out.fill(0, at, at + 26);
      ghostName(out, at, label);
    }
    return true;
  }

  function writeRomGhostPlanet(out, portrait, core) {
    const palette = new Map(), indices = new Uint8Array(128 * 128);
    const scale = Math.max(1, Math.min(2, Math.floor(128 / Math.max(portrait.w, portrait.h))));
    const width = Math.min(128, portrait.w * scale), height = Math.min(128, portrait.h * scale);
    const x0 = (128 - width) >> 1, y0 = (128 - height) >> 1;
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      const pixel = (Math.floor(y / scale) * portrait.w + Math.floor(x / scale)) * 4;
      if (!portrait.data[pixel + 3]) continue;
      const color = ((portrait.data[pixel] >> 3) << 11) | ((portrait.data[pixel + 1] >> 2) << 5) | (portrait.data[pixel + 2] >> 3);
      if (!palette.has(color)) {
        if (palette.size === 255) throw new Error('portrait exceeds the Playdate image palette');
        palette.set(color, palette.size + 1);
      }
      indices[(y + y0) * 128 + x + x0] = palette.get(color);
    }
    const encoded = core.rleEncode(indices);
    const dataOffset = 24 + 512;
    const size = Math.ceil((0x200 + dataOffset + 8 + encoded.length) / 0x1000) * 0x1000;
    if (GHOST_PLANET + size > GHOST_SIZE) throw new Error('Playdate image is too large');
    out.fill(0xff, GHOST_PLANET, GHOST_SIZE);
    out.fill(0, GHOST_PLANET, GHOST_PLANET + size);
    const sprite = out.subarray(PLANET_SPRITE, GHOST_PLANET + size);
    sprite.set([0x27, 3, 1, 0, 128, 128, 0, 0, 1, 1, 0x11, 1], 4);
    w16(sprite, 18, 24); w16(sprite, 20, dataOffset);
    for (const [color, index] of palette) w16(sprite, 24 + index * 2, color);
    w32(sprite, dataOffset, 8); w32(sprite, dataOffset + 4, encoded.length);
    sprite.set(encoded, dataOffset + 8);
    w32(out, GHOST_PLANET + 8, 0x54485353); w32(out, GHOST_PLANET + 12, size);
    setPlanetNameAndChecksum(out, core);
  }

  const api = {
    ED, GHOST_SIZE, PACKET_SIZE, SPECIAL_FOOD_PAYLOAD, createVisitor, createLabRelease, buildLabRelease,
    checkSpecialFood, createSpecialFood, parseKey, crypt, crc16,
    collectRomVisitors, buildRomGhost, writeRomGhostPlanet, setPlanetNameAndChecksum,
    PLAYDATE_FIELDS, playdateFields, writePlaydateFields,
    ghostAppearanceOptions, romEyes, editGhostAppearance,
  };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.ParamixVisit = api;
})(typeof window !== 'undefined' ? window : globalThis);
