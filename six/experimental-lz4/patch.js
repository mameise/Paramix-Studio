// Prototype legacy sprite codec shared by the Studio and its offline CLI.
(function (root) {
'use strict';

const C = root.ParamixCore || require('../../src/core');
const F = root.ParamixFeld || require('../../src/feld');
const Six = root.ParamixSix || require('../../src/six');

const read32 = (b, i) => (b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24) >>> 0;
const put16 = (b, i, n) => { b[i] = n & 255; b[i + 1] = n >>> 8 & 255; };
const put32 = (b, i, n) => { put16(b, i, n); put16(b, i + 2, n >>> 16); };
const same = (a, b) => a.length === b.length && a.every((n, i) => n === b[i]);
const hex = text => Uint8Array.from(text.match(/../g), byte => parseInt(byte, 16));
function join(parts) {
  const out = new Uint8Array(parts.reduce((total, part) => total + part.length, 0));
  let offset = 0;
  for (const part of parts) { out.set(part, offset); offset += part.length; }
  return out;
}

const PROGRAM = 0x108000;
const PAGE = 0x1000;
const VERSION_OFFSET = 0x3C;
const VERSION = Uint8Array.of(80, 76, 90, 50); // PLZ2
const OLDER_ENTRY = 'f0b581b00d46b1f1c04f0446';
const OLDER_DISPATCH = '45f65841c1f600012a780868';
const PROFILES = {
  gen1: { code: '__LEGACY_LZ4_GEN1_B64__', first: 0xB083B5F0, profileId: 1, sites: [
    [0x7B284, OLDER_ENTRY, PROGRAM],
    [0x7B2B4, OLDER_DISPATCH, PROGRAM + 0x40],
  ] },
  jade: { code: '__LEGACY_LZ4_JADE_B64__', first: 0xB083B5F0, profileId: 2, sites: [
    [0x7F11C, OLDER_ENTRY, PROGRAM],
    [0x7F14C, OLDER_DISPATCH, PROGRAM + 0x40],
  ] },
  gen3: { code: '__LEGACY_LZ4_B64__', first: 0x41F0E92D, sites: [
    [0x8C9F4, '2de9f04182b0b1f1c04f0446', PROGRAM],
    [0x8CA22, '45f61852c1f6000210680b78', PROGRAM + 0x40],
  ] },
};
function codecKind(rom) {
  const kind = F.familyOf(C.editionIdOf(rom));
  return PROFILES[kind] ? kind : null;
}

function branch(from, to) {
  const offset = (to - from - 4) >>> 0;
  const s = offset >>> 24 & 1;
  const j1 = 1 ^ s ^ (offset >>> 23 & 1);
  const j2 = 1 ^ s ^ (offset >>> 22 & 1);
  const bytes = new Uint8Array(4);
  put16(bytes, 0, 0xF000 | s << 10 | (offset >>> 12 & 1023));
  put16(bytes, 2, 0x9000 | j1 << 13 | j2 << 11 | (offset >>> 1 & 2047));
  return bytes;
}

function isInstalled(rom) {
  const profile = PROFILES[codecKind(rom)];
  return !!profile && profile.sites.every(([at, , dst]) => same(rom.subarray(at, at + 4), branch(at, dst))) &&
    read32(rom, PROGRAM) === profile.first &&
    (!profile.profileId || read32(rom, PROGRAM + 0x38) === profile.profileId);
}

function checkHelper(helper, profile) {
  const tag = C.LEGACY_LZ4_TAG;
  if (!helper || helper.length < 0x60 || helper.length > PAGE || !profile || read32(helper, 0) !== profile.first ||
      (profile.profileId && read32(helper, 0x38) !== profile.profileId) ||
      !same(helper.subarray(VERSION_OFFSET, VERSION_OFFSET + VERSION.length), VERSION) ||
      !helper.some((_, i) => same(helper.subarray(i, i + tag.length), tag))) {
    throw new Error('expected the compiled LZ4 helper for this firmware');
  }
}

function checkFirmware(rom, helper, stub) {
  const profile = PROFILES[codecKind(rom)];
  if (rom.length !== 0x1000000 || !profile) {
    throw new Error('expected a 16 MiB original Paradise ROM');
  }
  for (const [at, signature] of profile.sites) {
    if (!same(rom.subarray(at, at + signature.length / 2), hex(signature))) {
      throw new Error('native byte-RLE routine does not match the known firmware');
    }
  }
  checkHelper(helper, profile);
  const menu = stub && F.feldFind(rom, stub);
  const reserved = menu ? [...F.slotRanges(menu), [menu.off, menu.off + 0x10000], [menu.stubOff, menu.stubOff + PAGE]] : [];
  if (F.languagePatchPages(rom).includes(PROGRAM) || reserved.some(([a, b]) => PROGRAM < b && PROGRAM + PAGE > a) ||
      !rom.subarray(PROGRAM, PROGRAM + PAGE).every(byte => byte === 255)) {
    throw new Error('decoder page is occupied or reserved');
  }
  return profile;
}

function refresh(rom, helper) {
  checkHelper(helper, PROFILES[codecKind(rom)]);
  if (!isInstalled(rom)) throw new Error('legacy LZ4 hook is missing');
  if (same(rom.subarray(PROGRAM, PROGRAM + helper.length), helper)) return rom;
  if (!rom.subarray(PROGRAM + 0x400, PROGRAM + PAGE).every(byte => byte === 255))
    throw new Error('unexpected data in the legacy LZ4 helper page');
  const out = new Uint8Array(rom);
  out.fill(255, PROGRAM, PROGRAM + PAGE);
  out.set(helper, PROGRAM);
  return out;
}

function embedded(rom) {
  const kind = rom ? codecKind(rom) : 'gen3';
  const code = PROFILES[kind]?.code;
  if (!code) return null;
  if (!code.startsWith('__')) return Uint8Array.from(atob(code), ch => ch.charCodeAt(0));
  const file = kind === 'gen3' ? 'helper' : 'helper-' + kind;
  return typeof require === 'undefined' ? null : require('node:fs').readFileSync(__dirname + '/' + file + '.bin');
}

function wrapSprite(entry, compact = false) {
  const sprite = C.spriteHeader(entry);
  if ((sprite.flags & 0xE8) !== 0xA0 || sprite.bpp > 3) return null;
  const rawSize = Math.ceil(sprite.sw * sprite.sh * (1 << sprite.bpp) / 32) * 4;
  if (!rawSize || rawSize > 0x4000 || sprite.pixOffset + sprite.numSprites * 8 > entry.length) return null;

  const blocks = new Map();
  for (let i = 0; i < sprite.numSprites; i++) {
    const item = sprite.pixOffset + i * 8;
    const pointer = read32(entry, item);
    const length = read32(entry, item + 4);
    const begin = sprite.pixOffset + (pointer & 0x7FFFFFFF);
    if (begin < sprite.pixOffset + sprite.numSprites * 8 || begin + length > entry.length || !length) return null;
    if (!blocks.has(begin)) blocks.set(begin, { pointer, length, items: [] });
    const block = blocks.get(begin);
    if (block.length !== length || block.pointer !== pointer) return null;
    block.items.push(item);
  }

  let copy = new Uint8Array(entry);
  const ordered = [...blocks].sort(([a], [b]) => a - b);
  let count = 0;
  let saved = 0;
  let end = 0;
  for (const [begin, block] of ordered) {
    // Special maps may point outside the sprite or share partial streams.
    if (begin < end) return null;
    end = begin + block.length;
    if ((block.pointer >>> 31) || block.length <= 10 || block.length > 0x4000) continue;
    const rle = entry.subarray(begin, end).map(byte => byte ^ 0x53);
    if (C.LEGACY_LZ4_TAG.every((byte, i) => rle[i] === byte)) throw new Error('input is already wrapped');
    const raw = C.rleDecode(rle, false, rawSize);
    if (raw.length !== rawSize) continue;
    const lz = C.lzEnc(raw, true);
    const length = 10 + lz.length;
    if (length >= block.length) continue;
    const wrapped = new Uint8Array(length);
    wrapped.set(C.LEGACY_LZ4_TAG);
    put16(wrapped, 8, rawSize);
    wrapped.set(lz, 10);
    copy.fill(255, begin + length, end);
    copy.set(wrapped.map(byte => byte ^ 0x53), begin);
    for (const offset of block.items) put32(copy, offset + 4, length);
    count++;
    saved += block.length - length;
  }
  if (!count) return null;

  if (compact) {
    const head = sprite.pixOffset + sprite.numSprites * 8;
    const parts = [copy.slice(0, head)];
    let original = head, next = head;
    const isPadding = (a, b) => b - a <= 3 && entry.subarray(a, b).every(byte => byte === 0 || byte === 255);
    for (const [begin, block] of ordered) {
      if ((block.pointer >>> 31) || !isPadding(original, begin)) break;
      const length = read32(copy, block.items[0] + 4), aligned = (next + 3) & ~3;
      if (aligned !== next) parts.push(new Uint8Array(aligned - next));
      for (const item of block.items) put32(parts[0], item, aligned - sprite.pixOffset);
      parts.push(copy.subarray(begin, begin + length));
      original = begin + block.length;
      next = aligned + length;
    }
    if (original === end && isPadding(end, entry.length) && next < copy.length) copy = join(parts);
  }
  const before = C.spriteDecode(entry, 0, true);
  const after = C.spriteDecode(copy, 0, true);
  if (!before.cells.every((cell, i) => same(cell, after.cells[i]))) {
    throw new Error('wrapped pixels differ from the source');
  }
  return { copy, count, saved };
}

function patch(rom, helper, indices, compact = false, stub) {
  const profile = checkFirmware(rom, helper, stub);
  C.verifyRom(rom, []);
  const archive = C.arc2Parse(rom, C.ASSET_BASE);
  const pack = C.ASSET_BASE + archive.ents[1].offset;
  const packLength = archive.ents[1].ulen;
  const entries = read32(rom, pack) / 4;
  const out = compact ? null : new Uint8Array(rom);
  const changed = [];
  const replacements = new Map();
  for (const index of indices || Array.from({ length: entries }, (_, i) => i)) {
    if (!Number.isInteger(index) || index < 0 || index >= entries) throw new Error('invalid sprite index: ' + index);
    const start = pack + read32(rom, pack + index * 4);
    const end = pack + (index + 1 === entries ? packLength : read32(rom, pack + (index + 1) * 4));
    const result = wrapSprite(rom.subarray(start, end), compact);
    if (!result) continue;
    if (compact) replacements.set(index, result.copy);
    else out.set(result.copy, start);
    changed.push({ sprite: index, blocks: result.count, payloadSavings: result.saved });
  }
  if (!changed.length) throw new Error('none of the selected sprites could be compressed');
  let image = out, reclaimed = 0;
  if (compact) {
    const ctx = C.openRom(new Uint8Array(rom));
    const built = C.buildRom(ctx, replacements);
    image = Six.sixFix(built.rom);
    reclaimed = ctx.assetEnd - built.assetEnd;
  } else {
    put32(image, C.ASSET_BASE + 4, C.arc2Checksum(image, C.ASSET_BASE, archive.length));
  }
  image.set(helper, PROGRAM);
  for (const [site, , destination] of profile.sites) image.set(branch(site, destination), site);
  C.verifyRom(image, changed.map(item => item.sprite));
  return { rom: image, changed, reclaimed, entries };
}

if (typeof module !== 'undefined' && require.main === module) {
  const fs = require('node:fs');
  const stub = fs.readFileSync(require('node:path').join(__dirname, '../../feld/stub_feld_tpl.bin'));
  try {
    const [input, code, output, ...indices] = process.argv.slice(2);
    if (!output || (indices.includes('--all') && indices.length !== 1))
      throw new Error('usage: node patch.js INPUT.bin HELPER.bin OUTPUT.bin [--all | SPRITE_INDEX ...]');
    const all = indices[0] === '--all';
    const rom = fs.readFileSync(input), helper = fs.readFileSync(code);
    if (all && isInstalled(rom)) {
      fs.writeFileSync(output, refresh(rom, helper), { flag: 'wx' });
      console.log(JSON.stringify({ output, helperUpdated: true }));
      return;
    }
    const result = patch(rom, helper, all ? null : indices.length ? [...new Set(indices.map(Number))] : [50], all, stub);
    fs.writeFileSync(output, result.rom, { flag: 'wx' });
    const blocks = result.changed.reduce((count, item) => count + item.blocks, 0);
    const payloadSavings = result.changed.reduce((bytes, item) => bytes + item.payloadSavings, 0);
    console.log(JSON.stringify({ output, totalSprites: result.entries, changedSprites: result.changed.length, blocks, payloadSavings, archiveBytesReclaimed: result.reclaimed }, null, 2));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}

root.ParamixLegacyLz4 = { patch, wrapSprite, embedded, isInstalled, refresh, helperPage: PROGRAM };
if (typeof module !== 'undefined') module.exports = root.ParamixLegacyLz4;
})(typeof window !== 'undefined' ? window : globalThis);
