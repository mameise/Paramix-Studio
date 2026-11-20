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
  function createVisitor(opts) {
    const ed = typeof opts.edition === 'number' ? opts.edition : ED[String(opts.edition).toLowerCase()];
    if (!ed) throw new Error('unknown edition');
    if (opts.ghost && opts.ghost.length !== GHOST_SIZE) throw new Error('invalid ghost size');
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
      const a = their.slice();
      if (opts.ghost) a.set(opts.ghost);
      w16(a, GHOST_SIZE, 0);                          // force_send_away
      w16(a, GHOST_SIZE + 2, ed);
      w32(a, GHOST_SIZE + 4, 0x504D5800 | ed);
      const characterId = opts.ghost ? u32(opts.ghost, 0) : u32(their, GHOST_SIZE + 8);
      w32(a, GHOST_SIZE + 8, (characterId ^ 0x5A5A0000 ^ ed) >>> 0);
      return a;
    }
    async function packet(d, type) {
      st.got++;
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

  const GHOST_COMPOSITES = 0x600;
  const GHOST_SPRITES = 0x1d34;
  const GHOST_PLANET = 0x1b000;
  const PLANET_SPRITE = GHOST_PLANET + 0x200;
  const GHOST_POSES = [20, 24];
  const alignWord = (n) => (n + 3) & ~3;
  function collectRomVisitors(ctx, core) {
    if (!ctx?.files?.[0]) return [];
    const pack = ctx.files[0], poses = core.paraLayers(pack), text = core.textTablesOf(ctx.rom);
    if (!poses || !text) return [];
    const groups = new Map();
    const visitors = core.characterSprites(ctx).map((character) => {
      const { group } = character;
      if (!groups.has(group)) {
        const frames = GHOST_POSES.map(count => Array(count).fill(null));
        groups.set(group, frames[0]); groups.set(group + 3, frames[1]);
      }
      const frames = [groups.get(group), groups.get(group + 3)];
      const names = text.tabs.slice(0, 9).map((table) => core.txtWords(table, character.name).slice(0, 12));
      return { character, group, frames, names, pack, entries: ctx.entries };
    });
    for (const frame of poses.frames) {
      const body = frame.find(layer => layer.type === 0 && groups.has(layer.img));
      const frames = groups.get(body?.img);
      if (frames && body.sub < frames.length && !frames[body.sub]) frames[body.sub] = frame;
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
      const part = layer.img - visitor.group;
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
    const { character, group, frames, entries, names } = visitor;
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
      const sprite = entries[group + part];
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
    const sprites = EYE_PARTS.map(part => visitor.entries[visitor.group + part]);
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

  function writeRomGhostPlanet(out, portrait, core) {
    // ROM-only visitors have no live planet. Make a standard inline image from the selected Tama.
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
    const dataOffset = 24 + 512, size = alignWord(dataOffset + 8 + encoded.length);
    if (PLANET_SPRITE + size > GHOST_SIZE) throw new Error('Playdate image is too large');
    const sprite = out.subarray(PLANET_SPRITE, PLANET_SPRITE + size);
    sprite.set([0x27, 3, 1, 0, 128, 128, 0, 0, 1, 1, 0x11, 1], 4);
    w16(sprite, 18, 24); w16(sprite, 20, dataOffset);
    for (const [color, index] of palette) w16(sprite, 24 + index * 2, color);
    w32(sprite, dataOffset, 8); w32(sprite, dataOffset + 4, encoded.length);
    sprite.set(encoded, dataOffset + 8);
    const checksum = wordSum(sprite, 0, size);
    w32(out, GHOST_PLANET, checksum); w32(out, GHOST_PLANET + 4, ~checksum);
    w32(out, GHOST_PLANET + 8, 0x54485353); w32(out, GHOST_PLANET + 12, 0x200 + size);
    const label = Array.from('PARAMIX', (c) => core.TXT_ENC[c]);
    for (let slot = 0; slot < 9; slot++) ghostName(out, GHOST_PLANET + 16 + 26 * slot, label);
  }

  const api = {
    ED, GHOST_SIZE, PACKET_SIZE, createVisitor, parseKey, crypt, crc16,
    collectRomVisitors, buildRomGhost, writeRomGhostPlanet,
    ghostAppearanceOptions, romEyes, editGhostAppearance,
  };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.ParamixVisit = api;
})(typeof window !== 'undefined' ? window : globalThis);
