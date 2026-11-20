/*
 * Paramix Studio: save games in and out of the Paradise.
 *
 * Paradise: the game keeps its progress in the flash after the edition ID page
 * (0xD4A000, Tropical/Glacier 0xD2A000, Neon Planets 0xD86000) up to 0xF00000. Exported as a
 * .tamasave (format 3 of the TamaParadise app): every page of that region,
 * the seconds counter of the device and an armed wake alarm, so the app
 * continues instead of starting like a fresh battery. Behind the pages a
 * Paramix trailer ("PMXE", edition, region) that the app ignores and the
 * Studio uses to refuse a save of another edition.
 *
 * TamaEmu: the guest's own save is its flash. Exported as the raw image the
 * TamaEmu app uses (.sav, 4 MB or 8 MB; named after the app's device: plus-color, idl, ps, 4u): packed blocks plus the spare pages
 * the emulator wrote. Importing a .sav works like choosing a firmware.
 *
 * Author: mameise
 */
(function (root) {
'use strict';

const PAGE = 0x1000;
const MAGIC = 'TAMASAVE', VERSION = 3, TRAILER = 0x45584D50;   // "PMXE"
const SET_A = 0xEFE000, SET_B = 0xEFF000, REGION_END = 0xF00000;
const NEON_SETTINGS = [0xD86000, 0xD87000, 0xD88000, 0xD89000, 0xD8A000];
const NEON_GENERATION = 0xAD0;
const NEON_POINTS = 0xC0;
const NEON_LEVEL = 0x88;
const NEON_MISSION_FLAGS = [0xF8, 0xFC, 0x100];
// [save offset, target, optional bitmap byte count]; mission 13 is a u32 counter.
const NEON_MISSIONS = [
  [0xC4, 500], [0xC6, 500], [0xC8, 100], [0xCA, 100], [0xCC, 100], [0xCE, 1000],
  [0xD0, 10], [0xD2, 7, 1], [0xD4, 50], [0xD6, 50], [0xD8, 100], [0xDA, 50],
  [0xDC, 100], [0xE0, 99999], [0xE4, 50], [0xE6, 50], [0xE8, 18, 3],
  [0xEB, 12, 2], [0xED, 16, 2], [0xEF, 16, 2], [0xF1, 16, 2], [0xF3, 16, 2],
];
const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }

/** Edition 5/6 (Tropical, Glacier) keep their ID page lower. */
function idPage(edition) { return edition === 8 ? 0xD85000 : edition >= 5 && edition <= 7 ? 0xD29000 : 0xD49000; }
function region(edition) { return [idPage(edition) + PAGE, REGION_END]; }
function paraClear(rom, edition) {
  if (rom.length !== 0x1000000 || !Number.isInteger(edition) || edition < 1 || edition > 8)
    throw new Error('expected a Paradise ROM with a known save layout');
  const [start, end] = region(edition);
  return new Uint8Array(rom).fill(0xFF, start, end);
}
function family(edition) { return edition === 8 ? 'neon' : edition >= 5 && edition <= 7 ? 'gen3' : edition === 4 ? 'jade' : 'gen1'; }

/* Settings block: CRC-16/ARC over bytes 4..4095 at 0, its inverse at 2. */
function crc16arc(d, from, to) {
  let c = 0;
  for (let i = from; i < to; i++) { c ^= d[i]; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1; }
  return c;
}
function settingsOk(p) { const c = crc16arc(p, 4, PAGE); return u16(p, 0) === c && u16(p, 2) === (~c & 0xFFFF); }

function sealSettings(page) {
  const crc = crc16arc(page, 4, PAGE);
  w32(page, 0, crc | ((~crc & 0xFFFF) << 16));
}

/** The native boot scan picks the highest unsigned generation, with later pages winning ties. */
function neonSettings(pages) {
  let active = null;
  for (const [index, address] of NEON_SETTINGS.entries()) {
    const page = pages.get(address);
    if (!page || page.length !== PAGE || !settingsOk(page)) continue;
    const generation = u32(page, NEON_GENERATION);
    if (!active || generation >= active.generation) active = { index, page, generation };
  }
  return active;
}

// ---------------------------------------------------------------- Paradise
/** Pages of the save region from a 16 MB image (all of them, empty ones too). */
function paraFromImage(img, edition) {
  const [a, b] = region(edition), pages = new Map();
  for (let p = a; p < b; p += PAGE) pages.set(p, img.slice(p, p + PAGE));
  return pages;
}
/**
 * .tamasave from pages (Map address -> 4 KB). clock: { counter, ctrl } of the device or null.
 */
function paraEncode(pages, edition, clock, now = Math.floor(Date.now() / 1000)) {
  const counter = clock ? clock.counter >>> 0 : 0;
  const alarm = counter ? counter - 1 : 0, status = counter ? 0x100 : 0;   // armed, as the app does when it puts the console away
  // the always powered system registers as the app stores them (clock and PLL as the firmware sets them)
  const regs = counter ? [[0x000, 0xC051], [0x004, 0xA55A0000], [0x00C, 4], [0x010, 0], [0x018, 0x21], [0x020, 0x42], [0x050, 0],
    [0x100, 0x157EB7], [0x108, 0], [0x10C, 0x1FFFFD], [0x110, 0], [0x200, clock.ctrl & 0x80 ? clock.ctrl >>> 0 : 0xE0], [0x204, 0],
    [0x210, 0], [0x214, 0], [0x218, 0], [0x230, alarm], [0x234, status], [0x300, 0x10]] : [];
  const list = [...pages.keys()].sort((x, y) => x - y);
  const size = 36 + 8 * regs.length + 4 + list.reduce((n, a) => n + 8 + pages.get(a).length, 0) + 16;
  const out = new Uint8Array(size);
  for (let i = 0; i < 8; i++) out[i] = MAGIC.charCodeAt(i);
  w32(out, 8, VERSION);
  w32(out, 12, counter ? now : 0); w32(out, 16, 0);              // u64 time stamp (seconds since 1970)
  w32(out, 20, counter); w32(out, 24, alarm); w32(out, 28, status); w32(out, 32, regs.length);
  let o = 36;
  for (const [k, v] of regs) { w32(out, o, k); w32(out, o + 4, v); o += 8; }
  w32(out, o, list.length); o += 4;
  for (const a of list) { const d = pages.get(a); w32(out, o, a / PAGE); w32(out, o + 4, d.length); out.set(d, o + 8); o += 8 + d.length; }
  w32(out, o, TRAILER); w32(out, o + 4, edition); w32(out, o + 8, region(edition)[0]); w32(out, o + 12, region(edition)[1]);
  return out;
}
/** Write a decoded save back as it was (time stamp, clock, registers kept). */
function paraWrite(save, edition) {
  const regs = save.regs || [], list = [...save.pages.keys()].sort((x, y) => x - y);
  const size = 36 + 8 * regs.length + 4 + list.reduce((n, a) => n + 8 + save.pages.get(a).length, 0) + 16;
  const out = new Uint8Array(size);
  for (let i = 0; i < 8; i++) out[i] = MAGIC.charCodeAt(i);
  w32(out, 8, VERSION);
  w32(out, 12, save.stamp % 4294967296); w32(out, 16, Math.floor(save.stamp / 4294967296));
  w32(out, 20, save.counter); w32(out, 24, save.alarm); w32(out, 28, save.status); w32(out, 32, regs.length);
  let o = 36;
  for (const [k, v] of regs) { w32(out, o, k); w32(out, o + 4, v); o += 8; }
  w32(out, o, list.length); o += 4;
  for (const a of list) { const d = save.pages.get(a); w32(out, o, a / PAGE); w32(out, o + 4, d.length); out.set(d, o + 8); o += 8 + d.length; }
  const ed = edition || save.edition;
  w32(out, o, TRAILER); w32(out, o + 4, ed); w32(out, o + 8, region(ed)[0]); w32(out, o + 12, region(ed)[1]);
  return out;
}
/** Read a .tamasave (format 3). Throws on anything else. */
function paraDecode(b) {
  if (b.length < 40 || String.fromCharCode(...b.subarray(0, 8)) !== MAGIC) throw new Error('not a Paradise save (.tamasave)');
  if (u32(b, 8) !== VERSION) throw new Error('unknown save version ' + u32(b, 8));
  const stamp = u32(b, 12) + u32(b, 16) * 4294967296, counter = u32(b, 20), alarm = u32(b, 24), status = u32(b, 28);
  const nr = u32(b, 32); let o = 36;
  const regs = [];
  for (let i = 0; i < nr; i++) { if (o + 8 > b.length) throw new Error('save is cut off'); regs.push([u32(b, o), u32(b, o + 4)]); o += 8; }
  if (o + 4 > b.length) throw new Error('save is cut off');
  const np = u32(b, o); o += 4;
  const pages = new Map();
  for (let i = 0; i < np; i++) {
    if (o + 8 > b.length) throw new Error('save is cut off');
    const a = u32(b, o) * PAGE, n = u32(b, o + 4); o += 8;
    if (o + n > b.length) throw new Error('save is cut off');
    pages.set(a, b.slice(o, o + n)); o += n;
  }
  let edition = 0;
  if (o + 16 <= b.length && u32(b, o) === TRAILER) {
    edition = u32(b, o + 4);
    const start = u32(b, o + 8), end = u32(b, o + 12);
    // The fork formerly exported Neon as 7. Its recorded region distinguishes
    // these backups from 1.7.6 Tropical/Glacier backups using that number.
    if (edition === 7 && start === region(8)[0]) edition = 8;
    if (edition && (edition > 8 || start !== region(edition)[0] || end !== REGION_END))
      throw new Error('unknown Paradise save layout');
  }
  return { stamp, counter, alarm, status, regs, pages, edition };
}
/**
 * Can this save go onto a device of this edition? Returns { errors, warnings, pages (only the
 * save region, full 4 KB), complete }.
 */
function paraCheck(save, edition) {
  const errors = [], warnings = [], [a, b] = region(edition), pages = new Map();
  // 1.7.6 SAVE_FAMILY: the field switch changes the ID (Land/Water/Sky, Tropical/Glacier and their Forest/Sky): a save
  // of the same firmware fits, before a save from Tropical was refused on the device once it had switched to Glacier
  if (save.edition && family(save.edition) !== family(edition)) errors.push({ code: 'saveOtherEd', p: { s: save.edition, d: edition } });
  let outside = 0;
  for (const [p, d] of save.pages) {
    if (p < a || p >= b || d.length !== PAGE) { outside++; continue; }
    pages.set(p, d);
  }
  if (outside) warnings.push({ code: 'saveOutside', p: { n: outside } });
  const neon = edition === 8, settings = neon ? NEON_SETTINGS : [SET_A, SET_B];
  const set = neon ? neonSettings(pages)?.page : settings.map(a => pages.get(a)).find(p => p && settingsOk(p));
  // Restoring only some ring pages can leave a newer copy of the previous
  // settings on the device. A snapshot must include the whole ring.
  if (!set || (neon && !settings.every(a => pages.has(a)))) errors.push({ code: 'saveNoSettings' });
  else if (!save.edition && !neon) {
    // The word at +0x0C advances with the clock. Prefer the start of a whole
    // backup, with the old heuristic only for partial saves without metadata.
    const [gen3Start] = region(5), [olderStart] = region(1);
    const first = [...save.pages.keys()].reduce((min, at) => Math.min(min, at), Infinity);
    const gen3 = first === gen3Start || (first !== olderStart && u16(set, 12) === 0x016D);
    if (first === region(8)[0] || gen3 !== (family(edition) === 'gen3'))
      errors.push({ code: 'saveFamily' });
  }
  let complete = true;
  for (let p = a; p < b; p += PAGE) if (!pages.has(p)) { complete = false; break; }
  if (!complete) warnings.push({ code: 'saveIncomplete' });
  return { errors, warnings, pages, complete };
}
/** Seconds counter to set on the device: the saved one plus the time since. */
function paraClock(save, now = Math.floor(Date.now() / 1000)) {
  if (!save.counter || !save.stamp) return null;
  return (save.counter + Math.max(0, now - save.stamp)) >>> 0;
}

function neonProgress(save) {
  if (save.edition && save.edition !== 8) return null;
  if (!NEON_SETTINGS.every(address => save.pages.get(address)?.length === PAGE)) return null;
  const current = neonSettings(save.pages);
  if (!current) return null;
  const page = current.page;
  const flags = u32(page, NEON_MISSION_FLAGS[0]);
  const id = u16(page, 0x118), stage = (page[0x12C] >> 2) & 7;
  return {
    points: u32(page, NEON_POINTS),
    level: page[NEON_LEVEL] & 15,
    id, stage, canCare: id >= 1001 && id <= 4999 && stage >= 2 && stage <= 5,
    hunger: page[0x130], happy: page[0x131], sick: page[0x124],
    missions: NEON_MISSIONS.map(([, target], index) => ({
      target, count: neonMissionCount(page, index), complete: !!(flags & (1 << index)),
    })),
  };
}

function neonSaveLayout(rom) {
  const getter = [0x40,0xF2,0x94,0x20,0xC1,0xF6,0x00,0x00,0xD0,0xF8,0xBC,0x00,0x70,0x47];
  return rom.length === 0x1000000 && getter.every((byte, index) => rom[0x229C0 + index] === byte)
    && NEON_MISSIONS.every(([, target], index) => u32(rom, 0xA6D24 + index * 4) === target);
}

function neonMissionCount(page, index) {
  const [offset, , bytes] = NEON_MISSIONS[index];
  if (!bytes) return index === 13 ? u32(page, offset) : u16(page, offset);
  let count = 0;
  for (let i = 0; i < bytes; i++) {
    for (let value = page[offset + i]; value; value &= value - 1) count++;
  }
  return count;
}

function neonMissionSet(page, mission) {
  const { index, complete } = mission;
  if (!Number.isInteger(index) || !NEON_MISSIONS[index] || typeof complete !== 'boolean') throw new Error('invalid Neon mission');
  const mask = 1 << index;
  // Preserve the game's unclaimed reward for a mission that was already completed.
  if (complete && (u32(page, NEON_MISSION_FLAGS[0]) & mask)) return;
  const [offset, target, bytes] = NEON_MISSIONS[index];
  const current = neonMissionCount(page, index);
  const next = complete ? Math.max(current, target) : Math.min(current, target - 1);
  if (bytes) {
    let count = current;
    for (let i = 0; i < bytes * 8 && count !== next; i++) {
      const bit = complete ? i : bytes * 8 - 1 - i;
      const at = offset + (bit >> 3), flag = 1 << (bit & 7);
      if (complete && !(page[at] & flag)) { page[at] |= flag; count++; }
      if (!complete && (page[at] & flag)) { page[at] &= ~flag; count--; }
    }
  } else if (index === 13) {
    w32(page, offset, next);
  } else {
    page[offset] = next & 255;
    page[offset + 1] = next >>> 8;
  }
  for (const at of NEON_MISSION_FLAGS) {
    const flags = u32(page, at);
    w32(page, at, complete ? flags | mask : flags & ~mask);
  }
}

function neonProgressSet(save, changes) {
  const progress = neonProgress(save);
  if (!progress) throw new Error('the Neon settings ring is missing or damaged');
  const current = neonSettings(save.pages);
  const page = new Uint8Array(current.page), pages = new Map(save.pages);
  if (changes.id) changePetIdentity(save, pages, page, PET.neon.tama, NEON_SETTINGS, changes);
  if (changes.points !== undefined) {
    const points = changes.points;
    if (!Number.isInteger(points) || points < 0 || points > 99999) throw new Error('Tamapoints must be between 0 and 99999');
    w32(page, NEON_POINTS, points);
  }
  if (changes.level !== undefined) {
    const level = changes.level;
    if (!Number.isInteger(level) || level < 0 || level > 10) throw new Error('Galaxy level must be between 0 and 10');
    if (level !== (page[NEON_LEVEL] & 15)) page[NEON_LEVEL] = level * 0x11;
  }
  if (changes.mission) neonMissionSet(page, changes.mission);
  if (changes.care) {
    if (!progress.canCare) throw new Error('the save has no active Tama to care for');
    page[0x130] = 6;
    page[0x131] = 20;
    page[0x124] = 0;
    page[0x128] &= ~0x10;
  }
  if (page.every((byte, index) => byte === current.page[index])) return save;
  if (current.generation === 0xFFFFFFFF) throw new Error('the Neon settings generation cannot be advanced');
  w32(page, NEON_GENERATION, current.generation + 1);
  sealSettings(page);
  pages.set(NEON_SETTINGS[(current.index + 1) % NEON_SETTINGS.length], page);
  return { ...save, edition: 8, pages };
}

// ---------------------------------------------------------------- change a Paradise save (1.6.8)
// The pet in the settings block: +4/+6 character, +0x10 sickness, +0x18 bits 2..4 stage, +0x1C hunger 0..6,
// +0x1D happiness 0..20. Planet level: low nibble = level, high nibble = level already celebrated.
const PET = { gen1: { tama: 0x104, level: 0x7C }, jade: { tama: 0x108, level: 0x7C }, gen3: { tama: 0x114, level: 0x88 }, neon: { tama: 0x114, level: 0x88 } };
function paraSettings(save, edition) {
  if (family(edition) === 'neon') return neonSettings(save.pages)?.page || null;
  for (const a of [SET_A, SET_B]) {
    const p = save.pages.get(a);
    if (p && p.length === PAGE && settingsOk(p)) return p;
  }
  return null;
}
/** What the save says about the pet: { level, id, stage, hunger, happy, sick } or null. */
function paraPet(save, edition) {
  const p = paraSettings(save, edition), o = PET[family(edition)]; if (!p || !o) return null;
  const t = o.tama;
  return { level: p[o.level] & 15, id: u16(p, t + 4), stage: (p[t + 0x18] >> 2) & 7, hunger: p[t + 0x1C], happy: p[t + 0x1D], sick: p[t + 0x10] };
}
/**
 * Changed copy of the save. ch: { level, id, stage, care } (each optional). A new character goes
 * into the settings block and into the pet's own file (pages with the old character in the header).
 */
const TAMA_ANY = true;
// Update the settings and the active Tama's own file together.
function changePetIdentity(save, pages, p, t, sets, ch) {
  const old = u16(p, t + 4);
  p[t + 4] = ch.id & 255; p[t + 5] = ch.id >> 8; p[t + 6] = ch.id & 255; p[t + 7] = ch.id >> 8;
  if (ch.stage) p[t + 0x18] = (p[t + 0x18] & ~0x1C) | ((ch.stage & 7) << 2);
  let found = 0;
  if (old && old !== ch.id) for (const [a, d] of save.pages) {
    if (sets.includes(a) || d.length !== PAGE) continue;
    // 1.7.2 TAMA_ANY: a bred Tama's file was not found (GitHub): small number at +8, character at +0x0C or +0x0E
    if (TAMA_ANY ? (u32(d, 8) === 0 || u32(d, 8) > 255 || (u16(d, 14) !== old && u16(d, 12) !== old)) : (u32(d, 8) !== 1 || u16(d, 14) !== old)) continue;
    const n = d.slice(); found++;
    if (!TAMA_ANY || u16(n, 14) === old) { n[14] = ch.id & 255; n[15] = ch.id >> 8; }
    if (u16(n, 12) === old) { n[12] = ch.id & 255; n[13] = ch.id >> 8; }
    if (u16(n, 0x10A) === old) { n[0x10A] = ch.id & 255; n[0x10B] = ch.id >> 8; }
    pages.set(a, n);
  }
  if (TAMA_ANY && old && old !== ch.id && !found) throw new Error('the Tama\'s own file is not in the save, the Tama would not change');
}

function paraPetSet(save, edition, ch) {
  const neon = family(edition) === 'neon', ring = neon ? neonSettings(save.pages) : null;
  const p0 = neon ? ring?.page : paraSettings(save, edition), o = PET[family(edition)];
  const sets = neon ? NEON_SETTINGS : [SET_A, SET_B];
  if (!p0 || !o) throw new Error('the save has no settings block');
  const t = o.tama, p = p0.slice(), pages = new Map(save.pages);
  if (ch.level !== undefined) { const v = Math.max(0, Math.min(10, ch.level | 0)), now = p[o.level] & 15; p[o.level] = v > now ? (p[o.level] & 0xF0) | v : (v << 4) | v; }
  if (ch.id) changePetIdentity(save, pages, p, t, sets, ch);
  if (ch.care) { p[t + 0x1C] = 6; p[t + 0x1D] = 20; p[t + 0x10] = 0; }
  if (neon) {
    if (ring.generation === 0xFFFFFFFF) throw new Error('Neon settings generation exhausted');
    w32(p, NEON_GENERATION, ring.generation + 1);
    sealSettings(p);
    pages.set(NEON_SETTINGS[(ring.index + 1) % NEON_SETTINGS.length], p);
  } else {
    sealSettings(p);
    pages.set(SET_A, p); pages.set(SET_B, p.slice());
  }
  return Object.assign({}, save, { pages });
}

// ---------------------------------------------------------------- TamaEmu
const APP_CFG = 0xA0140, PMXC = 0x43584D50, PG_MAGIC = 0x50584D50, PG_MAX = 64;
const KINDS = { 0: { dev: 'pc', size: 0x400000, app: 'plus-color' }, 1: { dev: 'idl', size: 0x800000, app: 'idl' },
  2: { dev: 'ps', size: 0x800000, app: 'ps' }, 3: { dev: '4u', size: 0x800000, app: '4u' }, 4: { dev: '4u-plain', size: 0x800000, app: '4u-plain' } };
/** Settings block of the emulator in an image (or the 4 KB sector at 0xA0000). */
function emuCfg(sec, base = 0xA0000) {
  const c = APP_CFG - base;
  if (u32(sec, c) !== PMXC) return null;
  const v = u32(sec, c + 4), kind = u32(sec, c + 20);
  if (!KINDS[kind]) return null;
  return { version: v, guestOff: u32(sec, c + 8), guestSize: u32(sec, c + 12), saveOff: u32(sec, c + 16), kind, map: sec.slice(c + 24, c + 152), spareEnd: v >= 3 ? u32(sec, c + 152) : 0, lzOff: v >= 5 ? u32(sec, c + 160) : 0, ...KINDS[kind] };
}
/**
 * Guest flash (the .sav of the TamaEmu app) from the packed firmware and the spare pages.
 * read(addr, len) gives bytes of the Paradise flash (sync or async).
 */
function lzDec(s, n) {   // LZ4 block (as emu.js)
  const d = new Uint8Array(n).fill(0xFF); let ip = 0, op = 0;
  for (;;) {
    const t = s[ip++]; let l = t >> 4;
    if (l === 15) { let x; do { x = s[ip++]; l += x; } while (x === 255); }
    while (l-- > 0 && op < n) d[op++] = s[ip++];
    if (op >= n || ip >= s.length) return d;
    const o = s[ip] | (s[ip + 1] << 8); ip += 2; let m = (t & 15) + 4;
    if ((t & 15) === 15) { let x; do { x = s[ip++]; m += x; } while (x === 255); }
    while (m-- > 0 && op < n) { d[op] = d[op - o]; op++; }
  }
}
/* 1.7.3 test21 LZ_GONE: TamaEmu notes behind its page table which packed blocks the pet erased (four words, then
 * their xor with 0x454E4F47). Those blocks read empty, apart from the parts in spare pages (written after this). */
function lzGoneApply(t, o, g, size) {
  if (t.length < o + 20) return;
  let x = 0x454E4F47; for (let i = 0; i < 4; i++) x = (x ^ u32(t, o + 4 * i)) >>> 0;
  if (x !== u32(t, o + 16)) return;
  for (let b = 0; b < 128 && (b << 16) < size; b++) if ((u32(t, o + 4 * (b >> 5)) >>> (b & 31)) & 1) g.fill(0xFF, b << 16, (b + 1) << 16);
}
async function emuGuestAsync(read, cfg, progress) {
  const out = new Uint8Array(cfg.size).fill(0xFF), nb = cfg.size >> 16;
  let done = 0; const todo = [...cfg.map.subarray(0, nb)].filter((k) => k !== 0xFF).length;
  for (let b = 0; b < nb; b++) {
    let k = cfg.map[b];
    if (k === 0xFF) continue;
    if (k === 0xFE) k = b;
    out.set(new Uint8Array(await read(cfg.guestOff + (k << 16), 0x10000)), b << 16);
    if (progress) progress('read', ++done, todo);
  }
  // 1.7.3 GUEST_LZ: the packed cold blocks (LZ4 parts of 1 KB, see emu.js lzPack)
  if (cfg.lzOff) {
    const t = new Uint8Array(await read(cfg.lzOff, 512));
    for (let b = 0; b < nb; b++) {
      const lp = u32(t, 4 * b); if (!lp || cfg.map[b] !== 0xFF) continue;
      const l = new Uint8Array(await read(cfg.lzOff + lp, 65 * 4)), a0 = u32(l, 0), z0 = u32(l, 256);
      const raw = z0 > a0 ? new Uint8Array(await read(cfg.lzOff + a0, z0 - a0)) : new Uint8Array(0);
      for (let k = 0; k < 64; k++) {
        const a = u32(l, 4 * k) - a0, z = u32(l, 4 * k + 4) - a0, at = (b << 16) + k * 0x400;
        if (z === a) continue;
        out.set(z - a >= 0x400 ? raw.subarray(a, a + 0x400) : lzDec(raw.subarray(a, z), 0x400), at);
      }
    }
  }
  // spare pages (8 KB parts the guest wrote into blocks that are empty in the firmware)
  if (cfg.spareEnd) {
    const lo = (cfg.guestOff + cfg.guestSize + 0x1FFF) & ~0x1FFF, hi = cfg.spareEnd & ~0x1FFF;
    if (hi >= lo + 0x4000) {
      const data = lo + 0x2000; let n = (hi - data) >> 13; if (n > PG_MAX) n = PG_MAX;
      const t = new Uint8Array(await read(lo, 16 + 2 * PG_MAX + 20));
      let x = (PG_MAGIC ^ Math.imul(n, 0x9E3779B1)) >>> 0;
      for (let i = 0; i < PG_MAX; i++) x = (((x << 5) | (x >>> 27)) ^ u16(t, 16 + 2 * i)) >>> 0;
      if (u32(t, 0) === PG_MAGIC && u32(t, 4) === n && u32(t, 8) === x) {
        lzGoneApply(t, 16 + 2 * PG_MAX, out, cfg.size);   // 1.7.3 test21 LZ_GONE: packed blocks the pet erased
        for (let i = 0; i < n; i++) {
          const v = u16(t, 16 + 2 * i);
          if (v === 0xFFFF || (v << 13) >= cfg.size) continue;
          if (cfg.map[(v << 13) >> 16] !== 0xFF) continue;          // only blocks that are empty in the firmware
          out.set(new Uint8Array(await read(data + (i << 13), 0x2000)), v << 13);
        }
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------- T7Emu
/*
 * The pet's EEPROM (4 KB) in the ROM: two slots of 8 KB from 0x0C1000 (header sector:
 * "T7EE", sequence, page mask, checksum; then the pages). The T7Emu app keeps it as
 * save.bin: page mask (2 bytes) and the 16 pages, 4098 bytes. Same content, so it can go back and forth.
 */
const T7_EE = 0xC1000, T7_EE_MAGIC = 0x45453754, T7_SAVE_LEN = 4098;
function t7Sum(b, o) { let s = 0x7E7E; for (let i = 0; i < 4096; i++) s = (Math.imul(s, 31) + b[o + i]) >>> 0; return s; }
// test27: up to test26 the T7Emu could write its pages 2 bytes off (the flash DMA takes words, its buffer lay at ...2):
// 2 other bytes first, the pages without their last 2. The header sum is over the right pages, so such a slot is found.
function t7Sum2(b, o) { let s = 0x7E7E; for (let i = 2; i < 4096; i++) s = (Math.imul(s, 31) + b[o + i]) >>> 0; return Math.imul(Math.imul(s, 31) >>> 0, 31) >>> 0; }
function t7Slot(img, at) {
  if (u32(img, at) !== T7_EE_MAGIC) return null;
  const sum = u32(img, at + 12), sh = sum === t7Sum(img, at + 0x1000) ? 0 : sum === t7Sum2(img, at + 0x1000) ? 2 : -1;
  if (sh < 0) return null;
  return { seq: u32(img, at + 4), have: u32(img, at + 8) & 0xFFFF, at, sh };
}
function t7Newest(img, ee = T7_EE) { const a = t7Slot(img, ee), b = t7Slot(img, ee + 0x2000); if (a && b) return ((a.seq - b.seq) | 0) > 0 ? a : b; return a || b; }
/** save.bin of the T7Emu app from a 16 MB image (null: the pet has no save yet). base: save area of
 * the T7Emu (0x0C0000, a further one from 1.6.0 its own). */
function t7SaveOut(img, base = 0xC0000) {
  const s = t7Newest(img, base + 0x1000); if (!s) return null;
  const out = new Uint8Array(T7_SAVE_LEN); out[0] = s.have & 255; out[1] = s.have >> 8;
  out.set(img.subarray(s.at + 0x1000 + s.sh, s.at + 0x2000), 2);   // shifted slot: the last 2 bytes stay 0
  return out;
}
function t7SaveCheck(b) { return b && b.length === T7_SAVE_LEN; }
/** save.bin into the image: into the older slot, as the device does. Returns a new image. */
function t7SaveIn(img, save, base = 0xC0000) {
  if (!t7SaveCheck(save)) throw new Error('not a T7Emu save (save.bin, 4098 bytes)');
  const ee = base + 0x1000, out = new Uint8Array(img), cur = t7Newest(img, ee);
  const at = cur && cur.at === ee ? ee + 0x2000 : ee;
  out.fill(0xFF, at, at + 0x2000);
  out.set(save.subarray(2), at + 0x1000);
  w32(out, at, T7_EE_MAGIC); w32(out, at + 4, cur ? (cur.seq + 1) >>> 0 : 1); w32(out, at + 8, save[0] | (save[1] << 8)); w32(out, at + 12, t7Sum(out, at + 0x1000));
  out.fill(0xFF, base, base + 0x1000);          // the running session belongs to the old pet
  return out;
}

root.ParamixSaves = { paraPet, paraPetSet, neonSaveLayout, neonSettings, neonProgress, neonProgressSet, paraClear, paraWrite, t7SaveOut, t7SaveIn, t7SaveCheck, T7_EE, idPage, region, family, settingsOk, crc16arc, paraFromImage, paraEncode, paraDecode, paraCheck, paraClock, emuCfg, emuGuestAsync, lzGoneApply, KINDS };
if (typeof module !== 'undefined') module.exports = root.ParamixSaves;
})(typeof window !== 'undefined' ? window : globalThis);
