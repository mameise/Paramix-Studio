// Optional local integration test. User-supplied ROMs are never committed.
// node test/feld_preview_rom_test.js <paradise.bin> [...]
// Optional PARADISE_GHOST_DIR checks original ratchi/okojotchi exports as
// independent palette evidence; the repository includes no ghost/game data.
const fs = require('fs'), os = require('os'), path = require('path'), assert = require('assert');
const { execFileSync } = require('child_process');
const C = require('../src/core.js'), F = require('../src/feld.js'), E = require('../src/emu.js');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'paramix-preview-rom-'));
const emb = { feld: new Uint8Array(fs.readFileSync(path.join(__dirname, '../feld/feld.bin'))),
  stub: new Uint8Array(fs.readFileSync(path.join(__dirname, '../feld/stub_feld_tpl.bin'))) };
try {
  const bin = path.join(tmp, 'preview-test');
  execFileSync(process.env.CC || 'cc', ['-w', '-fsanitize=address,undefined', '-g', path.join(__dirname, 'feld_preview_test.c'), '-o', bin]);
  for (const file of process.argv.slice(2)) {
    const rom = new Uint8Array(fs.readFileSync(file)), ctx = C.openRom(rom);
    const installed = F.feldApply(rom, { fields: true, bag: true, tama: true }, [], emb, E), cfg = F.feldFind(installed, emb.stub);
    assert.deepEqual(F.feldStrip(installed, emb, E), rom, 'install/strip must restore every byte');
    const again = F.feldApply(F.feldStrip(installed, emb, E), { fields: true, bag: true, tama: true }, [], emb, E);
    assert.deepEqual(again, installed, 'reinstallation must rebuild identical references');
    const chars = C.charTable(ctx.files[0]).filter(c => c.id > 1000 && c.id < 9000);
    const group = new Map(chars.map((c, i) => [c.id, ctx.groups[i + 1]]));
    const ids = F.characters(rom).map(c => c.id);
    const variant = ctx.entries.find(e => { const h = C.spriteHeader(e); return h.numPal === 16 && h.bpp === 2 && h.sw === 64 && h.sh === 64 && h.numSprites === 1; });
    const palettes = C.spriteDecode(variant).palettes;
    const source = path.join(tmp, 'rom.bin'), out = path.join(tmp, 'fb.bin'); fs.writeFileSync(source, installed);
    const cases = ids.map(id => [id, id, 16]);
    for (const body of [ids[0], ids[2], ids[ids.length - 1]]) for (let color = 0; color < 16; color++) cases.push([body, ids[ids.length - 2], color]);
    // These two actual eye resources place dark outline 0x000C in indices
    // 7/6. Applying the body's white variant would turn their eyes white.
    const darkEyes = new Map([[4021, 7], [4119, 6]]);
    const whiteBody = ids.includes(4119) ? 4119 : ids[ids.length - 1];
    for (const id of darkEyes.keys()) if (ids.includes(id)) cases.push([whiteBody, id, 14]);
    if (process.env.PARADISE_GHOST_DIR) {
      for (const [name, id, color] of [['ratchi.bin', 4021, 9], ['okojotchi.bin', 4119, 14]]) {
        const filename = path.join(process.env.PARADISE_GHOST_DIR, name);
        if (!group.has(id) || !fs.existsSync(filename)) continue;
        const ghost = fs.readFileSync(filename);
        assert.equal(ghost.readUInt16LE(14), id); assert.equal(ghost[16], color);
        const at = ghost.readUInt32LE(0x118), len = ghost.readUInt32LE(0x11C);
        const exportPalette = C.spriteDecode(new Uint8Array(ghost.subarray(at, at + len))).palettes[0];
        const romPalette = C.spriteDecode(ctx.entries[group.get(id) + 1]).palettes[0];
        assert.deepEqual(exportPalette, romPalette, `${name}: firmware/export eyes retain their own palette`);
        assert.equal(exportPalette[darkEyes.get(id)], 0x000C, `${name}: body color must not replace dark eyes`);
      }
    }
    for (const [body, eyes, color] of cases) {
      execFileSync(bin, [source, String(cfg.off + 0x140), String(body), String(eyes), String(color), out, tmp]);
      const actual = fs.readFileSync(out), expected = Buffer.alloc(32768);
      for (let i = 0; i < 16384; i++) expected.writeUInt16LE(0x1085, i * 2); // RGB(16,16,40)
      const g = group.get(body), e = group.get(eyes), pose = C.faceFirst(ctx.files[0], g, g + 1, g + 2), ep = C.faceFirst(ctx.files[0], e, e + 1, e + 2);
      const part = (index, sub, dx, dy, tint = true) => {
        const entry = ctx.entries[index].slice(), h = C.spriteHeader(entry);
        if (tint && color < 16) for (let i = 2; i <= 7; i++) if (entry[h.palOffset + 2 * i] || entry[h.palOffset + 2 * i + 1]) {
          const v = palettes[color][i]; entry[h.palOffset + 2 * i] = v & 255; entry[h.palOffset + 2 * i + 1] = v >> 8;
        }
        const image = C.spriteDecode(entry).subs[sub];
        for (let y = 0; y < image.h; y++) for (let x = 0; x < image.w; x++) {
          const o = (y * image.w + x) * 4, X = x + dx, Y = y + dy;
          if (!image.data[o + 3] || X < 32 || X >= 96 || Y < 34 || Y >= 98) continue;
          const v = ((image.data[o] >> 3) << 11) | ((image.data[o + 1] >> 2) << 5) | (image.data[o + 2] >> 3);
          expected.writeUInt16LE(v, (Y * 128 + X) * 2);
        }
      };
      part(g, pose.bs, 32, 34);
      part(e + 1, ep.eye.sub, 32 + pose.eye.dx, 50 + pose.eye.dy, false);
      part(e + 2, ep.mouth.sub, 32 + pose.mouth.dx, 50 + pose.mouth.dy);
      assert(actual.equals(expected), `pixel mismatch: ${path.basename(file)} body=${body} eyes=${eyes} color=${color}`);
      if (color === 14 && darkEyes.has(eyes)) {
        const eye = C.spriteDecode(ctx.entries[e + 1]), index = darkEyes.get(eyes), pixels = eye.cells[ep.eye.sub];
        assert.equal(eye.palettes[0][index], 0x000C);
        let visibleDarkEyes = 0;
        pixels.forEach((v, i) => {
          if (v !== index) return;
          const x = 32 + pose.eye.dx + i % 64, y = 50 + pose.eye.dy + Math.floor(i / 64);
          if (x < 32 || x >= 96 || y < 34 || y >= 98) return;
          const off = (y * 128 + x) * 2;
          // Ignore eye pixels legitimately covered by the later mouth layer.
          if (expected.readUInt16LE(off) !== 0x000C) return;
          assert.equal(actual.readUInt16LE(off), 0x000C, 'white body erased a dark eye pixel'); visibleDarkEyes++;
        });
        assert(visibleDarkEyes > 0, `white body must keep visible dark eye pixels for ${eyes}`);
      }
    }
    if (process.env.PREVIEW_OUT) fs.copyFileSync(path.join(tmp, '000.ppm'), path.join(process.env.PREVIEW_OUT, path.basename(file) + '.ppm'));
    console.log(`PASS ${path.basename(file)}: ${cases.length} pixel-exact previews, all selectable characters, 16 colors, mixed faces, install/strip/reinstall`);
  }
} finally { fs.rmSync(tmp, { recursive: true, force: true }); }
