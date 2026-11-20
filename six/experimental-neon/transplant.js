// Import Neon characters and items into the supported Paradise archives.
(function (root) {
'use strict';

const C = root.ParamixCore || require('../../src/core');
const F = root.ParamixFeld || require('../../src/feld');
const Six = root.ParamixSix || require('../../src/six');
const Lz4 = root.ParamixLegacyLz4 || require('../experimental-lz4/patch');

const RECORDS = 11, SPRITE_FLAGS = 12, LAYERS = 15, LAYER_OFFSETS = 16, FRAME_SOUNDS = 17, FRAMES = 18;
const RECORD_SIZE = 30, FRAMES_PER_ANIMATION = 54;
const ITEMS = 10, ITEM_SIZE = 46, SCRIPT_OFFSETS = 13, SCRIPTS = 14;
const ITEM_COUNT = 59, ITEM_STATE = 0x107FD0, ITEM_MAGIC = 0x31495A4E; // NZI1
const ITEM_PLACEHOLDERS = [5086, 6052];
const BASE_ITEM_COUNTS = [0, 246, 246, 246, 274, 343, 343];
// Neon additions relative to Tropical/Glacier. Older editions lack some of the
// original Tropical/Glacier items too, so absence alone cannot select the donor rows.
const NEON_ITEM_IDS = [
  5086, 5087, 5088, 5089, 5090, 5091, 5092, 5093, 5094, 5095, 5106, 5107, 5108,
  5109, 5110, 5111, 5112, 5113, 5114, 5115, 5117, 6052, 6054, 7088, 7089, 7090,
  7091, 7092, 7093, 7094, 7095, 7096, 7097, 7098, 7099, 7100, 7102, 8109, 8110,
  8111, 8112, 8113, 8114, 8115, 8116, 8117, 8118, 8119, 8120, 8121, 8122, 8123,
  8124, 8126, 8127, 8128, 8129, 8130, 8131,
];
const PLANET_TEXTS = [14, 15, 16];
const expectedFields = [24, 38, 24];
const GROWTH_PAGE = 0x107000, GROWTH_MAGIC = 0x5747524E;
const GROWTH_PROFILES = {
  gen1: { code: '__NEON_GROWTH_GEN1_B64__', hook: 0x1F594, stack: 0x90, layers: [0x2FF2A, 0x2FF48] },
  jade: { code: '__NEON_GROWTH_JADE_B64__', hook: 0x1F6C8, stack: 0xC0, layers: [0x3126E, 0x3128C] },
  gen3: { code: '__NEON_GROWTH_B64__', hook: 0x2114E, stack: 0xC8, layers: [0x34B22, 0x34B40] },
};
function growthKind(rom) {
  const kind = F.familyOf(C.editionIdOf(rom));
  return GROWTH_PROFILES[kind] ? kind : null;
}
// Shared with zh patcher. The donor names still contain the stock Chinese codes.
const NAME_STATE = GROWTH_PAGE + 0xFE0, NAME_MAGIC = 0x314E5A4E; // NZN1
const FIELD_STATE = NAME_STATE + 16, FIELD_MAGIC = 0x31465A4E; // NZF1
const GROWTH_VERSION = 2;
const NATIVE_TEENS = 4, NATIVE_ADULTS = 16, MAX_ADULTS = 33;
const GROWTH_CONFIG_SIZE = (3 + 3 * NATIVE_TEENS + 3 * NATIVE_ADULTS + 3 + 3 * MAX_ADULTS) * 2;
const LAYER_INDEX_CODE = [0x00, 0x6C, 0x07, 0x9B, 0xB3, 0xF9, 0x00, 0x30, 0xBD, 0xF9, 0x1A, 0xC0];

const u16 = (b, o) => b[o] | b[o + 1] << 8;
const u32 = (b, o) => (u16(b, o) | u16(b, o + 2) << 16) >>> 0;
function w16(b, o, v) { b[o] = v; b[o + 1] = v >>> 8; }
function w32(b, o, v) { w16(b, o, v); w16(b, o + 2, v >>> 16); }
function word(v) { const b = new Uint8Array(2); w16(b, 0, v); return b; }
function dword(v) { const b = new Uint8Array(4); w32(b, 0, v); return b; }
function same(a, b) { return a.length === b.length && a.every((v, i) => v === b[i]); }
function concat(parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0;
  for (const bytes of parts) { out.set(bytes, at); at += bytes.length; }
  return out;
}

function dataTables(data) {
  const count = u32(data, 0) / 2 - 1;
  if (count !== 22) throw new Error('unrecognized Paradise DataPack');
  const off = Array.from({ length: count + 1 }, (_, i) => u32(data, 4 * i) * 2);
  if (off.some((a, i) => a > data.length || (i && a < off[i - 1]))) {
    throw new Error('unrecognized Paradise DataPack');
  }
  return off.slice(0, -1).map((a, i) => data.subarray(a, off[i + 1]));
}

function dataPack(tables) {
  const head = new Uint8Array(4 * (tables.length + 1));
  let at = head.length;
  tables.forEach((bytes, i) => { w32(head, 4 * i, at / 2); at += bytes.length; });
  w32(head, 4 * tables.length, at / 2);
  return concat([head, ...tables]);
}

function officialPlanetNames(source) {
  if (C.editionIdOf(source) !== 8) throw new Error('expected a Neon Planets source');
  const tables = C.textTablesOf(source)?.tabs;
  if (!tables || tables.length !== 9) throw new Error('unrecognized Neon text pack');
  const labels = tables.map(t => PLANET_TEXTS.map(id => C.txtWords(t, id)));
  if (labels.some(lang => lang.some(words => !words.length || !words.includes(217))))
    throw new Error('Neon Planets labels are missing');
  return labels;
}

function encodeWords(codes) {
  const bytes = new Uint8Array(2 * (codes.length + 1));
  codes.forEach((v, i) => w16(bytes, 2 * i, v));
  return bytes;
}

function rebuildNames(table, append, replace = new Map()) {
  const total = table.n + append.length, first = u16(table.b, 2);
  if (first !== table.n + 2 || u16(table.b, 2 * (table.n + 1)) * 2 !== table.b.length)
    throw new Error('unsupported native string table');
  const head = new Uint8Array(2 * (total + 2)), rows = [];
  w16(head, 0, total);
  let end = head.length / 2;
  for (let i = 0; i < total; i++) {
    let row;
    if (i >= table.n) row = encodeWords(append[i - table.n]);
    else if (replace.has(i)) row = encodeWords(replace.get(i));
    else {
      const from = u16(table.b, 2 * (i + 1)), to = u16(table.b, 2 * (i + 2));
      if (from < first || to < from || to * 2 > table.b.length) throw new Error('invalid native string offsets');
      row = table.b.subarray(2 * from, 2 * to);
    }
    w16(head, 2 * (i + 1), end);
    end += row.length / 2;
    rows.push(row);
  }
  if (end > 0xFFFF) throw new Error('too many text words');
  w16(head, 2 * (total + 1), end);
  return concat([head, ...rows]);
}

function names(base, source, chars) {
  const dst = C.textTablesOf(base).tabs;
  const src = C.textTablesOf(source).tabs;
  const labels = officialPlanetNames(source);
  const start = dst[0].n;
  if (dst.length !== 9 || src.length !== dst.length || dst.some(t => t.n !== start))
    throw new Error('unrecognized Paradise text pack');
  const tables = dst.map((table, lang) => {
    const add = chars.map(({ name }) => {
      const codes = C.txtWords(src[lang], name);
      if (!codes.length) throw new Error('Neon character name missing at ' + name);
      return codes;
    });
    return rebuildNames(table, add.concat(labels[lang], [[]]));
  });
  return { file: C.arc2Build(tables), start, planetTextIds: PLANET_TEXTS.map((_, i) => start + chars.length + i) };
}

function needsPlanetNames(rom) {
  if (!installed(rom) || u32(rom, NAME_STATE) !== NAME_MAGIC ||
      u32(rom, FIELD_STATE) !== 0xFFFFFFFF || u32(rom, FIELD_STATE + 4) !== 0xFFFFFFFF) return false;
  const packed = u32(rom, NAME_STATE + 4), first = (packed & 0xFFFF) + 86;
  if (packed >>> 16 !== 86 || u32(rom, NAME_STATE + 8) !== (~packed >>> 0)) return false;
  const labels = ['COSMIC PLANET', 'NEW TAMAGOTCHI PLANET', 'TAMAGOTCHI PLANET'];
  return labels.every((label, i) => C.textGet(rom, first + i + 1) === label);
}

function localizePlanetNames(rom, source) {
  if (!needsPlanetNames(rom)) throw new Error('no legacy Neon Planets labels to update');
  const labels = officialPlanetNames(source);
  const first = u16(rom, NAME_STATE + 4) + 86;
  const text = C.textTablesOf(rom);
  const tables = text.tabs.map((t, lang) => {
    if (first + 3 > t.n) throw new Error('Neon Planets label index is out of bounds');
    return rebuildNames(t, [], new Map(labels[lang].map((words, i) => [first + i, words])));
  });
  const archive = C.arc2Build(tables), ctx = C.openRom(rom);
  // The text pack is last, so replacing it leaves every sprite address intact.
  if (text.off + ctx.root.ents[2].clen !== ctx.assetEnd || text.off + archive.length > ctx.limit)
    throw new Error('cannot replace the Neon Planets label table in place');
  const out = new Uint8Array(rom), finish = text.off + archive.length, base = C.ASSET_BASE;
  out.set(archive, text.off);
  if (finish < ctx.assetEnd) out.fill(0xFF, finish, ctx.assetEnd);
  w32(out, base + 8, finish - base - 16);
  w32(out, base + 56, archive.length);
  w32(out, base + 60, archive.length);
  w32(out, base + 4, C.arc2Checksum(out, base, finish - base - 16));
  w32(out, FIELD_STATE, FIELD_MAGIC);
  w32(out, FIELD_STATE + 4, 0);
  C.verifyRom(out, []);
  return out;
}

function characterRows(data, chars, firstAnimation, firstName) {
  const sourceRows = new Map();
  for (let o = 0; o < data.length; o += RECORD_SIZE) sourceRows.set(u16(data, o), data.subarray(o, o + RECORD_SIZE));
  return chars.map((c, i) => {
    const row = sourceRows.get(c.id).slice();
    w16(row, 2, firstAnimation + i);
    w16(row, 4, firstName + i);
    return row;
  });
}

// Keep each original cell's palette indexes and the entire sprite metadata.
function legacySprite(image) {
  const h = C.spriteHeader(image);
  if (!(h.flags & 8)) return image;
  const { cells } = C.spriteDecode(image, 0, true);
  const bits = 1 << h.bpp;
  const head = image.slice(0, h.pixOffset + 8 * h.numSprites);
  head[4] = (h.flags & ~8) | 0x20;
  let offset = head.length - h.pixOffset;
  const payloads = [];
  cells.forEach((cell, i) => {
    const raw = new Uint8Array(Math.ceil(cell.length * bits / 32) * 4);
    cell.forEach((color, k) => {
      const bit = k * bits;
      raw[bit >>> 3] |= color << (bit & 7);
    });
    const rle = C.rleEncode(raw), uncompressed = raw.length <= rle.length;
    let payload = uncompressed ? raw : rle;
    if (h.flags & 0x80) payload = payload.map(b => b ^ 0x53);
    const padding = (-(h.pixOffset + offset)) & 3;
    if (padding) payloads.push(new Uint8Array(padding));
    offset += padding;
    const table = h.pixOffset + 8 * i;
    w32(head, table, offset | (uncompressed ? 0x80000000 : 0));
    w32(head, table + 4, payload.length);
    offset += payload.length;
    payloads.push(payload);
  });
  const size = h.pixOffset + offset, padding = (-size) & 3;
  w32(head, 0, size + padding);
  return concat([head, ...payloads, new Uint8Array(padding)]);
}

function spriteImporter(base, neon, flags, sourceFlags, baseGroups, compact = true) {
  const entries = base.slice(), pendingFlags = [];
  const mapped = new Map(), hashes = new Map();
  const byHeader = new Map(), pixels = new Map();
  function key(entry) { return entry.length + ':' + C.crc32(entry); }
  function note(k, id, image, bits) {
    const hits = hashes.get(k);
    const hit = { id, image, bits };
    if (hits) hits.push(hit);
    else hashes.set(k, [hit]);
  }
  base.forEach((image, id) => note(key(image), id, image, id * 2 < flags.length ? u16(flags, 2 * id) : 0));
  function header(image) { return image.subarray(4, C.spriteHeader(image).pixOffset); }
  for (const group of baseGroups) for (let id = group; id < group + 6; id++) {
    const h = header(base[id]), k = key(h), hits = byHeader.get(k) || [];
    hits.push({ id, h });
    byHeader.set(k, hits);
  }
  function logicalDuplicate(image, bits) {
    const h = header(image);
    const candidates = byHeader.get(key(h))?.filter(x => same(x.h, h) && u16(flags, 2 * x.id) === bits);
    if (!candidates?.length) return undefined;
    const incoming = C.spriteDecode(image, 0, true).cells;
    for (const { id } of candidates) {
      let known = pixels.get(id);
      if (!known) {
        known = C.spriteDecode(base[id], 0, true).cells;
        pixels.set(id, known);
      }
      if (known.length === incoming.length && known.every((cell, i) => same(cell, incoming[i]))) return { id };
    }
    return undefined;
  }
  function sprite(index) {
    if (mapped.has(index)) return mapped.get(index);
    const image = neon[index];
    if (!image) throw new Error('invalid Neon animation sprite ' + index);
    const bits = u16(sourceFlags, 2 * index), k = key(image);
    const find = (k, image) => hashes.get(k)?.find(x => x.bits === bits && same(x.image, image));
    const sameRaw = find(k, image) || logicalDuplicate(image, bits);
    let packed = image;
    if (!sameRaw) {
      packed = legacySprite(image);
      if (compact) packed = Lz4.wrapSprite(packed, true)?.copy || packed;
    }
    const packedKey = key(packed);
    const duplicate = sameRaw || find(packedKey, packed);
    const id = duplicate?.id ?? entries.length;
    if (!duplicate) {
      entries.push(packed);
      pendingFlags.push(word(bits));
      note(packedKey, id, packed, bits);
    }
    if (packed !== image || duplicate) note(k, id, image, bits);
    mapped.set(index, id);
    return id;
  }
  function allFlags() {
    const complete = new Uint8Array(2 * base.length);
    complete.set(flags);
    return concat([complete, ...pendingFlags]);
  }
  return { entries, mapped, sprite, allFlags };
}

function addAnimations(dst, src, chars, sprite) {
  const frameBytes = FRAMES_PER_ANIMATION * 4;
  const firstAnimation = dst[FRAMES].length / frameBytes;
  if (!Number.isInteger(firstAnimation) || src[FRAMES].length % frameBytes ||
      dst[FRAME_SOUNDS].some(Boolean) || src[FRAME_SOUNDS].some(Boolean)) {
    throw new Error('unrecognized Paradise animation frames');
  }
  const layers = [], offsets = [], frames = [];
  const map = new Map(), matches = new Map();
  let layerWords = dst[LAYERS].length / 2;
  let layerCount = dst[LAYER_OFFSETS].length / 4 - 1;
  let usedFrames = 0;
  const frameKey = (bytes, count) => count + ':' + bytes.length + ':' + C.crc32(bytes);
  function recordFrame(bytes, count, id) {
    const key = frameKey(bytes, count), known = matches.get(key) || [];
    known.push({ bytes, id });
    matches.set(key, known);
  }
  const indexed = new Set();
  for (let at = 0; at < dst[FRAMES].length; at += 4) {
    const first = u16(dst[FRAMES], at), count = u16(dst[FRAMES], at + 2);
    const key = first + ':' + count;
    if (!count || first + count > layerCount || indexed.has(key)) continue;
    indexed.add(key);
    const start = u32(dst[LAYER_OFFSETS], first * 4), end = u32(dst[LAYER_OFFSETS], (first + count) * 4);
    recordFrame(dst[LAYERS].subarray(start * 2, end * 2), count, first);
  }
  function translateLayer(id) {
    const begin = u32(src[LAYER_OFFSETS], id * 4);
    const end = u32(src[LAYER_OFFSETS], (id + 1) * 4);
    const bytes = src[LAYERS].slice(begin * 2, end * 2);
    const mask = u16(bytes, 0);
    if (bytes.length < 4 || mask & 0xF800 || !(mask & 0x400)) {
      throw new Error('unsupported Neon animation layer ' + id);
    }
    // Bit 10 is the last word of the layer and refers to the sprite pack.
    w16(bytes, bytes.length - 2, sprite(u16(bytes, bytes.length - 2)));
    return bytes;
  }
  for (const { animation } of chars) {
    const table = src[FRAMES].slice(animation * frameBytes, (animation + 1) * frameBytes);
    if (table.length !== frameBytes) throw new Error('missing Neon animation ' + animation);
    for (let at = 0; at < table.length; at += 4) {
      const first = u16(table, at), count = u16(table, at + 2);
      if (first === 0xFFFF && count === 0) continue;
      if (!count || (first + count + 1) * 4 > src[LAYER_OFFSETS].length) {
        throw new Error('invalid Neon animation frame');
      }
      usedFrames++;
      const key = first + ':' + count;
      if (!map.has(key)) {
        const translated = Array.from({ length: count }, (_, i) => translateLayer(first + i));
        const bytes = concat(translated);
        const found = matches.get(frameKey(bytes, count))?.find(x => same(x.bytes, bytes));
        if (found) map.set(key, found.id);
        else {
          recordFrame(bytes, count, layerCount);
          map.set(key, layerCount);
          for (const layer of translated) {
            offsets.push(dword(layerWords));
            layers.push(layer);
            layerWords += layer.length / 2;
            layerCount++;
          }
        }
      }
      w16(table, at, map.get(key));
    }
    frames.push(table);
  }
  if (layerCount >= 0xFFFF) throw new Error('too many animation layers');
  dst[LAYERS] = concat([dst[LAYERS], ...layers]);
  dst[LAYER_OFFSETS] = concat([dst[LAYER_OFFSETS].subarray(0, -4), ...offsets, dword(layerWords)]);
  dst[FRAME_SOUNDS] = concat([dst[FRAME_SOUNDS], new Uint8Array(4 * usedFrames)]);
  dst[FRAMES] = concat([dst[FRAMES], ...frames]);
  return { firstAnimation, addedFrames: usedFrames, addedLayers: layers.length };
}

function transplant(base, neon) {
  if (!growthKind(base) || C.editionIdOf(neon) !== 8) {
    throw new Error('expected an original Paradise base and a Neon Planets source');
  }
  // Node Buffer slices alias their input. The archive builder and frame
  // remapper need the copying behavior of an ordinary Uint8Array slice.
  const view = bytes => new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const dst = C.openRom(view(base)), src = C.openRom(view(neon));
  const target = dataTables(dst.files[0]), donor = dataTables(src.files[0]);
  if (growthKind(base) === 'gen3' && !Lz4.isInstalled(base)) {
    throw new Error('the base needs its legacy LZ4 decoder first');
  }
  const existing = new Set(C.charTable(dst.files[0]).map(c => c.id));
  const chars = C.charTable(src.files[0]).filter(c => c.id < 9000 && c.field >= 7 && c.field <= 9 && c.stage >= 3 && c.stage <= 5)
    .sort((a, b) => a.id - b.id);
  if (expectedFields.some((n, i) => chars.filter(c => c.field === i + 7).length !== n) ||
      chars.some(c => existing.has(c.id)) || new Set(chars.map(c => c.animation)).size !== chars.length) {
    throw new Error('Neon character roster does not match the expected 86 entries');
  }
  const imported = spriteImporter(dst.entries, src.entries, target[SPRITE_FLAGS], donor[SPRITE_FLAGS], dst.groups, Lz4.isInstalled(base));
  const anim = addAnimations(target, donor, chars, imported.sprite);
  const text = names(base, neon, chars);
  target[RECORDS] = concat([target[RECORDS], ...characterRows(donor[RECORDS], chars, anim.firstAnimation, text.start)]);
  target[SPRITE_FLAGS] = imported.allFlags();
  const rebuilt = C.buildRom({ ...dst, entries: imported.entries, files: [target, null, text.file] }, new Map(), dataPack(target));
  const rom = Six.sixFix(rebuilt.rom);
  C.verifyRom(rom, []);
  return {
    ...rebuilt, rom, planetTextIds: text.planetTextIds, ...anim, sprites: imported.entries.length - dst.entries.length,
    rows: chars.map((c, i) => ({ ...c, animation: anim.firstAnimation + i, name: text.start + i + 1 })),
  };
}

function itemsInstalled(rom) {
  const names = u32(rom, ITEM_STATE + 4);
  return u32(rom, ITEM_STATE) === ITEM_MAGIC && names >>> 16 === ITEM_COUNT &&
    u32(rom, ITEM_STATE + 8) === (~names >>> 0) && u32(rom, ITEM_STATE + 12) <= 1;
}

function importItemScripts(dst, src, sprite) {
  const original = dst[SCRIPTS], offsetsTable = dst[SCRIPT_OFFSETS];
  const from = src[SCRIPT_OFFSETS], code = src[SCRIPTS];
  if (original.length % 4 || code.length % 4 || u16(offsetsTable, offsetsTable.length - 2) * 4 > original.length ||
      u16(from, from.length - 2) * 4 > code.length)
    throw new Error('unrecognized item script table');
  const last = u16(offsetsTable, offsetsTable.length - 2) * 4;
  const index = last === original.length ? offsetsTable.subarray(0, -2) : offsetsTable;
  const imported = new Map(), offsets = [], bytecode = [];
  let end = original.length;
  // A 16-bit command header is followed by the operands selected by its high byte.
  // The two first-operand references used by the donor are sprites and other scripts.
  const supported = new Set([0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 13, 16, 26, 27, 28, 29, 30, 32, 36]);
  function script(id) {
    if (imported.has(id)) return imported.get(id);
    const start = 4 * u16(from, 2 * id), limit = (id + 1) * 2 < from.length ? 4 * u16(from, 2 * (id + 1)) : code.length;
    if (id * 2 >= from.length || start + 2 > limit || limit > code.length)
      throw new Error('missing Neon item script ' + id);
    const result = code.slice(start, limit), length = result.length;
    const mapped = index.length / 2 + bytecode.length;
    imported.set(id, mapped);
    offsets.push(word(end / 4)); bytecode.push(result); end += result.length;
    if (end / 4 > 0xFFFF) throw new Error('too many item script words');
    for (let pos = 2; pos < length;) {
      const command = u16(result, pos), op = command & 255;
      pos += 2;
      if (command === 0xFCBD) break;
      if (!supported.has(op)) throw new Error('unknown Neon item command ' + command.toString(16) + ' in script ' + id + ' at ' + (pos - 2));
      for (const bit of [15, 14, 13, 11, 10, 9]) {
        if (!(command & (1 << bit))) continue;
        if (pos + 2 > length) throw new Error('truncated Neon item command in script ' + id + ' at ' + pos);
        if (bit === 15) {
          const ref = u16(result, pos);
          if (ref && op === 1) w16(result, pos, sprite(ref));
          if (ref && op === 16) w16(result, pos, script(ref));
          if (op !== 1 && op !== 16) throw new Error('unrecognized Neon item script reference');
        }
        pos += 2;
      }
    }
    return mapped;
  }
  return { script, finish() {
    dst[SCRIPT_OFFSETS] = concat([index, ...offsets, word(end / 4)]);
    dst[SCRIPTS] = concat([original, ...bytecode]);
  } };
}

function itemRows(table) {
  if (table.length % ITEM_SIZE) throw new Error('invalid item records');
  return Array.from({ length: table.length / ITEM_SIZE }, (_, i) => table.subarray(i * ITEM_SIZE, (i + 1) * ITEM_SIZE));
}

function clearPlaceholderCounts(page) {
  const SV = root.ParamixSaves || require('../../src/saves');
  if (!page || !SV.settingsOk(page)) return false;
  let changed = false;
  for (let i = 0; i < 640; i++) {
    if (!ITEM_PLACEHOLDERS.includes(u16(page, 0x270 + 2 * i))) continue;
    const off = 0x770 + (i >> 1), mask = 0xF << (4 * (i & 1));
    if (page[off] & mask) { page[off] &= ~mask; changed = true; }
  }
  if (changed) {
    const crc = SV.crc16arc(page, 4, 0x1000);
    w16(page, 0, crc); w16(page, 2, ~crc);
  }
  return changed;
}

function clearItemSaveSlots(slots) {
  return slots.map(slot => {
    const pages = F.slotPages(slot, false);
    const a = clearPlaceholderCounts(pages.get(0xEFE000));
    const b = clearPlaceholderCounts(pages.get(0xEFF000));
    return a || b ? { ...F.slotBuild(pages, slot.index, 0, 0x1000000, slot.head), index: slot.index } : slot;
  });
}

function transplantItems(base, neon) {
  const edition = C.editionIdOf(base), stockCount = BASE_ITEM_COUNTS[edition === 7 ? 5 : edition];
  if (!stockCount || C.editionIdOf(neon) !== 8) throw new Error('expected an original Paradise base and a Neon Planets source');
  if (itemsInstalled(base) || !base.subarray(ITEM_STATE, ITEM_STATE + 16).every(b => b === 0xFF))
    throw new Error('Neon item manifest is already installed or occupied');
  const stub = F.embedded()?.stub || (typeof require !== 'undefined'
    ? require('node:fs').readFileSync(__dirname + '/../../feld/stub_feld_tpl.bin') : null);
  const menu = stub && F.feldFind(base, stub);
  if (F.languagePatchPages(base).includes(GROWTH_PAGE) || (menu &&
      [...F.slotRanges(menu), [menu.off, menu.off + 0x10000], [menu.stubOff, menu.stubOff + 0x1000]]
        .some(([start, end]) => GROWTH_PAGE < end && GROWTH_PAGE + 0x1000 > start))) {
    throw new Error('Neon item manifest page is reserved');
  }
  const dst = C.openRom(new Uint8Array(base)), src = C.openRom(new Uint8Array(neon));
  const target = dataTables(dst.files[0]), donor = dataTables(src.files[0]);
  const existing = itemRows(target[ITEMS]).map(row => row.slice());
  const positions = new Map(existing.map((row, i) => [u16(row, 0), i]));
  const placeholders = edition >= 5 ? ITEM_PLACEHOLDERS : [];
  if (existing.length !== stockCount || positions.size !== existing.length ||
      placeholders.some(id => !existing[positions.get(id)]?.subarray(2).every(b => b === 0)) ||
      NEON_ITEM_IDS.some(id => positions.has(id) && !placeholders.includes(id))) {
    throw new Error('unrecognized base item table');
  }
  const donors = new Map(itemRows(donor[ITEMS]).map(row => [u16(row, 0), row]));
  const items = NEON_ITEM_IDS.map(id => donors.get(id));
  if (items.some(row => !row || row[22] < 1 || row[22] > 7 || u16(row, 24) === 0xFFFF))
    throw new Error('Neon donor does not have the expected 59 items');
  const images = spriteImporter(dst.entries, src.entries, target[SPRITE_FLAGS], donor[SPRITE_FLAGS], dst.groups, Lz4.isInstalled(base));
  const scripts = importItemScripts(target, donor, images.sprite);
  const texts = C.textTablesOf(dst.rom).tabs, source = C.textTablesOf(src.rom).tabs, first = texts[0].n;
  if (texts.length !== 9 || source.length !== 9 || texts.some(t => t.n !== first)) throw new Error('invalid Neon item language tables');
  const translated = texts.map((t, lang) => rebuildNames(t, items.map(row => {
    const words = C.txtWords(source[lang], u16(row, 2) + 1);
    if (!words.length) throw new Error('missing native item name');
    return words;
  })));
  items.forEach((native, i) => {
    const row = native.slice(), id = u16(row, 0);
    w16(row, 2, first + i);
    for (const at of [4, 6]) { const ref = u16(row, at); if (ref) w16(row, at, images.sprite(ref)); }
    for (let at = 8; at <= 18; at += 2) { const ref = u16(row, at); if (ref) w16(row, at, scripts.script(ref)); }
    if (positions.has(id)) existing[positions.get(id)] = row;
    else existing.push(row);
  });
  scripts.finish();
  target[ITEMS] = concat(existing);
  target[SPRITE_FLAGS] = images.allFlags();
  const rebuilt = C.buildRom({ ...dst, entries: images.entries, files: [null, null, C.arc2Build(translated)] }, new Map(), dataPack(target));
  const rom = Six.sixFix(rebuilt.rom), manifest = first | ITEM_COUNT << 16;
  w32(rom, ITEM_STATE, ITEM_MAGIC); w32(rom, ITEM_STATE + 4, manifest);
  w32(rom, ITEM_STATE + 8, ~manifest); w32(rom, ITEM_STATE + 12, 0);
  if (placeholders.length) for (const at of [0xEFE000, 0xEFF000]) clearPlaceholderCounts(rom.subarray(at, at + 0x1000));
  C.verifyRom(rom, []);
  return { ...rebuilt, rom, ids: items.map(row => u16(row, 0)) };
}

function growthBranch(from, to) {
  const delta = to - (from + 4), sign = delta >>> 24 & 1;
  const bits = new Uint8Array(4);
  w16(bits, 0, 0xF000 | sign << 10 | (delta >>> 12 & 1023));
  w16(bits, 2, 0xD000 | (1 ^ sign ^ (delta >>> 23 & 1)) << 13 |
    (1 ^ sign ^ (delta >>> 22 & 1)) << 11 | (delta >>> 1 & 2047));
  return bits;
}

function growthHeader(code) {
  const version = code && u32(code, 4);
  if (!code || code.length < 16 || code.length > 0x1000 ||
      u32(code, 0) !== GROWTH_MAGIC || (version !== 1 && version !== GROWTH_VERSION)) return null;
  const entry = u32(code, 8) & ~1, config = u32(code, 12);
  if (entry !== 16 || config < entry + 16 || config + GROWTH_CONFIG_SIZE > code.length) return null;
  return { entry, config, version };
}

function installed(rom) {
  const header = growthHeader(rom.subarray(GROWTH_PAGE, GROWTH_PAGE + 0x1000));
  const profile = GROWTH_PROFILES[growthKind(rom)];
  return !!header && !!profile && same(growthBranch(profile.hook, GROWTH_PAGE + header.entry),
    rom.subarray(profile.hook, profile.hook + 4));
}

function fixLayerIndices(rom) {
  // DataPack frames hold an unsigned layer index and a separate count.
  // The old renderer sign-extends the index twice; the import exceeds 32767.
  const unsigned = LAYER_INDEX_CODE.slice();
  unsigned[5] = 0xF8;
  const sites = GROWTH_PROFILES[growthKind(rom)]?.layers;
  if (!sites || sites.some(at => {
    const code = rom.subarray(at, at + unsigned.length);
    return !same(code, LAYER_INDEX_CODE) && !same(code, unsigned);
  })) throw new Error('unexpected Paradise animation layer reader');
  for (const at of sites) rom[at + 5] = unsigned[5];
}

function growthBinary(kind) {
  const code = GROWTH_PROFILES[kind].code;
  if (!code.startsWith('__')) return Uint8Array.from(atob(code), c => c.charCodeAt(0));
  const name = kind === 'gen3' ? 'growth' : 'growth-' + kind;
  return typeof require !== 'undefined' ? require('node:fs').readFileSync(__dirname + '/' + name + '.bin') : null;
}

function growthUpdateNeeded(rom) {
  return installed(rom) && u32(rom, GROWTH_PAGE + 4) < GROWTH_VERSION;
}

function upgradeGrowth(rom) {
  if (!growthUpdateNeeded(rom)) return rom;
  const code = growthBinary(growthKind(rom)), fresh = growthHeader(code);
  const old = growthHeader(rom.subarray(GROWTH_PAGE, ITEM_STATE));
  if (fresh?.version !== GROWTH_VERSION || !old || code.length > ITEM_STATE - GROWTH_PAGE) {
    throw new Error('Cannot update the Neon growth helper');
  }
  const config = rom.subarray(GROWTH_PAGE + old.config, GROWTH_PAGE + old.config + GROWTH_CONFIG_SIZE);
  const out = new Uint8Array(rom);
  out.set(code, GROWTH_PAGE);
  out.set(config, GROWTH_PAGE + fresh.config);
  return out;
}

function installGrowth(rom, rows, code) {
  const kind = growthKind(rom), profile = GROWTH_PROFILES[kind];
  if (!profile || (kind === 'gen3' && !Lz4.isInstalled(rom)) ||
      !same(rom.subarray(profile.hook, profile.hook + 4), [0xBD, 0xF8, profile.stack, 0x10])) {
    throw new Error('unexpected Paradise growth routine');
  }
  code ||= growthBinary(kind);
  const header = growthHeader(code);
  if (!header || code.length > ITEM_STATE - GROWTH_PAGE) throw new Error('invalid Neon growth helper');
  const stub = F.embedded()?.stub || (typeof require !== 'undefined'
    ? require('node:fs').readFileSync(__dirname + '/../../feld/stub_feld_tpl.bin') : null);
  const menu = stub ? F.feldFind(rom, stub) : null;
  const reserved = menu ? [...F.slotRanges(menu), [menu.off, menu.off + 0x10000], [menu.stubOff, menu.stubOff + 0x1000]] : [];
  if (F.languagePatchPages(rom).includes(GROWTH_PAGE) ||
      reserved.some(([begin, end]) => GROWTH_PAGE < end && GROWTH_PAGE + 0x1000 > begin) ||
      !rom.subarray(GROWTH_PAGE, ITEM_STATE).every(v => v === 0xFF) ||
      (!itemsInstalled(rom) && !rom.subarray(ITEM_STATE, NAME_STATE).every(v => v === 0xFF)) ||
      !rom.subarray(NAME_STATE, GROWTH_PAGE + 0x1000).every(v => v === 0xFF)) {
    throw new Error('Neon growth helper page is occupied or reserved');
  }
  const records = C.charTable(C.arc2Files(rom, C.ASSET_BASE)[0]);
  if (rows.length !== 86 || !rows.every(c => records.some(r => c.id === r.id && c.animation === r.animation))) {
    throw new Error('import the 86 Neon character records first');
  }
  const worlds = expectedFields.map((n, i) => {
    const all = rows.filter(r => r.field === i + 7);
    const children = all.filter(r => r.stage === 3);
    const teens = all.filter(r => r.stage === 4).sort((a, b) => a.line - b.line);
    const adults = all.filter(r => r.stage === 5).sort((a, b) => a.id - b.id);
    const regular = adults.filter(r => r.rank >= 1 && r.rank <= 4);
    if (all.length !== n || children.length !== 1 || teens.length !== NATIVE_TEENS ||
        regular.length !== NATIVE_ADULTS || adults.length > MAX_ADULTS) {
      throw new Error('unexpected Neon growth roster');
    }
    const order = regular.map(r => r.id);
    // Stock Neon has the first two adults of this Tamagotchi line reversed.
    if (i === 2 && order[8] === 4191 && order[9] === 4192) [order[8], order[9]] = [order[9], order[8]];
    return { child: children[0].id, teens: teens.map(r => r.id), order, adults: adults.map(r => r.id) };
  });
  const rosters = [...worlds.map(w => w.child), ...worlds.flatMap(w => w.teens), ...worlds.flatMap(w => w.order),
    ...worlds.map(w => w.adults.length), ...worlds.flatMap(w => w.adults.concat(Array(MAX_ADULTS - w.adults.length).fill(0)))];
  const out = new Uint8Array(rom);
  out.set(code, GROWTH_PAGE);
  rosters.forEach((id, i) => w16(out, GROWTH_PAGE + header.config + i * 2, id));
  const ids = rows.map(row => row.name - 1).sort((a, b) => a - b);
  if (!ids.every((id, i) => id === ids[0] + i && id >= 0 && id < 0x10000))
    throw new Error('unexpected Neon name indexes');
  const range = ids[0] | (ids.length << 16);
  w32(out, NAME_STATE, NAME_MAGIC);
  w32(out, NAME_STATE + 4, range); w32(out, NAME_STATE + 8, ~range);
  w32(out, NAME_STATE + 12, 0);
  const english = C.textTablesOf(rom).tabs[1];
  if (PLANET_TEXTS.every((_, i) => C.txtWords(english, ids[0] + rows.length + 1 + i).includes(217))) {
    w32(out, FIELD_STATE, FIELD_MAGIC);
    w32(out, FIELD_STATE + 4, 0);
  }
  out.set(growthBranch(profile.hook, GROWTH_PAGE + header.entry), profile.hook);
  fixLayerIndices(out);
  return out;
}

root.ParamixNeonTransplant = { transplant, installGrowth, installed, growthUpdateNeeded, upgradeGrowth, fixLayerIndices, needsPlanetNames, localizePlanetNames, transplantItems, itemsInstalled, clearPlaceholderCounts, clearItemSaveSlots, growthPage: GROWTH_PAGE };
if (typeof module !== 'undefined') module.exports = root.ParamixNeonTransplant;
})(typeof window !== 'undefined' ? window : globalThis);
