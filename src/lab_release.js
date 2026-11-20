/* Optional in-game Lab Invite sender for Paradise editions 1–6. */
(() => {
  const root = typeof window === 'undefined' ? globalThis : window;
  const C = root.ParamixCore || require('./core.js');
  const Home = root.ParamixHomeMenu || require('./home_menu.js');
  const helper = typeof module === 'undefined' ? '__LAB_RELEASE_HELPER__' : require('../six/my-lab/helper.json');
  const pageSize = 0x1000;
  const signature = [0x50, 0x4d, 0x58, 0x4c, 0x41, 0x42, 0x02, 0x00]; // PMXLAB, version 2
  const labels = [83, 288, 277]; // Release entry, confirmation, and connect instructions
  const read16 = (bytes, at) => bytes[at] | (bytes[at + 1] << 8);
  const word = (bytes, at, value) => { bytes[at] = value & 255; bytes[at + 1] = value >>> 8; };
  const hex = value => Uint8Array.from(value.match(/../g), byte => parseInt(byte, 16));
  const same = (bytes, at, expect) => at >= 0 && at + expect.length <= bytes.length &&
    expect.every((value, index) => value === bytes[at + index]);
  const blank = (bytes, start, end) => start >= 0 && end <= bytes.length &&
    bytes.subarray(start, end).every(value => value === 255);
  const hookNames = [
    'connection_row', 'connection_selection', 'allocate_scene', 'release_dialog', 'release_confirmation',
    'release_link_ready', 'initialize_link', 'send_body', 'send_body', 'status_after_body', 'status_after_body',
  ];
  // Entry offsets in the first PMXLAB v2 helper. The text manifest stays v2
  // so language patches can continue to recognize the existing three rows.
  const oldEntries = {
    1: [0x20, 0, 92, 136, 176, 264, 300, 440, 440, 468, 468, 328, 356],
    4: [0x20, 0, 92, 136, 176, 264, 300, 356, 356, 384, 384, 328],
    5: [0x20, 0, 92, 136, 176, 264, 300, 328, 328, 356, 356],
  };
  const oldSizes = { 1: 868, 4: 784, 5: 748 };
  // Addresses are offsets from the executable's XIP base.
  // Each entry has the required original bytes and any immediate replacement.
  const profiles = {
    1: { rowCount: 4, constructor: 0x27170,
      simple: [
        [0x5745c, '6020', '6c20'],
        [0x75de2, '0421', '0421'],
        [0x57028, '9020', '9c20'],
      ], hooks: [
        [0x75df4, 'b1f7bcf9'],
        [0x75e44, 'b1f7dcf9'],
        [0x5702c, 'bff794f9'],
        [0x74354, 'abf7a0fe'],
        [0x743f6, '90f7cffc'],
        [0x743aa, 'e0f713fe'],
        [0x74374, 'e0f7a6fd'],
        [0x745c2, 'e0f767fd'],
        [0x746dc, 'e0f7dafc'],
        [0x74628, 'e0f7b2fc'],
        [0x74736, 'e0f72bfc'],
        [0x6fa34, '41eac301', 'release_packet_type'],
        [0x76546, '02f035f9', 'release_ghost_chunk'],
      ],
    },
    4: { rowCount: 4, constructor: 0x28f90,
      simple: [
        [0x5a808, '6020', '6c20'],
        [0x7a23e, '0421', '0421'],
        [0x5a3d2, '9020', '9c20'],
      ], hooks: [
        [0x7a250, 'aef79efe'],
        [0x7a2a0, 'aef7befe'],
        [0x5a3d6, 'bcf73ff9'],
        [0x787b0, 'a8f714fe'],
        [0x78852, '8cf727fb'],
        [0x78806, 'dff775fd'],
        [0x787d0, 'dff708fd'],
        [0x78a1e, 'dff7c9fc'],
        [0x78b38, 'dff73cfc'],
        [0x78a84, 'dff714fc'],
        [0x78b92, 'dff78dfb'],
        [0x73aa8, '41eac301', 'release_packet_type'],
      ],
    },
    5: { rowCount: 5, constructor: 0x2d5c4,
      simple: [
        [0x6389c, '6c20', '7820'],
        [0x8d0ec, '0521', '0521'],
        [0x63466, '9020', '9c20'],
      ], hooks: [
        [0x8d0f8, 'a0f764fa'],
        [0x8d148, 'a0f784fa'],
        [0x6346a, 'b5f75dfd'],
        [0x8b5fc, '99f72efd'],
        [0x8b69e, '7af703fc'],
        [0x8b652, 'd5f7b9f9'],
        [0x8b61c, 'd5f734f9'],
        [0x8b86a, 'd5f701fa'],
        [0x8b984, 'd5f774f9'],
        [0x8b8d0, 'd5f758f8'],
        [0x8b9de, 'd4f7d1ff'],
      ],
    },
  };
  for (const [id, profile] of Object.entries(profiles)) {
    profile.payload = helper[id];
    profile.oldSize = oldSizes[id];
    profile.simple = profile.simple.map(([at, before, after]) => [at + 0x11000, hex(before), hex(after)]);
    profile.hooks = profile.hooks.map(([at, before, hook], index) =>
      [at + 0x11000, hex(before), hook || hookNames[index], oldEntries[id][index]]);
  }

  function profileOf(rom) {
    const edition = C.editionIdOf(rom);
    return profiles[edition && (edition < 4 ? 1 : edition > 4 && edition < 7 ? 5 : edition)];
  }

  function branch(from, to, link = true) {
    const delta = to - from - 4, sign = (delta >>> 24) & 1;
    const j1 = 1 ^ ((delta >>> 23) & 1) ^ sign, j2 = 1 ^ ((delta >>> 22) & 1) ^ sign;
    const out = new Uint8Array(4);
    word(out, 0, 0xf000 | (sign << 10) | ((delta >>> 12) & 1023));
    word(out, 2, (link ? 0xd000 : 0x9000) | (j1 << 13) | (j2 << 11) | ((delta >>> 1) & 2047));
    return out;
  }

  function movw(bytes, at, register, value) {
    word(bytes, at, 0xf240 | (((value >>> 11) & 1) << 10) | (value >>> 12));
    word(bytes, at + 2, (((value >>> 8) & 7) << 12) | (register << 8) | (value & 255));
  }

  function replaceTextID(bytes, marker, value) {
    let count = 0;
    for (let at = 0; at + 4 <= bytes.length; at += 2) {
      const first = read16(bytes, at), second = read16(bytes, at + 2);
      if ((first & 0xfbf0) !== 0xf240 || (second & 0x8000)) continue;
      const immediate = ((first & 15) << 12) | (((first >>> 10) & 1) << 11) |
        (((second >>> 12) & 7) << 8) | (second & 255);
      if (immediate !== marker) continue;
      movw(bytes, at, (second >>> 8) & 15, value);
      count++;
    }
    if (count !== 1) throw new Error('invalid Lab Invite program');
  }

  function patchedCall(site, page, entry, profile) {
    const offset = entry === 'connection_row' ? 0x20 : 0x80 + profile.payload.entries[entry];
    return branch(site, page + offset);
  }

  function findInstalled(rom, legacy) {
    const profile = profileOf(rom);
    if (!profile) return null;
    for (let page = 0x10e000; page >= 0x90000; page -= pageSize) {
      if (same(rom, page, signature) &&
          profile.simple.every(([at, , bytes]) => same(rom, at, bytes)) &&
          profile.hooks.every(([at, , entry, oldOffset]) => same(rom, at, legacy
            ? branch(at, page + (entry === 'connection_row' ? oldOffset : 0x80 + oldOffset))
            : patchedCall(at, page, entry, profile)))) return { page, profile };
    }
    return null;
  }
  const installed = rom => !!findInstalled(rom, false);
  const needsUpgrade = rom => !!findInstalled(rom, true);

  function supported(rom) {
    const profile = profileOf(rom);
    return !!profile && [...profile.simple, ...profile.hooks].every(([at, bytes]) => same(rom, at, bytes));
  }

  function addText(rom, donor, reserved) {
    const ctx = C.openRom(rom), src = C.textTablesOf(rom), from = C.textTablesOf(donor);
    if (!src || !from || src.tabs.length !== 9 || from.tabs.length !== 9 || C.textGet(donor, 83) !== 'RELEASE')
      throw new Error('Choose a Neon Planets ROM with its native language tables');
    const label = src.tabs[0].n;
    const tables = src.tabs.map((table, language) => {
      const original = from.tabs[language];
      if (!table.native || table.n !== label || !original.native)
        throw new Error('Unsupported Lab Invite text table');
      const extra = labels.map(id => C.txtWords(original, id));
      if (extra.some(words => !words.length || words.length > 200))
        throw new Error('Neon Planets is missing Lab Invite text');
      const count = label + extra.length, indexShift = 2 * extra.length;
      const output = new Uint8Array(table.b.length + indexShift +
        2 * extra.reduce((size, words) => size + words.length + 1, 0));
      if (output.length / 2 > 65535) throw new Error('Lab Invite text exceeds the language table capacity');
      word(output, 0, count);
      for (let i = 1; i <= label; i++) word(output, 2 * i, table.offs[i] + extra.length);
      const dataOffset = 2 * table.offs[1];
      output.set(table.b.subarray(dataOffset), dataOffset + indexShift);
      let cursor = table.b.length + indexShift;
      extra.forEach((words, i) => {
        word(output, 2 * (label + 1 + i), cursor / 2);
        for (const code of words) { word(output, cursor, code); cursor += 2; }
        cursor += 2;
      });
      word(output, 2 * (count + 1), cursor / 2);
      return output;
    });
    // Text is the last ARC2 file. Keep the sprite addresses and any external
    // rain-cell pointers byte-for-byte intact.
    const entry = ctx.root.ents[2];
    if (entry.flags || src.off + entry.clen !== ctx.assetEnd)
      throw new Error('Unsupported Lab Invite text archive layout');
    const archive = C.arc2Build(tables);
    const end = src.off + archive.length;
    if (end > ctx.limit || (end > ctx.assetEnd && (!blank(rom, ctx.assetEnd, end) ||
        reserved.some(([start, stop]) => start < end && ctx.assetEnd < stop))))
      throw new Error('Lab Invite text overlaps an allocated area');
    const out = new Uint8Array(rom), size = end - C.ASSET_BASE - 16;
    const header = new DataView(out.buffer, C.ASSET_BASE, 64);
    out.set(archive, src.off);
    if (end < ctx.assetEnd) out.fill(255, end, ctx.assetEnd);
    header.setUint32(8, size, true);
    header.setUint32(56, archive.length, true);
    header.setUint32(60, archive.length, true);
    header.setUint32(4, C.arc2Checksum(out, C.ASSET_BASE, size), true);
    C.verifyRom(out, []);
    return { rom: out, label };
  }

  function prepare(rom, donor) {
    if (needsUpgrade(rom)) return { rom: upgrade(rom), label: null };
    if (installed(rom)) return { rom, label: null };
    if (!supported(rom)) throw new Error('This version of the firmware has no Lab Invite profile');
    if (!donor || C.editionIdOf(donor) !== 7) throw new Error('Choose a Neon Planets ROM for the in-game text');
    return addText(rom, donor, Home.reservations(rom));
  }

  function install(rom, label) {
    if (label === null) return rom;
    if (!supported(rom)) throw new Error('This version of the firmware has no Lab Invite profile');
    const profile = profileOf(rom);
    const reserved = Home.reservations(rom);
    let page = 0x10e000;
    while (page >= 0x90000 && (!blank(rom, page, page + pageSize) ||
      reserved.some(([start, end]) => start < page + pageSize && page < end))) page -= pageSize;
    if (page < 0x90000) throw new Error('No free firmware page for Lab Invite');
    const out = new Uint8Array(rom);
    writeProgram(out, page, label, profile);

    const row = hex('019b00000000da6300221a645a640023002200920022');
    movw(row, 2, 2, label);
    row[16] = profile.rowCount + 1;
    const rowWord = profile.rowCount * 3;
    for (const [position, index] of [[6, 0], [10, 1], [12, 2]])
      word(row, position, 0x601a | ((rowWord + index) << 6));
    out.set(signature, page);
    const manifest = new DataView(out.buffer, out.byteOffset + page + 8, 12);
    const textRange = label | (labels.length << 16);
    manifest.setUint32(0, textRange, true);
    manifest.setUint32(4, ~textRange, true);
    manifest.setUint32(8, 0, true); // Chinese rows still use the donor ROM's native glyphs.
    out.set(row, page + 0x20);
    out.set(branch(page + 0x20 + row.length, 0x11000 + profile.constructor, false), page + 0x20 + row.length);
    profile.simple.forEach(([at, , bytes]) => out.set(bytes, at));
    return out;
  }

  function writeProgram(out, page, label, profile) {
    const program = hex(profile.payload.hex);
    replaceTextID(program, 0x6fe1, label + 1);
    replaceTextID(program, 0x6fe2, label + 2);
    if (0x80 + program.length > pageSize) throw new Error('Lab Invite code is too large');

    out.set(program, page + 0x80);
    profile.hooks.forEach(([at, , entry]) => out.set(patchedCall(at, page, entry, profile), at));
  }

  function upgrade(rom) {
    const found = findInstalled(rom, true);
    if (!found) return rom;
    const { page, profile } = found;
    const manifest = new DataView(rom.buffer, rom.byteOffset + page + 8, 12);
    const range = manifest.getUint32(0, true), label = range & 0xffff;
    if (range >>> 16 !== labels.length || manifest.getInt32(4, true) !== ~range ||
        !label || manifest.getUint32(8, true) > 1 ||
        !blank(rom, page + 0x80 + profile.oldSize, page + pageSize))
      throw new Error('Invalid installed Lab Invite helper');
    const out = new Uint8Array(rom);
    writeProgram(out, page, label, profile);
    return out;
  }

  function apply(rom, donor) {
    const prepared = prepare(rom, donor);
    return install(prepared.rom, prepared.label);
  }

  root.ParamixLabRelease = { prepare, install, apply, installed, supported, needsUpgrade, upgrade };
  if (typeof module !== 'undefined') module.exports = root.ParamixLabRelease;
})();
