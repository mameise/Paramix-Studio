/*
 * Paramix Studio: read and write the Paradise over a UART dongle.
 *
 * Two protocols on the same serial line (460800 baud, 8N1):
 *
 * 1. App loading (firmware download screen, "CONNECTION"). The Paradise firmware
 *    accepts a PDLO file over its own transfer protocol: text lines (PKT, ACK,
 *    ENQ, NAK, CAN) and binary chunks of up to 4 KB, each with a 12 byte header
 *    and a keystream from SHA-256(nonce || key). A PDLO file is a 16 byte header
 *    ("PDLO", length in words, token) plus a 64 KB image, word-wise XORed. The
 *    device copies the image to PRAM and starts it. Nothing is written to flash;
 *    switching the device off and on returns to the normal firmware.
 *
 * 2. PAU1, spoken by our own flash app (flash/src/fw.c) once it runs: COBS framed
 *    packets with a CRC-32. HELLO, READ, CRC and the write commands below.
 *
 * Writing goes sector by sector (4 KB): data, then commit, the device erases,
 * programs and compares. Order as in the original tool: user data from 0x111000
 * to the end first, then 0x1000..0x111000, sector 0 (boot header) last. At the
 * end every 64 KB block is checked against its CRC.
 *
 * "Only changes": compare CRCs first (64 KB blocks, then 4 KB sectors) and write
 * only sectors that differ.
 *
 * "Keep saves": sectors that hold game progress are taken from the device
 * instead of the new image (see keepPlan). The rest of the image is written.
 *
 * The transport is an object { write(Uint8Array): Promise, onData: fn } so the
 * same code runs with Web Serial in the browser and with a pipe in tests.
 *
 * Author: mameise. License: GPL-3.0-or-later.
 */
(function (root) {
'use strict';

const FLASH_SIZE = 0x1000000, SECTOR = 0x1000, BLOCK64 = 0x10000;
const BAUD = 460800;
const FIRST_USER = 0x111000;   // write order starts here
// PAU1 commands
const CLOCK_READ = 14, CLOCK_SET = 15;
const HELLO = 1, READ = 5, CRC = 6, WRITE_TEST = 8, WRITE_BEGIN = 9, WRITE_DATA = 10, WRITE_COMMIT = 11, WRITE_FINISH = 12, WRITE_STATE = 13;
// App loading: constants of the device's download protocol (needed to load our
// own flash app, the same values as in the Paradise Flash Tool).
/* link values of the Paradise (chunk key, download file token and mask), kept as two mixed tables (1.6.9 test35) */
const LV_A = [3, 175, 246, 44, 204, 25, 45, 199, 29, 242, 14, 63, 238, 148, 219, 113, 91, 21, 50, 246, 127, 248];
const LV_B = [11, 238, 128, 40, 225, 137, 250, 116, 146, 111, 247, 251, 126, 100, 255, 4, 18, 238, 64, 95, 162, 62];
const LV = (() => { const o = new Uint8Array(LV_A.length + LV_B.length); let s = 0x2C1B3C6D;
  for (let i = 0; i < o.length; i++) { s = (Math.imul(s, 1103515245) + 12345) & 0x7fffffff; o[i] = (i & 1 ? LV_B : LV_A)[i >> 1] ^ ((s >>> 16) & 255); }
  return o; })();
const LINK_KEY = String.fromCharCode(...LV.subarray(0, 32)), PDLO_TOKEN = String.fromCharCode(...LV.subarray(32, 40));
const PDLO_XOR = (LV[40] | LV[41] << 8 | LV[42] << 16 | LV[43] << 24) >>> 0;
const IMAGE_SIZE = 0x10000, CHUNK = 0x1000;
const EDITIONS = { 1: 'Land', 2: 'Water', 3: 'Sky', 4: 'Jade Forest', 5: 'Tropical', 6: 'Glacier' };

// ------------------------------------------------------------------ messages
// English texts; every error and note carries .code and .p so the page can show
// its own translation (keys "fl_<code>" in app.js).
const MSG = {
  timeout: 'no answer from the device ({c} {a})',
  noAck: 'the device does not answer. Is it on the download screen?',
  repeats: 'too many repeats while loading',
  devCancel: 'the device cancelled loading',
  unexpected: 'unexpected answer: {r}',
  hello: 'unexpected HELLO answer',
  notReady: 'flash not ready',
  readErr: 'read error at {a}',
  noCrc: 'no checksum at {a}',
  cancelled: 'cancelled',
  size: 'the image must be exactly 16 MB',
  notParadise: 'not a Paradise image (SONIXDEV missing)',
  cancelledWrite: 'cancelled. Keep the device switched on and write again',
  notAccepted: 'sector {a} not accepted. Keep the device switched on and write again',
  differs: 'sector {a} differs after writing. Keep the device switched on and write again',
  checkFail: 'check failed at {a}. Keep the device switched on and write again',
  noAnswer: 'no answer. Put the device on the download screen and try again',
  lost: 'the device stopped answering before writing. Nothing was written. Switch the Paradise off and on, put it on the download screen, connect again and write again',
  appSilent: 'the flash app was sent but does not answer. Try again',
  unknownEd: 'unknown edition in the new ROM, untick "keep saves"',
  otherEd: 'the device is {d}, the new ROM is {r}. Untick "keep saves" to switch the edition',
  noteNoFit: 'field {f} does not fit into the new slot and is not kept',
  noteMoved: 'field {f} moved to the new slot',
  noteDropped: 'the new ROM has no field menu, stored fields on the device are dropped',
  noteEdKept: 'the device is {d}, the new ROM is {r}: same firmware, the device keeps its save and its field ({d})',
  noteSaveMoved: 'save of {n} taken along',
  noteEmu: 'TamaEmu firmware chosen again, the emulator starts fresh. For an update just open the ROM and save it',
};
function fill(m, p) { if (p) for (const k of Object.keys(p)) m = m.split('{' + k + '}').join(p[k]); return m; }
function fe(code, p) { const e = new Error(fill(MSG[code], p)); e.code = code; e.p = p; return e; }
function note(code, p) { return { code, p, text: fill(MSG[code], p) }; }
const hex = (v) => '0x' + v.toString(16);

// ------------------------------------------------------------------ helpers
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
const ascii = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
function b64(s) {
  if (typeof atob === 'function') { const t = atob(s); const o = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) o[i] = t.charCodeAt(i); return o; }
  return new Uint8Array(Buffer.from(s, 'base64'));
}

const CRC_TAB = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(d, a = 0, n = d.length - a) { let c = 0xFFFFFFFF; for (let i = a; i < a + n; i++) c = (c >>> 8) ^ CRC_TAB[(c ^ d[i]) & 255]; return (c ^ 0xFFFFFFFF) >>> 0; }
function crc16(d) { let c = 0; for (const b of d) { c ^= b; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1; } return c; }
async function sha256(d) { return new Uint8Array(await root.crypto.subtle.digest('SHA-256', d)); }

function cobsEncode(d) {
  const o = new Uint8Array(d.length + Math.ceil(d.length / 254) + 3);
  let n = 1, ci = 1, code = 1; o[0] = 0;   // leading delimiter
  o[n++] = 0;
  for (const b of d) {
    if (b !== 0) { o[n++] = b; code++; if (code === 255) { o[ci] = code; ci = n; o[n++] = 0; code = 1; } }
    else { o[ci] = code; ci = n; o[n++] = 0; code = 1; }
  }
  o[ci] = code; o[n++] = 0;
  return o.subarray(0, n);
}
function cobsDecode(f, len) {
  const o = new Uint8Array(len); let n = 0, i = 0;
  while (i < len) {
    const code = f[i++]; if (code === 0) continue;
    for (let k = 1; k < code && i < len; k++) o[n++] = f[i++];
    if (code !== 255 && i < len) o[n++] = 0;
  }
  return o.subarray(0, n);
}

// ------------------------------------------------------------------ link
/** One serial line: PAU1 frames and text lines are parsed from the same bytes. */
class Link {
  constructor(io, opts = {}) {
    this.io = io; this.scale = opts.timeScale || 1;
    this.session = (Math.random() * 0x7FFFFFFF | 0) + 1; this.seq = 0;
    this.asm = new Uint8Array(0x10000); this.an = 0;
    this.line = []; this.lines = []; this.frames = []; this.wake = null;
    io.onData = (d) => this.feed(d);
  }
  feed(d) {
    for (let i = 0; i < d.length; i++) {
      const b = d[i];
      if (b === 0) { if (this.an) { const f = this.parse(cobsDecode(this.asm, this.an)); if (f) this.frames.push(f); } this.an = 0; }
      else if (this.an < this.asm.length) this.asm[this.an++] = b; else this.an = 0;
      if (b === 10) { const s = String.fromCharCode(...this.line).trim(); this.line = []; if (s) this.lines.push(s); }
      else if (b !== 13) { if (this.line.length < 200 && b >= 32 && b < 127) this.line.push(b); else if (this.line.length >= 200 || b < 32 || b >= 127) this.line = []; }
    }
    if (this.wake) { const w = this.wake; this.wake = null; w(); }
  }
  parse(d) {
    if (d.length < 28 || d[0] !== 0x50 || d[1] !== 0x41 || d[2] !== 0x55 || d[3] !== 0x31) return null;
    const plen = d[20] | (d[21] << 8);
    if (d.length < 28 + plen || u32(d, 24 + plen) !== crc32(d, 0, 24 + plen)) return null;
    return { type: d[6], seq: u32(d, 12), off: u32(d, 16), status: d[22] | (d[23] << 8), payload: d.slice(24, 24 + plen) };
  }
  async until(test, ms, what) {
    const end = Date.now() + ms * this.scale;
    for (;;) {
      const r = test(); if (r) return r;
      const left = end - Date.now(); if (left <= 0) throw new TimeoutError(what);
      await new Promise((res) => { const t = setTimeout(() => { this.wake = null; res(); }, Math.min(left, 500)); this.wake = () => { clearTimeout(t); res(); }; });
    }
  }
  clear() { this.lines = []; this.frames = []; this.line = []; this.an = 0; }
  async writeLine(s) { await this.io.write(ascii(s + '\r\n')); }
  waitLine(ms) { return this.until(() => this.lines.shift(), ms, { c: 'line', a: '' }); }
  frame(type, seq, off, payload) {
    const d = new Uint8Array(28 + payload.length);
    d.set([0x50, 0x41, 0x55, 0x31, 1, 1, type, 0]);
    w32(d, 8, this.session); w32(d, 12, seq); w32(d, 16, off);
    d[20] = payload.length & 255; d[21] = payload.length >> 8;
    d.set(payload, 24); w32(d, 24 + payload.length, crc32(d, 0, 24 + payload.length));
    return cobsEncode(d);
  }
  /** Send one PAU1 request and wait for the matching reply. */
  async request(type, off, payload = new Uint8Array(0), ms = 6000) {
    const seq = ++this.seq;
    await this.io.write(this.frame(type, seq, off >>> 0, payload));
    return this.until(() => {
      while (this.frames.length) { const f = this.frames.shift(); if (f.type === type && f.seq === seq && f.off === off >>> 0) return f; }
      return null;
    }, ms, { c: type, a: hex(off >>> 0) });
  }
}
class TimeoutError extends Error { constructor(what) { super(fill(MSG.timeout, what)); this.code = 'timeout'; this.p = what; } }

// ------------------------------------------------------------------ app loading
/** Make a PDLO file from a raw image (up to 64 KB, runs from PRAM 0). */
function buildPdlo(img) {
  const head = new Uint8Array(16); head.set(ascii('PDLO')); w32(head, 4, IMAGE_SIZE / 4); head.set(ascii(PDLO_TOKEN), 8);
  const body = new Uint8Array(IMAGE_SIZE); body.set(img.subarray(0, IMAGE_SIZE));
  const f = new Uint8Array(16 + IMAGE_SIZE); f.set(head); f.set(body, 16);
  for (let i = 0; i < f.length; i += 4) w32(f, i, u32(f, i) ^ PDLO_XOR);
  return f;
}
let keystream = null;
async function chunk(idx, payload) {
  const nonce = new Uint8Array([1, 0, 0, 0]);
  if (!keystream) { const s = new Uint8Array(4 + 32); s.set(nonce); s.set(ascii(LINK_KEY), 4); keystream = await sha256(s); }
  const body = new Uint8Array(12 + payload.length);
  body.set(ascii('TCP'), 4); body[7] = 3; body[8] = idx & 255; body[9] = 255 - (idx & 255);
  const c = crc16(payload); body[10] = c & 255; body[11] = c >> 8;
  body.set(payload, 12);
  const k = keystream.slice();
  for (let i = 0; i < body.length; i++) { const j = i % 32; body[i] ^= k[j]; k[j] = (k[j] * 2 + 1) & 255; }
  const out = new Uint8Array(4 + body.length); out.set(nonce); out.set(body, 4);
  return out;
}
/** Is the device on the download screen? */
async function probe(link) {
  link.clear(); await link.writeLine('ECHO REQ');
  try { return (await link.waitLine(2000)).startsWith('ECHO REP'); } catch (e) { return false; }
}
/** Load an image into the device (download screen) and start it. */
async function loadApp(link, img, progress) {
  const file = buildPdlo(img);
  link.clear(); await link.writeLine('PKT ' + file.length);
  const a = await link.waitLine(4000).catch(() => '');
  if (!a.startsWith('ACK')) throw fe('noAck');
  let idx = 0, guard = 0;
  while (idx * CHUNK < file.length) {
    if (++guard > 200) throw fe('repeats');
    const off = idx * CHUNK, n = Math.min(CHUNK, file.length - off);
    await link.io.write(await chunk(idx, file.subarray(off, off + n)));
    const r = await link.waitLine(6000);
    if (r.startsWith('ACK')) { idx++; if (progress) progress(Math.min(idx * CHUNK, file.length), file.length); }
    else if (r.startsWith('ENQ')) { const e = parseInt(r.slice(3), 10); if (!isNaN(e)) idx = e; }
    else if (r.startsWith('NAK')) { /* same chunk again */ }
    else if (r.startsWith('CAN')) throw fe('devCancel');
    else throw fe('unexpected', { r });
  }
}

// ------------------------------------------------------------------ device
class Paradise {
  constructor(link) { this.link = link; this.info = null; }
  get block() { const b = this.info.maxBlock; return b === 1024 || b === 2048 || b === 4096 ? b : 1024; }
  async hello(ms = 6000) {
    const r = await this.link.request(HELLO, 0, undefined, ms);
    const p = r.payload; if (p.length !== 44) throw fe('hello');
    this.info = { edition: p[8], wave: p[9], maxBlock: p[10] | (p[11] << 8), jedec: u32(p, 24), capacity: u32(p, 32), flashError: u32(p, 36) };
    // The flash app always reports 1 (Land). The real edition is the ID page in the flash
    // (0xD49000, Tropical/Glacier 0xD29000), read like the Studio reads a ROM (1.3.0).
    try {
      const x = await this.read(0xD49000, 4), v = u32(x, 0);
      if (v >= 1 && v <= 6) this.info.edition = v;
      else { const y = await this.read(0xD29000, 4), w = u32(y, 0); if (w >= 1 && w <= 6) this.info.edition = w; }
    } catch (e) { if (!(e instanceof TimeoutError)) throw e; }
    this.info.name = EDITIONS[this.info.edition] || 'Edition ' + this.info.edition;
    return this.info;
  }
  ready() { if (!this.info || this.info.capacity !== FLASH_SIZE || this.info.flashError) throw fe('notReady'); }
  async read(off, len) {
    const out = new Uint8Array(len), blk = this.block;
    for (let o = 0; o < len; o += blk) {
      let r = null;
      for (let t = 0; t < 5 && !r; t++) {
        try { const x = await this.link.request(READ, off + o, u32le(blk)); if (x.payload.length === blk && !x.status) r = x; }
        catch (e) { if (!(e instanceof TimeoutError)) throw e; }
      }
      if (!r) throw fe('readErr', { a: hex(off + o) });
      out.set(r.payload.subarray(0, Math.min(blk, len - o)), o);
    }
    return out;
  }
  async crc(off, len) {
    for (let t = 0; t < 4; t++) {
      try { const c = await this.link.request(CRC, off, u32le(len), 15000); if (c.payload.length === 4 && !c.status) return u32(c.payload, 0); }
      catch (e) { if (!(e instanceof TimeoutError)) throw e; }
    }
    throw fe('noCrc', { a: hex(off) });
  }
  /** Seconds counter of the Paradise (1.3.0 flash app). null if the app is older. */
  async clockRead() {
    try { const r = await this.link.request(CLOCK_READ, 0, undefined, 2500); if (r.payload.length === 8) return { counter: u32(r.payload, 0), ctrl: u32(r.payload, 4) }; }
    catch (e) { if (!(e instanceof TimeoutError)) throw e; }
    return null;
  }
  /** Set the seconds counter. Returns true if the device took it. */
  async clockSet(v) {
    try { const r = await this.link.request(CLOCK_SET, 0, u32le(v >>> 0), 2500); return r.payload.length === 4 && !r.status && u32(r.payload, 0) - v < 5; }
    catch (e) { if (!(e instanceof TimeoutError)) throw e; }
    return false;
  }
  /** Which 4 KB sectors in [from, to) are not empty (0xFF)? Uses the CRC, reads nothing. */
  async usedSectors(from, to, progress, cancel) {
    const ff = new Uint8Array(BLOCK64).fill(0xFF), cB = crc32(ff, 0, BLOCK64), cS = crc32(ff, 0, SECTOR), out = [];
    for (let off = from & ~(BLOCK64 - 1); off < to; off += BLOCK64) {
      if (cancel && cancel()) throw fe('cancelled');
      if (await this.crc(off, BLOCK64) !== cB) for (let s = off; s < off + BLOCK64; s += SECTOR) if (s >= from && s < to && await this.crc(s, SECTOR) !== cS) out.push(s);
      if (progress) progress('compare', off + BLOCK64 - from, to - from);
    }
    return out;
  }
  /** Writer state; asked again a few times, then a ping (1.5.6: a lost answer here broke the write). */
  async writeState() {
    for (let t = 0; t < 3; t++) {
      try { return await this.link.request(WRITE_STATE, 0, undefined, 6000); }
      catch (e) { if (!(e instanceof TimeoutError)) throw e; }
      try { await this.link.request(HELLO, 0, undefined, 3000); } catch (e) { if (!(e instanceof TimeoutError)) throw e; }
    }
    throw fe('lost');
  }
  /** Write single 4 KB sectors (Map address -> data), checked one by one. */
  async writeSectors(map, progress, cancel) {
    this.ready();
    const list = [...map.keys()].sort((a, b) => a - b);
    const st = await this.writeState();
    if (!(st.payload.length >= 16 && u32(st.payload, 12))) await this.link.request(WRITE_TEST, 0, undefined, 15000);
    await this.link.request(WRITE_BEGIN, 0, new Uint8Array(36), 15000);
    const blk = this.block;
    let n = 0;
    for (const s of list) {
      if (cancel && cancel()) throw fe('cancelledWrite');
      const d = map.get(s);
      for (let o = 0; o < SECTOR; o += blk) await this.link.request(WRITE_DATA, s + o, d.subarray(o, o + blk), 15000);
      const c = await this.link.request(WRITE_COMMIT, s, u32le(crc32(d, 0, SECTOR)), 15000);
      if (c.payload.length !== 8) throw fe('notAccepted', { a: hex(s) });
      if (u32(c.payload, 4) || u32(c.payload, 0) !== crc32(d, 0, SECTOR)) throw fe('differs', { a: hex(s) });
      n++; if (progress) progress('write', n, list.length);
    }
    await this.link.request(WRITE_FINISH, 0, undefined, 15000);
    return n;
  }
  /** Whole flash (16 MB). */
  async dump(progress, cancel) {
    this.ready();
    const img = new Uint8Array(FLASH_SIZE);
    for (let off = 0; off < FLASH_SIZE; off += BLOCK64) {
      if (cancel && cancel()) throw fe('cancelled');
      img.set(await this.read(off, BLOCK64), off);
      if (progress) progress('read', off + BLOCK64, FLASH_SIZE);
    }
    return img;
  }
  /** Sectors (4 KB offsets) where the device differs from img, in [from, to). */
  async changed(img, progress, cancel, from = 0, to = FLASH_SIZE) {
    const out = new Set();
    for (let off = from; off < to; off += BLOCK64) {
      if (cancel && cancel()) throw fe('cancelled');
      if (await this.crc(off, BLOCK64) !== crc32(img, off, BLOCK64)) {
        for (let s = off; s < off + BLOCK64; s += SECTOR) if (await this.crc(s, SECTOR) !== crc32(img, s, SECTOR)) out.add(s);
      }
      if (progress) progress('compare', off + BLOCK64 - from, to - from);
    }
    return out;
  }
  /**
   * Write img. opts: { onlyChanged, keep (function from keepPlan or null) }.
   * Returns { written, kept, notes, image } (image = what the device holds now).
   */
  async write(img, opts, progress, cancel) {
    if (img.length !== FLASH_SIZE) throw fe('size');
    if (String.fromCharCode(...img.subarray(0, 8)) !== 'SONIXDEV') throw fe('notParadise');
    this.ready();
    let image = img, notes = [], kept = 0, changed = null;
    if (opts.onlyChanged || opts.keep) changed = await this.changed(image, progress, cancel);
    if (opts.keep) {
      const k = await opts.keep(this, image, changed);
      image = k.image; notes = k.notes; kept = k.kept;
      for (const s of k.protectedSectors) changed.delete(s);
    }
    const st = await this.writeState();
    if (!(st.payload.length >= 16 && u32(st.payload, 12))) { if (progress) progress('prepare', 0, 1); await this.link.request(WRITE_TEST, 0, undefined, 15000); }
    let order = sectorOrder();
    if (opts.onlyChanged) {
      order = order.filter((s) => changed.has(s));
      if (!order.length) { await this.link.request(WRITE_FINISH, 0, undefined, 15000); return { written: 0, kept, notes, image }; }
    }
    const begin = new Uint8Array(36); begin.set(await sha256(image)); w32(begin, 32, crc32(image));
    const pos = await this.link.request(WRITE_BEGIN, 0, begin, 15000);
    let start = !opts.onlyChanged && pos.payload.length >= 4 ? u32(pos.payload, 0) : 0;
    if (start >= order.length) start = 0;
    const blk = this.block;
    let written = 0;
    for (let i = start; i < order.length; i++) {
      if (cancel && cancel()) throw fe('cancelledWrite');
      const s = order[i];
      for (let o = 0; o < SECTOR; o += blk) await this.link.request(WRITE_DATA, s + o, image.subarray(s + o, s + o + blk), 15000);
      const c = await this.link.request(WRITE_COMMIT, s, u32le(crc32(image, s, SECTOR)), 15000);
      if (c.payload.length !== 8) throw fe('notAccepted', { a: hex(s) });
      if (u32(c.payload, 4)) throw fe('differs', { a: hex(s) });
      written++;
      if (progress) progress('write', i + 1 - start, order.length - start);
    }
    for (let off = 0; off < FLASH_SIZE; off += BLOCK64) {
      if (await this.crc(off, BLOCK64) !== crc32(image, off, BLOCK64)) throw fe('checkFail', { a: hex(off) });
      if (progress) progress('check', off + BLOCK64, FLASH_SIZE);
    }
    await this.link.request(WRITE_FINISH, 0, undefined, 15000);
    return { written, kept, notes, image };
  }
}
const u32le = (v) => { const b = new Uint8Array(4); w32(b, 0, v); return b; };
function sectorOrder() {
  const l = [];
  for (let s = FIRST_USER; s < FLASH_SIZE; s += SECTOR) l.push(s);
  for (let s = SECTOR; s < FIRST_USER; s += SECTOR) l.push(s);
  l.push(0);
  return l;
}

/** Open the device: talk to the flash app, or load it first from the download screen. */
async function connect(io, appImage, opts = {}, status) {
  const link = new Link(io, opts), dev = new Paradise(link);
  try { await dev.hello(2500); return { dev, loaded: false }; }
  catch (e) { if (!(e instanceof TimeoutError)) throw e; }
  if (!(await probe(link))) throw fe('noAnswer');
  if (status) status('load', 0, 1);
  await loadApp(link, appImage, (d, t) => status && status('load', d, t));
  await sleep(1200 * (opts.timeScale || 1));
  link.clear();
  for (let t = 0; t < 4; t++) {
    try { await dev.hello(4000); return { dev, loaded: true }; }
    catch (e) { if (!(e instanceof TimeoutError)) throw e; }
  }
  throw fe('appSilent');
}

// ------------------------------------------------------------------ keep saves
/*
 * Which sectors of the new image hold game progress, and is the device's copy
 * of them still valid for the new image? Ranges (taken from the device):
 *   - Paradise save data: from the page after the edition ID page to the end of
 *     the flash (same edition only, otherwise keeping makes no sense),
 *   - field slots of the Paramix menu, when the device has the same slot layout
 *     (a different layout: the stored fields are copied into the new slots),
 *   - TamaEmu: run state and the guest firmware (holds the guest's own save),
 *     when the device runs the same guest with the same packing and the guest
 *     was not changed in the Studio,
 *   - DOOM: its save sectors, when the device runs DOOM too.
 * F and E are the feld.js and emu.js modules, menu = { stub } of the menu (or
 * null); opts.emuChanged = guest replaced in the Studio.
 */
/* T18Emu's settings on the device (d at dc) and in the new image (n at nc) describe the same save: same save place,
 * block map and spares, where the new one may have more spares behind (test29: spares added keep the save). */
function t18SameGrown(d, dc, n, nc) {
  const eq = (a, len) => { for (let i = 0; i < len; i++) if (d[dc + a + i] !== n[nc + a + i]) return false; return true; };
  const nd = u32(d, dc + 12), nn = u32(n, nc + 12);
  if (nd > 64 || nn > 64 || nn < nd) return false;
  return eq(8, 4) && eq(16, 256) && eq(272, 2 * nd) && (nn === nd ? eq(272, 128) : true);
}
function keepPlan(F, E, menu, opts = {}) {
  return async (dev, img, changed) => {
    const ed = F.edition(img);
    if (!ed) throw fe('unknownEd');
    // 1.6.6: Land/Water/Sky and Tropical/Glacier are the same firmware; the edition ID page only follows the
    // active field. Within such a family the device's ID page and save stay (so the device keeps its field).
    const fam = (id) => (F.familyOf ? F.familyOf(id) : null);
    const sameFam = dev.info.edition !== ed.id && fam(dev.info.edition) && fam(dev.info.edition) === fam(ed.id);
    if (dev.info.edition !== ed.id && !sameFam) throw fe('otherEd', { d: dev.info.name, r: EDITIONS[ed.id] || ed.id });
    const image = new Uint8Array(img), notes = [];
    if (sameFam) notes.push(note('noteEdKept', { d: dev.info.name, r: EDITIONS[ed.id] || ed.id }));
    // the device's program area (stub, menu, apps), only the sectors that differ
    const view = new Uint8Array(img);
    for (const s of changed) if (s >= 0x90000 && s < 0x110000) view.set(await dev.read(s, SECTOR), s);
    // six fields keep pictures from 0xF00000; a start device (1.6.8) may have programs there (the Paradise never writes there)
    const launchImg = !!(menu && F.isLaunch && F.isLaunch(img, menu.stub));
    const ranges = [[ed.profile.idPage + (sameFam ? 0 : SECTOR), (F.isSix && F.isSix(img)) || launchImg ? 0xF00000 : FLASH_SIZE]];
    // field slots
    const fi = menu && F.feldFind(img, menu.stub), fd = menu && F.feldFind(view, menu.stub);
    if (fd && (fd.flags & 1)) {
      const at = (f, i) => (F.slotAddr ? F.slotAddr(f, i) : f.slotOff + i * f.slotSize);
      if (fi && (fi.flags & 1) && fi.slotOff === fd.slotOff && fi.slotSize === fd.slotSize && fi.nslots === fd.nslots && (fi.slotOff2 || 0) === (fd.slotOff2 || 0) && (fi.slotN1 || 0) === (fd.slotN1 || 0)) {
        for (let i = 0; i < fd.nslots; i++) ranges.push([at(fd, i), at(fd, i) + fd.slotSize]);
      }
      else if (fi && (fi.flags & 1) && F.slotReadAsync) {
        // other layout: copy every stored field into the new slots (shared pool, 1.2.12)
        const slots = [];
        for (let i = 0; i < fd.nslots; i++) {
          const s = await F.slotReadAsync((a, n) => dev.read(a, n), fd, i);
          if (!s) continue;
          const idx = fd.version >= 3 ? s.field - 1 : i;
          if (idx < 0 || idx >= fi.nslots) { notes.push(note('noteNoFit', { f: s.field })); continue; }
          slots.push([idx, s]);
        }
        // the device's saves replace whatever the new image carries in the slots
        for (let i = 0; i < fi.nslots; i++) image.fill(0xFF, at(fi, i), at(fi, i) + fi.slotSize);
        const used = new Set();
        for (const [idx, s] of slots) {
          try { F.slotPlace(image, fi, idx, s, used); notes.push(note('noteMoved', { f: s.field })); }
          catch (e) { notes.push(note('noteNoFit', { f: s.field })); }
        }
        for (let i = 0; i < fi.nslots; i++) for (let a = at(fi, i); a < at(fi, i) + fi.slotSize; a += SECTOR) if (crc32(image, a, SECTOR) !== crc32(img, a, SECTOR)) changed.add(a);
      } else notes.push(note('noteDropped'));
    }
    const guestR = [];   // 1.6.8: TamaEmu firmware areas; sectors the Studio changed there (download items) come from the image
    const mine = opts.mine || null;
    // TamaEmu: settings block at 0x0A0140 (guest size and block map decide the layout)
    const kind = F.appKind(img), dkind = F.appKind(view);
    const T7 = 0x37584D50, t7i = u32(img, 0xA0140) === T7, t7d = u32(view, 0xA0140) === T7;
    const T18 = 0x38584D50, t18i = u32(img, 0xA0140) === T18, t18d = u32(view, 0xA0140) === T18, PMXC_ = 0x43584D50, t18keepX = [];
    if (t7i && t7d) {
      // T7Emu (1.4.0): the pet's EEPROM stays as long as the V7 ROM is the same, the session also needs the same figures
      const ro = u32(img, 0xA0148), rl = u32(img, 0xA014C);
      let sameRom = ro === u32(view, 0xA0148) && rl === u32(view, 0xA014C);
      for (let a = ro & ~0xFFF; sameRom && a < ro + rl; a += SECTOR) if (changed.has(a)) sameRom = false;
      if (sameRom && opts.t7Save) ranges.push([0xC5000, 0xC6000]);   // an imported save.bin replaces the pet, the settings stay
      else if (sameRom) {
        ranges.push([0xC1000, 0xC6000]);   // EEPROM slots and the settings
        if (view.subarray(0xA0148, 0xA0140 + 24 + 16 * 24).every((v, i) => v === img[0xA0148 + i])) ranges.push([0xC0000, 0xC1000]);
      } else notes.push(note('noteEmu'));
    } else if (u32(img, 0xA0140) === 0x31584D50 && u32(view, 0xA0140) === 0x31584D50) {
      // V1Emu as the main program (1.6.8): session and settings stay with the same ROM at the same place
      const ro = u32(img, 0xA0148), rl = u32(img, 0xA014C), so = u32(img, 0xA0150);
      let same = [0x148, 0x14C, 0x150].every((k) => u32(view, 0xA0000 + k) === u32(img, 0xA0000 + k));
      for (let x = ro & ~0xFFF; same && x < ro + rl; x += SECTOR) if (changed.has(x)) same = false;
      if (same) ranges.push([so, so + 0x2000]); else { ranges.push([so + 0x1000, so + 0x2000]); notes.push(note('noteEmu')); }
    } else if (kind === 2 && dkind === 2 && !t7i && !t7d && !t18i && !t18d && u32(img, 0xA0140) !== 0x31584D50 && u32(view, 0xA0140) !== 0x31584D50) {
      // same firmware layout (offset, size, save area, device, block map); the program version may differ
      const same = view.subarray(0xA0148, 0xA0140 + 24 + 128).every((v, i) => v === img[0xA0148 + i]);
      if (same && !opts.emuChanged) {
        const gOff = u32(img, 0xA0148), gLen = u32(img, 0xA014C);
        // with the packed firmware also the spare pages behind it (item stores, since v0.9.2)
        const end = u32(img, 0xA0144) >= 3 ? Math.min(u32(img, 0xA0140 + 152), 0xD48000) : gOff + gLen;
        ranges.push([0xC0000, 0x110000], [gOff, Math.max((gOff + gLen + 0xFFF) & ~0xFFF, end)]);
        guestR.push([gOff, Math.max((gOff + gLen + 0xFFF) & ~0xFFF, end)]);
      } else notes.push(note('noteEmu'));
    }
    if (kind === 1 && dkind === 1) ranges.push([0x10E000, 0x110000]);
    // more programs of the menu (1.6.0): a save stays when the device has the same program at the same
    // place with the same ROM
    const xT18 = [];   // T18Emu as a further program (start device, 1.6.8): its blocks, like the main one's below
    if (menu && F.feldApps) {
      const ai = F.feldApps(img, menu.stub).apps, ad = F.feldApps(view, menu.stub).apps;
      for (const [ix, a] of ai.entries()) {
        const off = a.src - 0x10000000 + 0x11000;
        const mag = u32(img, off + 0x140);
        if (mag === T18) {
          const c = off + 0x140, ns = Math.min(u32(img, c + 12), 64), bl = [];
          for (let i = 0; i < 128; i++) { const b = img[c + 16 + 2 * i] | (img[c + 17 + 2 * i] << 8); if (b !== 0xFFFF) bl.push(b); }
          for (let i = 0; i < ns; i++) bl.push(img[c + 272 + 2 * i] | (img[c + 273 + 2 * i] << 8));
          if (img[c + 400] & 4) bl.push(0xFD, 0xFE);
          xT18.push(...bl);
          if (!ad.some((d) => d.src === a.src)) continue;
          const dv = changed.has(off) ? await dev.read(off, SECTOR) : img.subarray(off, off + SECTOR);
          if (u32(dv, 0x140) === T18 && t18SameGrown(dv, 0x140, img, c) && !opts.emuChanged) {
            const so = u32(img, c + 8); ranges.push([so, so + 0x24000]);
            for (let i = 0; i < ns; i++) { const b = img[c + 272 + 2 * i] | (img[c + 273 + 2 * i] << 8); t18keepX.push([b << 16, (b + 1) << 16]); }
          } else notes.push(note('noteEmu'));
          continue;
        }
        if (!ad.some((d) => d.src === a.src)) continue;
        const dv = changed.has(off) ? await dev.read(off, SECTOR) : img.subarray(off, off + SECTOR);
        if (mag === PMXC_ && u32(img, off + 0x144) >= 3) {
          // TamaEmu as a further program: run state and spare pages stay with the same firmware layout
          const same = u32(dv, 0x140) === PMXC_ && dv.subarray(0x148, 0x140 + 156).every((v, i) => v === img[off + 0x148 + i]);   // the flags (+156: clock, NFC) may change
          const gOff = u32(img, off + 0x148), gLen = u32(img, off + 0x14C), so = u32(img, off + 0x150), se = u32(img, off + 0x140 + 152);
          let sameFw = same;
          if (mine && same) {
            // 1.6.8: the same firmware when its code blocks (reset vector) are untouched by the Studio; the rest of
            // its flash (its own writes) stays from the device, except what the Studio changed (download items)
            const rv = u32(img, off + 0x140 + 20) === 0 ? 0 : 0x40;
            for (const b of [rv, rv + 1]) { const k = img[off + 0x140 + 24 + b]; if (k === 0xFF) continue; const kk = k === 0xFE ? b : k; for (let x = gOff + (kk << 16); sameFw && x < gOff + ((kk + 1) << 16); x += SECTOR) if (mine.has(x) && changed.has(x)) sameFw = false; }
            if (sameFw) { ranges.push([gOff, gOff + gLen]); guestR.push([gOff, gOff + gLen]); }
          } else for (let x = gOff; sameFw && x < gOff + gLen; x += SECTOR) if (changed.has(x)) sameFw = false;
          if (sameFw) ranges.push([so, so + 0x50000], [(gOff + gLen + 0xFFF) & ~0xFFF, se]); else notes.push(note('noteEmu'));
          continue;
        }
        if (mag === 0x47584D50) {   // GBEmu (1.6.9 test36): settings and cartridge RAM stay with the same game at the same place
          const sameG = [0x140, 0x148, 0x14C, 0x150].every((k) => u32(dv, k) === u32(img, off + k));
          const ro = u32(img, off + 0x148), rl = u32(img, off + 0x14C), so = u32(img, off + 0x150);
          let sameRom = sameG; for (let x = ro & ~0xFFF; sameRom && x < ro + rl; x += SECTOR) if (changed.has(x)) sameRom = false;
          if (sameRom) ranges.push([so, so + 0x9000]); else notes.push(note('noteEmu'));
          continue;
        }
        if (mag === 0x57584D50) {   // Wolf3D (1.6.10 test1): settings and the saved game stay with the same pack at the same place
          const sameW = [0x140, 0x148, 0x14C, 0x150].every((k) => u32(dv, k) === u32(img, off + k));
          const ro = u32(img, off + 0x148), rl = u32(img, off + 0x14C), so = u32(img, off + 0x150);
          let samePack = sameW; for (let x = ro & ~0xFFF; samePack && x < ro + rl; x += SECTOR) if (changed.has(x)) samePack = false;
          if (samePack) ranges.push([so, so + 0x10000]); else notes.push(note('noteEmu'));
          continue;
        }
        if (mag === 0x31584D50) {   // V1Emu (1.6.8): the session stays with the same ROM at the same place
          const sameV = [0x140, 0x148, 0x14C, 0x150].every((k) => u32(dv, k) === u32(img, off + k));
          const ro = u32(img, off + 0x148), rl = u32(img, off + 0x14C), so = u32(img, off + 0x150);
          let sameRom = sameV; for (let x = ro & ~0xFFF; sameRom && x < ro + rl; x += SECTOR) if (changed.has(x)) sameRom = false;
          if (sameRom) ranges.push([so, so + 0x2000]); else { ranges.push([so + 0x1000, so + 0x2000]); notes.push(note('noteEmu')); }   // session, settings
          continue;
        }
        const same = [0x140, 0x148, 0x14C, 0x150].every((k) => u32(dv, k) === u32(img, off + k)) && u32(img, off + 0x140) === T7;
        if (!same) continue;
        const ro = u32(img, off + 0x148), rl = u32(img, off + 0x14C), so = u32(img, off + 0x150);
        let sameRom = true;
        for (let x = ro & ~0xFFF; sameRom && x < ro + rl; x += SECTOR) if (changed.has(x)) sameRom = false;
        if (sameRom && opts.t7SaveX === ix) ranges.push([so + 0x5000, so + 0x6000]);   // an imported save.bin replaces the pet, the settings stay
        else if (sameRom) ranges.push([so, so + 0x6000]); else notes.push(note('noteEmu'));
      }
    }
    // T18Emu (1.5.0): firmware blocks come from the image (they may lie in areas that are kept
    // otherwise); its save (map, clock note, session at 0x0C0000 and the spare blocks the firmware
    // wrote) stays when the block map is the same
    const t18blocks = xT18.slice(), t18keep = t18keepX;
    if (t18i) {
      const c = 0xA0140, ns = Math.min(u32(img, c + 12), 64);
      for (let i = 0; i < 128; i++) { const b = img[c + 16 + 2 * i] | (img[c + 17 + 2 * i] << 8); if (b !== 0xFFFF) t18blocks.push(b); }
      // spares must be empty unless the save that uses them is kept (they may lie in the Paradise save range)
      for (let i = 0; i < ns; i++) t18blocks.push(img[c + 272 + 2 * i] | (img[c + 273 + 2 * i] << 8));
      if (img[c + 400] & 4) t18blocks.push(0xFD, 0xFE);   // hot window (1.6.6): second copy of the flash part
      const same = t18d && t18SameGrown(view, c, img, c);
      if (same && !opts.emuChanged) {
        ranges.push([0xC0000, 0xC0000 + 0x24000]);   // map, clock note, session (the program's flash part at 0x0F0000 comes from the image)
        for (let i = 0; i < ns; i++) { const b = img[c + 272 + 2 * i] | (img[c + 273 + 2 * i] << 8); t18keep.push([b << 16, (b + 1) << 16]); }
      } else if (t18d) notes.push(note('noteEmu'));
    }
    // T18Emu leaves the device or gets other blocks (1.5.6): its old firmware blocks and spares are no
    // save of the Paradise; they take what the new image has there (mostly empty) instead of being kept
    const t18old = [];
    if (t18d && !t18keep.length) {
      const c = 0xA0140, ns = Math.min(u32(view, c + 12), 64);
      for (let i = 0; i < 128; i++) { const b = view[c + 16 + 2 * i] | (view[c + 17 + 2 * i] << 8); if (b !== 0xFFFF && b < 0x100) t18old.push(b); }
      for (let i = 0; i < ns; i++) { const b = view[c + 272 + 2 * i] | (view[c + 273 + 2 * i] << 8); if (b < 0x100) t18old.push(b); }
      if (view[c + 400] & 4) t18old.push(0xFD, 0xFE);
    }
    // 1.6.8: saves that moved (main program <-> menu, other place in the menu): T7Emu and V1Emu by their ROM.
    // The device's save of the same ROM is copied to the program's new save area.
    if (menu && F.feldApps && opts.moveSaves !== false) {
      const T7M = 0x37584D50, V1M = 0x31584D50;
      const progs = async (b, rd, apps) => {
        const out = [], one = async (base, cfgOf) => {
          const c = await cfgOf(base); const m = u32(c, 0x140);
          if (m !== T7M && m !== V1M) return;
          const isMain = base === 0xA0000, so = u32(c, 0x150);
          out.push({ kind: m, romOff: u32(c, 0x148), romLen: u32(c, 0x14C), saveOff: so, saveLen: m === V1M ? 0x2000 : (isMain ? 0x6000 : 0x6000), base });
        };
        await one(0xA0000, async (o) => rd(o, SECTOR));
        for (const a of apps) await one(a.src - 0x10000000 + 0x11000, async (o) => rd(o, SECTOR));
        return out;
      };
      const imgRd = async (o, n) => img.subarray(o, o + n);
      const devRd = async (o, n) => (changed.has(o) ? dev.read(o, n) : img.subarray(o, o + n));
      try {
        const ip = await progs(img, imgRd, F.feldApps(img, menu.stub).apps), dp = await progs(view, devRd, F.feldApps(view, menu.stub).apps);
        const covered = (a) => ranges.some(([x, y]) => a >= x && a < y);
        for (const p of ip) {
          if (covered(p.saveOff)) continue;
          const want = crc32(img, p.romOff, p.romLen);
          for (const d of dp) {
            if (d.kind !== p.kind || d.romLen !== p.romLen || (d.base === p.base && d.saveOff === p.saveOff)) continue;
            const dc = dev.crc ? await dev.crc(d.romOff, d.romLen) : crc32(await dev.read(d.romOff, d.romLen), 0, d.romLen);
            if (dc !== want) continue;
            const len = Math.min(p.saveLen, d.saveLen), data = await dev.read(d.saveOff, len);
            image.set(data, p.saveOff);
            for (let s = p.saveOff; s < p.saveOff + len; s += SECTOR) changed.add(s);
            notes.push(note('noteSaveMoved', { n: p.kind === V1M ? 'V1Emu' : 'T7Emu' }));
            break;
          }
        }
      } catch (e) { /* only an extra; the normal plan stands */ }
    }
    // take the device's copy of every protected sector that differs
    const prot = new Set();
    for (const [a, b] of ranges) for (let s = a & ~0xFFF; s < b; s += SECTOR) prot.add(s);
    for (const b of t18blocks.concat(t18old)) for (let s = b << 16; s < (b + 1) << 16; s += SECTOR) prot.delete(s);
    for (const [a, b] of t18keep) for (let s = a; s < b; s += SECTOR) prot.add(s);
    if (mine) for (const [a, b] of guestR) for (let s = a & ~0xFFF; s < b; s += SECTOR) if (mine.has(s)) prot.delete(s);
    for (const [a, b] of opts.fresh || []) for (let s = a & ~0xFFF; s < b; s += SECTOR) prot.delete(s);   // 1.6.8: imported saves
    let kept = 0;
    for (const s of prot) if (changed.has(s)) { image.set(await dev.read(s, SECTOR), s); kept++; }
    const protectedSectors = [...prot].filter((s) => changed.has(s));
    return { image, notes, kept, protectedSectors };
  };
}

const FLASH_APP_B64 = '__FLASH_APP_B64__';
function embedded() { return FLASH_APP_B64.startsWith('__') ? null : b64(FLASH_APP_B64); }

/** Web Serial transport (Chrome, Edge). */
async function serialOpen(port) {
  await port.open({ baudRate: BAUD, bufferSize: 1 << 16 });
  const writer = port.writable.getWriter();
  const io = { onData: null, closed: false };
  let reader = null, stop = false;
  (async () => {
    // a buffer overrun, framing or parity error ends the reader but not the port: take a new one (1.5.6)
    while (!stop && port.readable) {
      reader = port.readable.getReader();
      try { for (;;) { const { value, done } = await reader.read(); if (done) break; if (value && io.onData) io.onData(value); } }
      catch (e) { io.errors = (io.errors || 0) + 1; }
      try { reader.releaseLock(); } catch (e) {}
      if (stop) break;
    }
    io.closed = true;
  })();
  io.write = (d) => writer.write(d);
  io.close = async () => { stop = true; try { await reader.cancel(); } catch (e) {} try { reader.releaseLock(); } catch (e) {} try { writer.releaseLock(); } catch (e) {} try { await port.close(); } catch (e) {} };
  return io;
}

root.ParamixFlash = { linkKey: LINK_KEY, connect, keepPlan, Link, Paradise, serialOpen, embedded, buildPdlo, crc32, sectorOrder, EDITIONS, FLASH_SIZE, BAUD, TimeoutError };
if (typeof module !== 'undefined') module.exports = root.ParamixFlash;
})(typeof window !== 'undefined' ? window : globalThis);
