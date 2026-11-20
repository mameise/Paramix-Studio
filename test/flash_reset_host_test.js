// Host reset interlock: protocol compatibility and complete-operation ordering.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const FL = require('../src/flash.js');
const u32 = n => { const p = new Uint8Array(4); new DataView(p.buffer).setUint32(0, n, true); return p; };
function device(request, capable = true) {
  const d = new FL.Paradise({ request });
  d.info = { resetControl: capable, capacity: FL.FLASH_SIZE, maxBlock: 4096, flashError: 0 };
  return d;
}
async function main() {
  let traffic = [];
  let d = device(async (...args) => { traffic.push(args); return { status: 0 }; }, false);
  assert.equal(await d.resetIdle(true), false);
  assert.equal(await d.resetTask(async () => 42), 42);
  assert.equal(traffic.length, 0, 'old firmware receives no unsupported command');

  for (const cap of [0, 1, 2]) {
    const p = new Uint8Array(44); p.set(u32(FL.FLASH_SIZE), 32); p.set(u32(cap), 40);
    d = device(async () => ({ status: 0, payload: p }));
    d.read = async () => new Uint8Array(4);
    assert.equal((await d.hello()).resetControl, !!(cap & 1));
  }
  let attempts = 0;
  d = device(async (cmd, off, p) => {
    assert.equal(cmd, 16); assert.equal(off, 0); assert.deepEqual(p, u32(0));
    if (++attempts < 3) throw new FL.TimeoutError({ c: 16, a: 0 });
    return { status: 0 };
  });
  assert.equal(await d.resetIdle(false), true);
  assert.equal(attempts, 3, 'idempotent control retries timeout replies');
  attempts = 0;
  d = device(async () => { attempts++; throw Error('disconnected'); });
  await assert.rejects(d.resetIdle(false), /disconnected/);
  assert.equal(attempts, 1);
  d = device(async (cmd, off, p) => { if (p[0]) throw Error('idle transport lost'); return { status: 0 }; });
  assert.equal(await d.resetTask(async () => 7), 7, 'idle cleanup failure preserves a successful result');

  const events = [];
  d = device(async (cmd, off, p) => { events.push(p[0] ? 'idle' : 'lock'); return { status: 0 }; });
  await d.resetTask(async () => {
    events.push('prepare');
    await d.resetTask(async () => { events.push('write'); });
    events.push('postcheck');
  });
  assert.deepEqual(events, ['lock', 'prepare', 'write', 'postcheck', 'idle']);
  events.length = 0;
  const original = Error('verification failed');
  await assert.rejects(d.resetTask(async () => { throw original; }), e => e === original);
  assert.deepEqual(events, ['lock'], 'an error does not rearm reset');
  assert.equal(d.resetDepth, 0);
  events.length = 0;
  await d.resetTask(async () => {
    try { await d.resetTask(async () => { throw original; }); } catch (_) {}
  });
  assert.deepEqual(events, ['lock'], 'a swallowed nested error also leaves reset locked');
  d = device(async () => ({ status: 1 }));
  let ran = false;
  await assert.rejects(d.resetTask(async () => { ran = true; }), /could not lock/);
  assert.equal(ran, false, 'failed lock acknowledgement prevents the job');

  // Model the firmware write latch: later reads/reconnect cannot clear a failed write.
  const writes = []; let latch = false;
  d = device(async (cmd, off, p) => {
    if (cmd === 16) { writes.push(p[0] ? 'idle' : 'lock'); return { status: p[0] && latch ? 1 : 0 }; }
    if (cmd === 9) { latch = true; writes.push('begin'); }
    if (cmd === 12) { latch = false; writes.push('finish'); }
    return { status: 0, payload: new Uint8Array(16) };
  });
  d.writeState = async () => { const p = new Uint8Array(16); p[12] = 1; return { payload: p }; };
  d.putSector = async () => { writes.push('sector'); };
  const map = new Map([[0, new Uint8Array(4096)]]);
  await d.resetTask(() => d.writeSectors(map, null, null, null, async () => { writes.push('verify'); }));
  assert.deepEqual(writes, ['lock', 'begin', 'sector', 'verify', 'finish', 'idle']);
  writes.length = 0;
  const header = new Uint8Array(4096); header.set(Buffer.from('SONIXDEV'));
  d.crc = async () => { writes.push('verify'); return (FL.crc32(header) + 1) >>> 0; };
  await assert.rejects(d.resetTask(() => d.rescueRestore(header)), e => e.code === 'checkFail');
  assert.deepEqual(writes, ['lock', 'begin', 'sector', 'verify']);
  assert.equal(latch, true);
  assert.equal(await d.resetIdle(true), false, 'reconnect idle cannot clear an incomplete-write latch');

  // Exercise the real UI wrapper with connection established inside the operation.
  const src = fs.readFileSync(require.resolve('../src/app.js'), 'utf8');
  const start = src.indexOf('async function devTask(fn) {');
  const end = src.indexOf("$('btnDevice').onclick", start);
  const uiEvents = [], stat = { textContent: 'done' };
  const ctx = { dv: { d: null }, $: () => stat, devRender: () => {}, flMsg: e => e.message, t: k => k };
  vm.createContext(ctx); vm.runInContext(src.slice(start, end), ctx);
  const connected = { resetIdle: async value => { uiEvents.push(value ? 'idle' : 'lock'); } };
  await ctx.devTask(async () => { ctx.dv.d = connected; uiEvents.push('hello', 'rescue check'); });
  assert.deepEqual(uiEvents, ['hello', 'rescue check', 'idle']);
  ctx.dv.d = null; uiEvents.length = 0;
  await ctx.devTask(async () => { ctx.dv.d = connected; throw original; });
  assert.deepEqual(uiEvents, [], 'failed connect checks never arm reset');
  assert.equal(ctx.dv.run, false);
  ctx.dv.d = device(async (cmd, off, p) => { uiEvents.push(p[0] ? 'idle' : 'lock'); return { status: 0 }; });
  await ctx.devTask(async () => { uiEvents.push('write', 'postcheck'); });
  assert.deepEqual(uiEvents, ['lock', 'write', 'postcheck', 'idle']);
  uiEvents.length = 0;
  await ctx.devTask(async () => { throw original; });
  assert.deepEqual(uiEvents, ['lock']);
  assert.equal(ctx.dv.run, false);
  console.log('ok   reset protocol, old firmware, nested operation safety, verification ordering and connection cleanup');
}
main().catch(e => { console.error(e); process.exitCode = 1; });
