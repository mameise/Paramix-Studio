// Optional inputs: a Capybara UART capture, a Paradise ROM and an adult Playdate file.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const V = require('../src/visit');
const C = require('../src/core');
const F = require('../src/flash');

const [capture, romPath, ghostPath] = process.argv.slice(2);
const key = V.parseKey('TestKeyOnly-NotTheReal-0123456789');
const encoder = new TextEncoder();
const sendLine = (session, text) => session.feed(encoder.encode(text + '\r\n'));

function adultGhost() {
  const ghost = Buffer.alloc(V.GHOST_SIZE);
  ghost.writeUInt16LE(5, 0xfc);
  ghost.writeUInt16LE(4152, 0x10a);
  let at = 0x1d34;
  for (let i = 0; i < 6; i++) {
    ghost.writeUInt32LE(at, 0x110 + 8 * i);
    ghost.writeUInt32LE(60, 0x114 + 8 * i);
    ghost.writeUInt32LE(60, at);
    ghost[at + 5] = 2;
    ghost.writeUInt16LE(1, at + 6);
    ghost[at + 8] = 2;
    ghost[at + 9] = 2;
    ghost[at + 12] = 1;
    ghost[at + 13] = 1;
    ghost.writeUInt16LE(24, at + 18);
    ghost.writeUInt16LE(56, at + 20);
    at += 60;
  }
  ghost.writeUInt32LE(at, 0x10c);
  return ghost;
}

async function readFrame(raw, frameKey, index) {
  const frame = Buffer.from(await V.crypt(raw.subarray(0, 4), frameKey, raw.subarray(4)));
  const data = frame.subarray(12);
  assert.equal(frame.subarray(4, 7).toString(), 'TCP');
  assert.equal(frame[8], index);
  assert.equal(frame[9], 255 - index);
  assert.equal(frame.readUInt16LE(10), V.crc16(data));
  return { sid: frame.readUInt32LE(0), type: frame[7], data };
}

async function readCapture(path) {
  const wire = fs.readFileSync(path);
  const nativeKey = V.parseKey(F.linkKey);
  const frames = [];
  let next = wire.indexOf('PKT 2\r\n');
  assert(next >= 0);
  while (next < wire.length) {
    const end = wire.indexOf('\r\n', next);
    if (end < 0) break;
    const command = wire.toString('ascii', next, end);
    next = end + 2;
    if (command === 'ECHO REQ') { frames.push(command); continue; }
    assert.match(command, /^PKT \d+$/);
    let remaining = Number(command.slice(4)), index = 0;
    const size = remaining, parts = [];
    while (remaining) {
      const length = Math.min(4096, remaining);
      parts.push(await readFrame(wire.subarray(next, next + 16 + length), nativeKey, index++));
      next += 16 + length;
      remaining -= length;
    }
    assert(parts.every(part => part.type === parts[0].type && part.sid === parts[0].sid));
    frames.push({ size, type: parts[0].type, bytes: Buffer.concat(parts.map(part => part.data)) });
  }
  return frames;
}

async function transmit(ghost, native) {
  const packets = [], status = [];
  let current, sid, session;
  let echoed = 0, retry = false;
  const reply = text => { void sendLine(session, text); };
  session = V.createLabRelease({
    core: C, ghost, key,
    log: (event, details) => status.push([event, details]),
    async send(bytes) {
      if (!current) {
        const line = Buffer.from(bytes).toString('ascii').trim();
        if (line === 'ECHO REQ') { echoed++; reply('ECHO REP'); return; }
        assert.match(line, /^PKT \d+$/);
        current = { size: Number(line.slice(4)), parts: [], received: 0 };
        if (packets.length === 0) { reply('\0ACK'); return; }
      } else {
        const frame = await readFrame(bytes, key, current.parts.length);
        if (!sid) sid = frame.sid;
        assert.equal(frame.sid, sid);
        if (current.size > 2 && current.parts.length === 1 && !retry) {
          retry = true;
          reply('NAK');
          return;
        }
        current.parts.push(frame);
        current.received += frame.data.length;
        if (current.received === current.size) {
          assert(current.parts.every(part => part.type === frame.type));
          packets.push({ size: current.size, type: frame.type, bytes: Buffer.concat(current.parts.map(part => part.data)) });
          current = null;
        }
      }
      reply('ACK');
    },
  });
  await session.start();
  assert.equal(session.state.phase, 'done', JSON.stringify(status));
  const expected = native?.filter(entry => typeof entry !== 'string');
  assert.deepEqual(packets.map(({ size, type }) => [size, type]), expected ?
    expected.map(({ size, type }) => [size, type]) : [[2, 0x36], [0x1b000, 0x26], [2, 0x26]]);
  assert.equal(packets[0].bytes[0], packets[0].bytes[1]);
  assert.deepEqual(packets[1].bytes, Buffer.from(V.buildLabRelease(ghost, C)));
  assert.deepEqual(packets[2].bytes, expected?.[2].bytes ?? Buffer.alloc(2));
  assert.equal(echoed, native ? native.filter(entry => entry === 'ECHO REQ').length : 6);
  assert(retry);
}

async function run() {
  const captured = capture ? await readCapture(capture) : null;
  const file = ghostPath ? fs.readFileSync(ghostPath) : adultGhost();
  assert.deepEqual(V.buildLabRelease(file, C), V.buildLabRelease(file.subarray(0, V.GHOST_SIZE), C));
  if (romPath) {
    const ctx = C.openRom(new Uint8Array(fs.readFileSync(romPath)));
    const adults = V.collectRomVisitors(ctx, C).filter(visitor => visitor.character.stage === 5);
    assert(adults.length);
    const imported = adults.find(visitor => visitor.character.field >= 7);
    for (const adult of [adults[0], imported].filter(Boolean)) {
      const original = Buffer.from(V.buildRomGhost(adult, C));
      const converted = Buffer.from(V.buildLabRelease(original, C));
      assert.equal(converted.length, 0x1b000);
      const parts = buffer => Array.from({ length: 6 }, (_, i) => {
        const start = buffer.readUInt32LE(0x110 + i * 8);
        return C.spriteDecode(buffer.subarray(start, start + buffer.readUInt32LE(0x114 + i * 8)), 0, true);
      });
      const pixels = parts(original);
      for (const [index, part] of parts(converted).entries()) {
        assert.deepEqual(part.cells, pixels[index].cells);
        assert.deepEqual(part.palettes, pixels[index].palettes);
      }
    }
  }
  const child = adultGhost();
  child[0xfc] = 3;
  assert.throws(() => V.buildLabRelease(child, C), /adult/);
  const invalid = adultGhost();
  invalid.writeUInt32LE(0x1d34, 0x118);
  assert.throws(() => V.buildLabRelease(invalid, C), /overlapping/);
  const cancelled = [];
  const stopped = V.createLabRelease({ core: C, key, ghost: file, send: async bytes => cancelled.push(Buffer.from(bytes).toString()) });
  const pending = stopped.start();
  await stopped.stop();
  await pending;
  assert(stopped.state.stopped);
  assert.deepEqual(cancelled, ['CAN\r\n']);
  await transmit(file, captured);
  console.log('PASS: adult ROM and ghost validation, Lab Invite framing, retransmission, and cancellation' +
    (capture ? ', matching the Capybara native UART capture' : ''));
}
run().catch(error => { console.error(error); process.exitCode = 1; });
