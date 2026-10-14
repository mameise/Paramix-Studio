/*
 * Paramix Studio: Paramix menu in the ROM (field switch, items, Tamapoints).
 *
 * A small program in the ROM, launched like the emulator and DOOM through the stub
 * (hold A and C on the home screen). It can
 *   - switch the field (Land/Water/Sky or Tropical/Glacier), each field with
 *     its own save, which is packed into a slot when switching,
 *   - put items into the bag and set Tamapoints (as in the TamaParadise app),
 *   - launch an installed program (DOOM, TamaEmu).
 * Anything that depends on the edition lives in the program's settings block and is
 * set here (profiles below, measured on the factory images).
 *
 * Location in flash:
 *   without DOOM: program and catalog from 0x0B1000, slots behind the character data
 *   with DOOM:    program, catalog and slots in the free end of the DOOM program region
 */
(function (root) {
'use strict';

const XIP_DEC = 0x11000;
const xip = (off) => 0x10000000 + off - XIP_DEC;
const APP_OFF = 0xA0000, STUB_OFF = 0xB0000, FELD_OFF = 0xB1000, FELD_END = 0xC0000;
const DOOM_CODE = 0xB1000, DOOM_SAVE = 0x10E000;
const SLOT_BASE = 0x830000, SLOT_SIZE = 0x58000, GUEST_OFF = 0x940000;
const FELD_MAGIC = 0x444C4546, SLOT_MAGIC = 0x544C5346, CAT_MAGIC = 0x54414350, PMXC = 0x43584D50, PDMC = 0x434D4450;
const PH_APP = 0x1234ABCD, PH_FELD = 0x6789EF01, PH_AUTO = 0x66666661, PH_ORIG = [0x22222221, 0x33333331, 0x44444441, 0x55555551];
const HOOK_OFF = [0x40, 0x46, 0x4C, 0x52];
const F_FIELDS = 1, F_BAG = 2;
// Six fields (six.js): helper page at 0x10F000, set byte in the settings block
const SIX_OFF = 0x10F000, SIX_MAGIC = 0x31584953, SIX_FLAG = 0xF04, CODE_END = 0x10F000;
const isSix = (rom) => rom.length > SIX_OFF + 4 && u32(rom, SIX_OFF) === SIX_MAGIC;

const rgb = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
/*
 * Editions. ID page: word 0 = ID, footer u16 h0, u16 h1, C minus ID,
 * inverse. Factory settings (0xEFE000/0xEFF000): zeros and these bytes, plus the
 * checksum, which the program computes itself. Bag and money as in the app.
 */
const PROFILES = [
  { name: 'gen1', idPage: 0xD49000, edIds: [1, 2, 3], ids: [1, 2, 3], names: ['LAND', 'WATER', 'SKY'], h: [1, 4], c: 0x70169E2C,
    cols: [rgb(80, 200, 60), rgb(40, 120, 255), rgb(120, 210, 255)], fieldByte: 439,
    common: [[8, 0x11], [16, 0xc0], [17, 0xa8], [168, 1], [576, 0x3f], [577, 0x1f], [1856, 1]],
    field: [[[4, 0x0c], [170, 2], [439, 0]], [[4, 4], [170, 1], [439, 1]], [[4, 4], [170, 1], [439, 2]]],
    bag: [0xb4, 0x240, 0x740] },
  // Jade Forest: one ID (4), three fields in the save (byte 443): LAND, WATER, FOREST (texts 14 to 16 in the ROM)
  { name: 'jade', idPage: 0xD49000, edIds: [4], ids: [0, 0, 0], names: ['LAND', 'WATER', 'FOREST'], h: [2, 0], c: 0x849633DB,
    cols: [rgb(80, 200, 60), rgb(40, 120, 255), rgb(60, 190, 110)], fieldByte: 443,
    common: [[4, 4], [8, 17], [16, 192], [17, 168], [168, 1], [170, 3], [580, 63], [581, 31], [1860, 1]],
    field: [[[443, 0]], [[443, 1]], [[443, 2]]], bag: [0xb4, 0x240, 0x740] },
  // Tropical and Glacier: same firmware, three fields (byte 499): TROPICAL, ICE (Glacier), FOREST
  { name: 'gen3', idPage: 0xD29000, edIds: [5, 6], ids: [5, 6, 0], names: ['TROPICAL', 'GLACIER', 'FOREST'], h: [3, 1], c: 0x96CB83B8,
    cols: [rgb(255, 150, 60), rgb(150, 220, 255), rgb(60, 190, 110)], fieldByte: 499,
    common: [[4, 4], [8, 17], [12, 109], [13, 1], [16, 192], [17, 168], [180, 1], [182, 1], [624, 62], [625, 31], [1904, 1]],
    field: [[[499, 0]], [[499, 1]], [[499, 2]]], bag: [0xc0, 0x270, 0x770] },
];
const NAMES = { 1: 'Land', 2: 'Water', 3: 'Sky', 4: 'Jade Forest', 5: 'Tropical', 6: 'Glacier' };
/* Tropical/Glacier with six fields: the second three are slots 0..2 with the set byte 1. */
function profileOf(rom, p) {
  if (!isSix(rom)) return p;
  const g = PROFILES[0];
  if (p.name === 'gen3') return Object.assign({}, p, {
    ids: p.ids.concat(p.ids), names: p.names.concat(g.names), cols: p.cols.concat(g.cols),
    field: p.field.concat(p.field.map((f) => f.concat([[SIX_FLAG, 1]]))), six: SIX_FLAG, sixMap: [0, 1, 2, 255, 3, 4, 5, 255],
  });
  // Jade Forest: Sky is slot 1 (Water) with the set byte
  if (p.name === 'jade') return Object.assign({}, p, {
    ids: p.ids.concat([0]), names: p.names.concat(['SKY']), cols: p.cols.concat([g.cols[2]]),
    field: p.field.concat([[[p.fieldByte, 1], [SIX_FLAG, 1]]]), six: SIX_FLAG, sixMap: [0, 1, 2, 255, 0, 3, 2, 255],
  });
  return p;
}
const cap = (s) => s.charAt(0) + s.slice(1).toLowerCase();

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w16(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; }
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }
function isFF(b, a, n) { for (let i = a; i < a + n; i++) if (b[i] !== 0xFF) return false; return true; }
const page = (n) => (n + 0xFFF) & ~0xFFF;
function b64(s) {
  if (typeof atob === 'function') { const t = atob(s); const o = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) o[i] = t.charCodeAt(i); return o; }
  return new Uint8Array(Buffer.from(s, 'base64'));
}
function findWord(b, v, from = 0, to = b.length) { for (let i = from; i + 4 <= to; i += 2) if (u32(b, i) === v) return i; return -1; }

/** Edition of the ROM: { profile, id } or null. */
function edition(rom) {
  for (const p of PROFILES) {
    const id = u32(rom, p.idPage);
    if (p.edIds.includes(id)) return { profile: p, id };
  }
  return null;
}
/** Edition ID (1 Land ... 6 Glacier) or 0. */
function feldId(rom) { const e = edition(rom); return e ? e.id : 0; }
/** Does this ROM support the field switch? (every recognized edition has three fields) */
function canFields(rom) { return !!edition(rom); }
/** The fields of this ROM (names). */
function fieldNames(rom) { const e = edition(rom); return e ? profileOf(rom, e.profile).names.map(cap) : []; }
/** Active field (name), read from the save. */
function fieldNow(rom) {
  const e = edition(rom); if (!e) return '';
  const P = profileOf(rom, e.profile);
  let f = rom[0xEFE000 + P.fieldByte];
  if (P.six && f < 4) f = P.sixMap[(rom[0xEFE000 + P.six] === 1 ? 4 : 0) + f];
  return f < P.names.length ? cap(P.names[f]) : NAMES[e.id];
}

/** Program in the ROM: 0 none, 1 DOOM, 2 emulator. */
function appKind(rom) {
  if (u32(rom, APP_OFF + 0x140) !== PMXC) return 0;
  for (let i = 0x140; i < 0x400; i += 4) if (u32(rom, APP_OFF + i) === PDMC) return 1;
  return 2;
}

/*
 * Where is the stub? Normally at 0x0B0000 (shared with the emulator and DOOM).
 * In editions whose firmware code still occupies that spot (Tropical, Glacier),
 * right after the code in the first free region below 0x110000.
 */
function freeStubOff(rom) {
  if (isFF(rom, STUB_OFF, 0x2000)) return STUB_OFF;
  let p = 0x10F000;
  while (p > 0x90000 && isFF(rom, p - 0x1000, 0x1000)) p -= 0x1000;
  return p;
}
function feldAt(rom, stubOff, ph) {
  const v = u32(rom, stubOff + ph);
  if (v < 0x10000000 || v >= 0x11000000) return null;
  const off = v - 0x10000000 + XIP_DEC;
  const c = off + 0x140;
  if (off + 0x200 > rom.length || u32(rom, c) !== FELD_MAGIC) return null;
  const version = u32(rom, c + 4);
  const f = { off, stubOff, version, slotOff: u32(rom, c + 8), slotSize: u32(rom, c + 12), nslots: u32(rom, c + 16), flags: F_FIELDS, catOff: 0, catN: 0 };
  if (version >= 2) { f.flags = u32(rom, c + 36); f.catOff = u32(rom, c + 272); f.catN = u32(rom, c + 276); }
  f.slotOff2 = version >= 4 ? u32(rom, c + 364) : 0; f.slotN1 = version >= 4 ? u32(rom, c + 368) : 0;
  return f;
}
/** Flash address of slot i (from menu version 4 the slots can lie in two regions). */
function slotAddr(f, i) { return f.slotOff2 && f.slotN1 && i >= f.slotN1 ? f.slotOff2 + (i - f.slotN1) * f.slotSize : f.slotOff + i * f.slotSize; }
/** Is the menu installed? Returns { off, stubOff, version, slotOff, slotSize, nslots, flags, catOff, catN } or null. */
function feldFind(rom, tpl) {
  const ph0 = findWord(tpl, PH_FELD);
  if (ph0 < 0) return null;
  // older stubs: menu address at 0xF0 (up to 1.2.6) and 0x118 (1.2.7)
  for (const ph of [...new Set([ph0, 0xF0, 0x118])]) {
    const f = feldAt(rom, STUB_OFF, ph);
    if (f) return f;
    for (let p = 0x90000; p < 0x110000; p += 0x1000) { const g = p !== STUB_OFF && feldAt(rom, p, ph); if (g) return g; }
  }
  return null;
}

/** Saved slots (raw bytes) of a ROM with the menu, for moving them over. */
function feldSlots(rom, tpl) {
  const f = feldFind(rom, tpl);
  if (!f || !(f.flags & F_FIELDS)) return [];
  const out = [];
  for (let i = 0; i < f.nslots; i++) {
    const b = slotAddr(f, i);
    if (u32(rom, b) !== SLOT_MAGIC) continue;
    const field = u32(rom, b + 4), bytes = u32(rom, b + 12);
    if (field < 1 || field > 6 || bytes > f.slotSize - 0x1000) continue;
    // Slot ID: from menu version 3 the field number + 1, before that (v0.9.4/v0.9.5) the edition ID
    out.push({ index: f.version >= 3 ? field - 1 : i, field, data: rom.slice(b, b + 0x1000 + bytes) });
  }
  return out;
}

// ---------------------------------------------------------------- catalog
/*
 * Items from the user's own image, as in the TamaParadise app (Bilder.kt):
 * picture table found by its shape, item table (46-byte records), English
 * text table found by its marker words. Name = text number in the record plus one.
 */
const SATZ = 46, CH_A = 318;
const SONDER = { 305: ' ', 317: ' ', 375: '&', 376: "'", 381: '.' };
const WORTE = ['FOOD', 'SNACKS', 'OUTSIDE TOY', 'LAB LOG', 'MILK'];
/*
 * The image table of the ROM. Only searched inside the asset archive (from 0x111000 to
 * its end): programs built in by the Studio (DOOM, TamaEmu) contain tables that look
 * alike, and DOOM sits before the archive's image table.
 */
function bildTabelle(d) {
  const end = Math.min(d.length, assetEnd(d));
  for (let o = 0x111000; o + 4 < end; o += 4) {
    const e0 = u32(d, o);
    if (e0 < 0x1000 || e0 > 0x8000 || e0 % 4) continue;
    let prev = e0, ok = true;
    for (let i = 1; i < 60; i++) { const w = u32(d, o + 4 * i); if (w <= prev || w > 0x1000000) { ok = false; break; } prev = w; }
    if (!ok) continue;
    const k = o + e0;
    if (k + 24 >= d.length) continue;
    const g = u32(d, k), p = u32(d, k + 20);
    if (p > 24 && p < g && g >= 1 && g <= 0x20000 && p % 8 === 0 && d[k + 8] > 0 && d[k + 9] > 0) return o;
  }
  return -1;
}
function itemTabelle(d, nb) {
  let best = -1, bn = 0;
  for (let o = 0; o + SATZ < d.length; o += 2) {
    const k0 = u16(d, o);
    if (k0 < 5001 || k0 > 9999) continue;
    const seen = new Set(); let n = 0;
    while (n < 400 && o + n * SATZ + 5 < d.length) {
      const k = u16(d, o + n * SATZ), b = u16(d, o + n * SATZ + 4);
      if (k < 5001 || k > 9999 || b >= nb || seen.has(k)) break;
      seen.add(k); n++;
    }
    if (n > bn) { bn = n; best = o; }
    if (n > 8) o += n * SATZ - 2;
  }
  return [best, bn];
}
function zeichenkette(d, t, n) {
  const ab = u16(d, t + 2 * n);
  if (ab <= 0 || ab > 0x8000) return '';
  let o = t + 2 * ab, s = '';
  while (s.length < 40) {
    const c = u16(d, o);
    if (c === 0) break;
    if (c >= CH_A && c < CH_A + 26) s += String.fromCharCode(65 + c - CH_A);
    else if (SONDER[c] !== undefined) s += SONDER[c];
    else return '';
    o += 2;
  }
  return s.trim();
}
function wort3(d, t, n) {
  const ab = u16(d, t + 2 * n);
  if (ab <= 0 || ab > 0x8000) return false;
  for (let k = 0; k < 3; k++) { const c = u16(d, t + 2 * ab + 2 * k); if (c < CH_A || c >= CH_A + 26) return false; }
  return true;
}
function textTabelle(d) {
  let best = -1, bp = 0;
  for (let o = 0; o < d.length - 8; o += 2) {
    const n = u16(d, o);
    if (n < 100 || n > 4000 || !(u16(d, o + 2) > n && u16(d, o + 4) > u16(d, o + 2) && u16(d, o + 6) > u16(d, o + 4)) || !wort3(d, o, 1)) continue;
    let prev = n, ok = true;
    for (let i = 1; i < Math.min(n, 200); i++) { const w = u16(d, o + 2 * i); if (w < prev || w > 0x8000) { ok = false; break; } prev = w; }
    if (!ok) continue;
    let p = 0; for (let k = 1; k < 120; k++) if (WORTE.includes(zeichenkette(d, o, k))) p++;
    if (p > bp) { bp = p; best = o; }
  }
  return bp >= 2 ? best : -1;
}
/*
 * Byte sum of an item picture entry, as the menu computes it after copying the entry
 * to RAM (header, sub-image table, palette, pixel data of all sub-images). 0 = unknown.
 */
function picSum(d, P, pic) { const i = picInfo(d, P, pic); return i ? i.sum : 0; }
/* Where the picture entry lies in flash, its length, byte sum and first word (or null). */
function picInfo(d, P, pic) {
  const n = u32(d, P) / 4; if (pic >= n) return null;
  const e = P + u32(d, P + 4 * pic); if (e & 3) return null;
  const flags = d[e + 4], bpp = d[e + 5], sw = d[e + 8], sh = d[e + 9];
  if (bpp > 3 || !sw || !sh) return null;
  const iw = d[e + 12] || 1, ih = d[e + 13] || 1, cnt = iw * ih, bits = 1 << bpp, mask = (1 << bits) - 1;
  const need = (sw * sh * bits + 7) >> 3, po = u16(d, e + 18), px = u16(d, e + 20);
  let end = px + 8 * cnt;
  for (let c = 0; c < cnt; c++) {
    let x;
    if (flags & 0x60) { const w = u32(d, e + px + 8 * c), off = w & 0x7FFFFFFF, ln = u32(d, e + px + 8 * c + 4); x = px + off + ((w & 0x80000000) ? need : ln); }
    else x = px + (c + 1) * need;
    if (x > end) end = x;
  }
  end = Math.max(end, po + 2 * (mask + 1));
  if (end > 12288 || e + end > d.length) return null;
  let sum = 0; for (let i = 0; i < end; i++) sum += d[e + i];
  return { addr: e, len: end, sum: (sum & 0xFFFF) || 1, head: u32(d, e) };
}
/** Catalog: [{ id, fixed, pic, sum, name }] ascending, only groups 5 to 8. */
function itemCatalog(d) {
  const bt = bildTabelle(d);
  if (bt < 0) return [];
  const [it, n] = itemTabelle(d, u32(d, bt) / 4);
  if (it < 0 || n < 100) return [];
  const tt = textTabelle(d);
  const out = [];
  for (let i = 0; i < n; i++) {
    const o = it + i * SATZ, id = u16(d, o);
    if (id < 5001 || id > 8999) continue;
    const pic = u16(d, o + 4);
    const pi = picInfo(d, bt, pic);
    out.push({ id, fixed: u16(d, o + 24) === 0xFFFF, pic, sum: pi ? pi.sum : 0, at: pi, name: tt >= 0 ? zeichenkette(d, tt, u16(d, o + 2) + 1) : '' });
  }
  out.sort((a, b) => a.id - b.id);
  out.pack = bt;
  return out;
}
/*
 * Catalog: 8 byte header, 28 bytes per entry, then "PEXT" and 12 bytes per entry:
 * flash address of the picture entry, its length, byte sum, first word. The menu
 * reads the picture straight from there (no lookup in the ROM's image table).
 */
const EXT_MAGIC = 0x54584550;
function catalogBlob(cat) {
  const x = 8 + 28 * cat.length;
  const b = new Uint8Array(x + 4 + 12 * cat.length);
  w32(b, x, EXT_MAGIC);
  cat.forEach((c, i) => { const o = x + 4 + 12 * i; if (c.at) { w32(b, o, c.at.addr); w16(b, o + 4, c.at.len); w16(b, o + 6, c.at.sum); w32(b, o + 8, c.at.head); } });
  w32(b, 0, CAT_MAGIC); w32(b, 4, cat.length);
  cat.forEach((c, i) => {
    const o = 8 + 28 * i, nm = c.name.slice(0, 20);
    w16(b, o, c.id); b[o + 2] = c.fixed ? 1 : 0; b[o + 3] = nm.length; w16(b, o + 4, c.pic); w16(b, o + 6, c.sum || 0);
    for (let k = 0; k < nm.length; k++) b[o + 8 + k] = nm.charCodeAt(k);
  });
  return b;
}

// ---------------------------------------------------------------- install and remove
function doomCodeEnd(rom) {
  let e = DOOM_SAVE;
  while (e > DOOM_CODE && rom[e - 1] === 0xFF) e--;
  return page(e);
}
function assetEnd(rom) { return 0x111000 + 16 + u32(rom, 0x111000 + 8); }

/** Remove the menu (program, catalog, slots, stub). Call feldSlots() first if the saves should be kept. */
function feldStrip(rom, emb, E) {
  const out = new Uint8Array(rom);
  const f = feldFind(out, emb.stub);
  if (!f) return out;
  const tab = E.sceneTable(out);
  const prev = f.stubOff === STUB_OFF ? E.installedOrigs(out, tab) : [0, 1, 2, 3].map((k) => u32(out, f.stubOff + 0x94 + 4 * k));
  // Program and catalog
  let end = f.off + (f.version >= 2 ? 0x8000 : 0x4000);
  if (f.catN && f.catOff > f.off) {
    let ce = f.catOff + 8 + (f.version >= 3 ? 28 : 24) * f.catN;
    if (u32(rom, ce) === EXT_MAGIC) ce += 4 + 12 * f.catN;
    end = Math.max(end, page(ce));
  }
  if (f.slotOff > f.off && f.slotOff < end) end = f.slotOff;
  end = Math.min(end, f.stubOff === STUB_OFF && f.off < FELD_END ? FELD_END : (isSix(out) ? CODE_END : 0x110000));
  out.fill(0xFF, f.off, end);
  // Slots: never erase into the character data
  if (f.nslots && f.slotSize) {
    if (f.slotOff2 && f.slotN1) {
      for (let i = 0; i < f.nslots; i++) { const b = slotAddr(f, i); if (b >= page(assetEnd(out)) || b < 0x111000) out.fill(0xFF, b, b + f.slotSize); }
    } else {
      const from = f.slotOff < 0x800000 ? f.slotOff : Math.max(f.slotOff, page(assetEnd(out)));
      out.fill(0xFF, from, f.slotOff + f.nslots * f.slotSize);
    }
  }
  if (prev) {
    if (f.stubOff === STUB_OFF && appKind(out) && emb.oldStub) return E.installApp(out, out.slice(APP_OFF, APP_OFF + 0x10000), emb.oldStub);
    prev.forEach((v, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, v));
    out.fill(0xFF, f.stubOff, f.stubOff + 0x1000);
  }
  return out;
}

/**
 * Install the menu into an image without a menu (after emulator/DOOM).
 * opts: { fields, bag }, saved: feldSlots() of the source ROM,
 * emb: { feld, stub } (embedded), E: ParamixEmu. Returns a new image.
 */
function feldApply(rom, opts, saved, emb, E) {
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB');
  const out = new Uint8Array(rom);
  if (feldFind(out, emb.stub)) throw new Error('the menu is already built in');
  const ed = edition(out);
  if (!ed) throw new Error('edition not recognised');
  const P = profileOf(out, ed.profile);
  const fields = !!opts.fields, bag = !!opts.bag;
  if (!fields && !bag) throw new Error('nothing chosen');
  const tab = E.sceneTable(out);
  if (!tab.PSID_HOME) throw new Error('home screen not found');
  const prev = E.installedOrigs(out, tab);
  const kind = appKind(out);
  const stubOff = prev || kind ? STUB_OFF : freeStubOff(out);
  if (!prev && !isFF(out, stubOff, 0x1000)) throw new Error('no room for the launcher');
  const catList = bag ? itemCatalog(out) : [];
  const cat = bag ? catalogBlob(catList) : new Uint8Array(0);
  const fsz = page(emb.feld.length), csz = page(cat.length);
  const doom = kind === 1;
  const prog = doom ? doomCodeEnd(out) : stubOff + 0x1000;
  const catOff = cat.length ? prog + fsz : 0;
  const nslots = fields ? P.ids.length : 0;
  let slotOff = 0, slotSize = 0, slotOff2 = 0, slotN1 = 0;
  if (doom) {
    if (prog + fsz + csz > DOOM_SAVE) throw new Error('no room for the menu next to DOOM');
    if (fields) {
      slotOff = prog + fsz + csz;
      slotSize = Math.floor((DOOM_SAVE - slotOff) / nslots / 0x1000) * 0x1000;
      if (slotSize < 0x8000) throw new Error('no room for the saves next to DOOM');
    }
  } else {
    if (prog + fsz + csz > (stubOff === STUB_OFF ? FELD_END : CODE_END)) throw new Error('menu program too big');
    if (!isFF(out, prog, fsz + csz)) throw new Error('the area for the menu is in use');
    if (fields && P.six) {
      // six fields: slots behind the menu (up to the helper page) and behind the character data
      const a1 = page(prog + fsz + csz), r1 = CODE_END - a1, a2 = page(assetEnd(out)), r2 = P.idPage - 0x1000 - a2;
      for (let s = SLOT_SIZE; s >= 0x10000; s -= 0x1000) {
        const n1 = Math.max(0, Math.floor(r1 / s)), n2 = Math.max(0, Math.floor(r2 / s));
        if (n1 + n2 >= nslots) { slotSize = s; slotN1 = Math.min(n1, nslots); slotOff = slotN1 ? a1 : a2; slotOff2 = slotN1 && slotN1 < nslots ? a2 : 0; if (!slotN1) slotN1 = 0; break; }
      }
      if (!slotSize) throw new Error('no room for the saves');
    } else if (fields) {
      slotOff = Math.max(SLOT_BASE, page(assetEnd(out)));
      const limit = kind === 2 ? GUEST_OFF : P.idPage - 0x1000;
      slotSize = Math.min(SLOT_SIZE, Math.floor((limit - slotOff) / nslots / 0x1000) * 0x1000);
      if (slotSize < 0x10000) throw new Error('no room for the saves');
    }
  }
  const fs = { slotOff, slotSize, slotOff2, slotN1 };
  if (fields) for (let i = 0; i < nslots; i++) if (!isFF(out, slotAddr(fs, i), slotSize)) throw new Error('the area for the saves is in use');
  // Program with settings
  const f = new Uint8Array(emb.feld);
  const ci = findWord(f, FELD_MAGIC);
  if (ci < 0 || u32(f, ci + 4) < 3) throw new Error('settings block of the menu missing');
  w32(f, ci + 8, slotOff); w32(f, ci + 12, slotSize); w32(f, ci + 16, nslots);
  w32(f, ci + 20, P.idPage + 0x1000); w32(f, ci + 24, P.six ? 0xF00000 : 0x1000000);   // six fields: rain pictures from 0xF00000 w32(f, ci + 28, P.idPage);
  w32(f, ci + 32, stubOff); w32(f, ci + 36, (fields ? F_FIELDS : 0) | (bag ? F_BAG : 0));
  w32(f, ci + 40, P.c); w16(f, ci + 44, P.h[0]); w16(f, ci + 46, P.h[1]);
  w32(f, ci + 48, P.ids.length);
  for (let i = 0; i < 4; i++) {
    w32(f, ci + 52 + 4 * i, P.ids[i] || 0);
    const nm = P.names[i] || '';
    for (let k = 0; k < 12; k++) f[ci + 68 + 12 * i + k] = k < nm.length ? nm.charCodeAt(k) : 0;
    w32(f, ci + 116 + 4 * i, P.cols[i] || 0xFFFF);
  }
  for (let k = 0; k < 16; k++) { const p = P.common[k]; w16(f, ci + 132 + 4 * k, p ? p[0] : 0); f[ci + 134 + 4 * k] = p ? p[1] : 0; f[ci + 135 + 4 * k] = 0; }
  for (let i = 0; i < 4; i++) for (let k = 0; k < 4; k++) {
    const p = (P.field[i] || [])[k], o = ci + 196 + 16 * i + 4 * k;
    w16(f, o, p ? p[0] : 0); f[o + 2] = p ? p[1] : 0; f[o + 3] = 0;
  }
  w32(f, ci + 260, P.bag[0]); w32(f, ci + 264, P.bag[1]); w32(f, ci + 268, P.bag[2]);
  w32(f, ci + 272, catOff); w32(f, ci + 276, cat.length ? u32(cat, 4) : 0);
  w32(f, ci + 280, P.fieldByte); w32(f, ci + 284, bag && catList.pack > 0 ? catList.pack : 0);
  if (P.six) {
    if (u32(f, ci + 4) < 4) throw new Error('menu program too old for six fields');
    w32(f, ci + 288, P.six);
    for (let i = 4; i < 6; i++) {
      w32(f, ci + 292 + 4 * (i - 4), P.ids[i] || 0);
      const nm = P.names[i] || '';
      for (let k = 0; k < 12; k++) f[ci + 300 + 12 * (i - 4) + k] = k < nm.length ? nm.charCodeAt(k) : 0;
      w32(f, ci + 324 + 4 * (i - 4), P.cols[i] || 0xFFFF);
      for (let k = 0; k < 4; k++) { const p = (P.field[i] || [])[k], o = ci + 332 + 16 * (i - 4) + 4 * k; w16(f, o, p ? p[0] : 0); f[o + 2] = p ? p[1] : 0; f[o + 3] = 0; }
    }
    w32(f, ci + 364, slotOff2); w32(f, ci + 368, slotOff2 ? slotN1 : 0);
    P.sixMap.forEach((v, k) => { f[ci + 372 + k] = v; });
  }
  out.set(f, prog);
  if (cat.length) out.set(cat, catOff);
  // carry over saved slots (the active field lives in the save itself)
  if (fields) for (const s of saved || []) {
    const i = s.index;
    if (i < 0 || i >= nslots) continue;
    if (s.data.length > slotSize) throw new Error('the stored save of ' + cap(P.names[i]) + ' does not fit here');
    const d = new Uint8Array(s.data); w32(d, 4, i + 1);          // new slot ID: field number + 1
    out.set(d, slotAddr(fs, i));
  }
  // Stub
  const origs = prev || [0, 1, 2, 3].map((k) => u32(out, tab.PSID_HOME + 12 + 4 * k));
  const st = new Uint8Array(emb.stub);
  const put = (ph, v) => { let n = 0; for (let i = 0; i + 4 <= st.length; i += 2) if (u32(st, i) === ph) { w32(st, i, v); n++; } if (n !== 1) throw new Error('launcher template does not fit'); };
  put(PH_APP, kind ? xip(APP_OFF) : 0);
  put(PH_FELD, xip(prog));
  PH_ORIG.forEach((ph, k) => put(ph, origs[k]));
  { const ia = findWord(st, PH_AUTO); if (ia >= 0) w32(st, ia, 0); }   // autostart off, the Studio sets it at the end
  out.fill(0xFF, stubOff, stubOff + 0x1000);
  out.set(st, stubOff);
  HOOK_OFF.forEach((h, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, (xip(stubOff) + h) | 1));
  return out;
}

const FELD_B64 = '__FELD_B64__', FELD_STUB_B64 = '__FELD_STUB_B64__';
function embedded() {
  if (FELD_B64.startsWith('__')) return null;
  return { feld: b64(FELD_B64), stub: b64(FELD_STUB_B64) };
}

root.ParamixFeld = { isSix, slotAddr, textTabelle, zeichenkette, edition, feldId, canFields, fieldNames, fieldNow, feldFind, feldSlots, feldStrip, feldApply, appKind, itemCatalog, embedded, NAMES, F_FIELDS, F_BAG };
if (typeof module !== 'undefined') module.exports = root.ParamixFeld;
})(typeof window !== 'undefined' ? window : globalThis);
