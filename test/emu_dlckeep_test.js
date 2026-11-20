// 1.7.6 DLC_KEEP: a download item for the main TamaEmu that needs a new block changes the layout. The pet's save on the
// device (guest flash and run state) has to stay: the Studio reads the device's guest, puts its item stores in and keeps
// the session. Same steps as the write button in app.js.
// node test/emu_dlckeep_test.js <paradise.bin> <ps_firmware.bin>
const fs = require('fs');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const E = require('../src/emu.js'), F = require('../src/feld.js'), FL = require('../src/flash.js'), SV = require('../src/saves.js');
const rom0 = new Uint8Array(fs.readFileSync(process.argv[2])), fw = new Uint8Array(fs.readFileSync(process.argv[3]));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../emu/emu.bin'));
const stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
(async () => {
  // the pet's save sits in the guest flash: a block with data in the firmware
  const saveAt = 0x7C0000 + 0x1000, guest = new Uint8Array(fw); guest.fill(0x41, saveAt, saveAt + 0x100);
  const built = E.emuPatch(rom0, guest, app, stub).rom;
  const cfg0 = SV.emuCfg(built.subarray(0xA0000, 0xA1000)); ok(cfg0 && cfg0.dev === 'ps', 'P\'s as the main program');
  // the user reads the ROM, then plays on: new purchases in the guest and a run state on the device
  const dev = new Uint8Array(built);
  const g0 = await SV.emuGuestAsync((a, n) => dev.subarray(a, a + n), cfg0);
  ok(g0[saveAt] === 0x41, 'read: the save is in the guest');
  { const k = cfg0.map[saveAt >> 16]; const at = cfg0.guestOff + ((k === 0xFE ? saveAt >> 16 : k) << 16) + (saveAt & 0xFFFF); dev.fill(0x42, at, at + 0x100); }
  dev.fill(0x33, cfg0.saveOff, cfg0.saveOff + 0x2000);
  // Studio: a clothing item into the empty store (a new block)
  const rec = new Uint8Array(0x400).fill(0); rec.set([0x54, 0x41, 0x4D, 0x41, 0x47, 0x4F]); rec[0x4E] = 0x81; rec[0x50] = 0x33; rec[0x4B] = 0x00; rec[0x4A] = 0x04; rec[0xFF] = 2;
  const st = E.dlcStores(g0).find((k) => k.kind === 2); ok(st && st.slots.every((x) => !x.name), 'clothing store empty');
  const r = E.dlcAdd(g0, rec); const gS = r.guest;
  const nb0 = E.packGuest(g0, 'ps').blocks, nb1 = E.packGuest(gS, 'ps').blocks; ok(nb1 !== nb0, 'the item needs a new block (' + nb0 + ' -> ' + nb1 + ')');
  // write button: device guest, Studio stores in, new image
  const dcfg = SV.emuCfg(dev.subarray(0xA0000, 0xA1000));
  const gd = await SV.emuGuestAsync((a, n) => dev.slice(a, a + n), dcfg);
  ok(gd[saveAt] === 0x42, 'the device\'s newer save is read');
  const g = new Uint8Array(gd); for (const [a, z] of E.dlcAreas('ps')) g.set(gS.subarray(a, z), a);
  const img = E.emuPatch(rom0, g, app, stub).rom;
  ok(!img.subarray(0xA0148, 0xA0140 + 152).every((v, i) => v === dev[0xA0148 + i]), 'the layout changed');
  const SEC = 0x1000, changed = new Set(); for (let s = 0; s < img.length; s += SEC) for (let i = s; i < s + SEC; i++) if (img[i] !== dev[i]) { changed.add(s); break; }
  const ed = F.edition(img);
  const plan = FL.keepPlan(F, E, null, { emuSess: true });
  const res = await plan({ info: { edition: ed && ed.id, name: 'x' }, read: async (a, n) => dev.slice(a, a + n) }, img, changed);
  const out = res.image, cfg = SV.emuCfg(out.subarray(0xA0000, 0xA1000));
  const go = await SV.emuGuestAsync((a, n) => out.subarray(a, a + n), cfg);
  ok(go[saveAt] === 0x42 && go[saveAt + 0xFF] === 0x42, 'written: the device\'s save stays');
  ok(E.dlcStores(go).find((k) => k.kind === 2).slots[0].name !== null, 'written: the item is there');
  ok(out.subarray(cfg.saveOff, cfg.saveOff + 0x2000).every((v) => v === 0x33), 'written: the run state stays ' + JSON.stringify(res.notes) + ' ' + cfg.saveOff.toString(16) + ' ' + dcfg.saveOff.toString(16) + ' ' + out[cfg.saveOff]);
  // without the switch (old way): the save of the read ROM, no run state
  const plan0 = FL.keepPlan(F, E, null, { emuChanged: true });
  const img0 = E.emuPatch(rom0, gS, app, stub).rom; const ch0 = new Set(); for (let s = 0; s < img0.length; s += SEC) for (let i = s; i < s + SEC; i++) if (img0[i] !== dev[i]) { ch0.add(s); break; }
  const r0 = await plan0({ info: { edition: ed && ed.id, name: 'x' }, read: async (a, n) => dev.slice(a, a + n) }, img0, ch0);
  const c0 = SV.emuCfg(r0.image.subarray(0xA0000, 0xA1000)), gz = await SV.emuGuestAsync((a, n) => r0.image.subarray(a, a + n), c0);
  ok(gz[saveAt] === 0x41, 'before: only the save of the read ROM (purchases since then gone)');
  // 1.7.6 PS_DL2: a stamp card. Real P's flash with a save (argv[4]): the save byte goes into the guest from the device
  // and into the save copy of the run state, the rest of the run state stays
  if (process.argv[4]) {
    const fwS = new Uint8Array(fs.readFileSync(process.argv[4])), devS = new Uint8Array(E.emuPatch(rom0, fwS, app, stub).rom);
    const cS = SV.emuCfg(devS.subarray(0xA0000, 0xA1000)), so = cS.saveOff, save = fwS.slice(0xE000, 0xE4E4);
    // run state: as state.c writes it, the save copy somewhere in the RAM part
    const emu = new Uint8Array(0xFE88); for (let i = 0; i < emu.length; i++) emu[i] = (i * 13) & 0x7F; emu.set(save, 0x198C + 0x12A8);
    const raw = new Uint8Array(emu.length + 32768); raw.set(emu); raw.fill(9, emu.length);
    const out = []; const enc = (p) => { let i = 0; while (i < p.length) { let r = 1; while (i + r < p.length && r < 130 && p[i + r] === p[i]) r++; if (r >= 3) { out.push(0x80 + r - 3, p[i]); i += r; continue; } const s0 = i; let l = 0; while (i < p.length && l < 128) { let q = 1; while (i + q < p.length && q < 3 && p[i + q] === p[i]) q++; if (q >= 3) break; i++; l++; } out.push(l - 1); for (let k = 0; k < l; k++) out.push(p[s0 + k]); } };
    enc(emu); enc(raw.subarray(emu.length));
    const w = (o, x) => { devS[o] = x & 255; devS[o + 1] = (x >>> 8) & 255; devS[o + 2] = (x >>> 16) & 255; devS[o + 3] = (x >>> 24) & 255; }, ns = Math.ceil(out.length / 0x1000);
    devS.fill(0xFF, so, so + 0x50000); w(so, 0x53584D50); w(so + 4, 3); w(so + 8, 77); w(so + 12, 5); w(so + 16, out.length); w(so + 20, ns); w(so + 24, (0x53584D50 ^ 3 ^ 77 ^ 5 ^ out.length ^ ns) >>> 0); devS.set(out, so + 0x1000);
    const card = '/tmp/priv/psdl/P\'s Stamp Cards/Christmas%20stamp.jpg';
    const gS0 = await SV.emuGuestAsync((a, n) => devS.subarray(a, a + n), cS), rS = E.dlcAdd(gS0, E.dlcRecord(new Uint8Array(fs.readFileSync(card)), 'ps'));
    ok(rS.stamp && E.psStampGet(rS.guest) === 0x0C, 'stamp card: the Studio guest has the card');
    // write button (DLC_KEEP): device guest + stores + the stamp byte
    const gD = new Uint8Array(await SV.emuGuestAsync((a, n) => devS.slice(a, a + n), cS));
    for (const [a, z] of E.dlcAreas('ps')) gD.set(rS.guest.subarray(a, z), a); E.psStamp(gD, 0x0C);
    const imgS = E.emuPatch(rom0, gD, app, stub).rom, chS = new Set(); for (let s = 0; s < imgS.length; s += SEC) for (let i = s; i < s + SEC; i++) if (imgS[i] !== devS[i]) { chS.add(s); break; }
    const after = async (im) => { const c = SV.emuCfg(im.subarray(0xA0000, 0xA1000)), g = await SV.emuGuestAsync((a, n) => im.subarray(a, a + n), c); E.psSessStamp(im, c.saveOff, g.subarray(0xE000, 0xE4E4), E.psStampGet(g)); };
    const rP = await FL.keepPlan(F, E, null, { emuSess: true, after })({ info: { edition: ed && ed.id, name: 'x' }, read: async (a, n) => devS.slice(a, a + n) }, imgS, chS);
    const oS = rP.image, cO = SV.emuCfg(oS.subarray(0xA0000, 0xA1000)), gO = await SV.emuGuestAsync((a, n) => oS.subarray(a, a + n), cO);
    ok(E.psStampGet(gO) === 0x0C && gO[0xE4E4 + 0x4B] === 0x0C, 'stamp card: both save copies in the guest flash');
    ok(gO.subarray(0x740000, 0x740006).every((v, i) => v === 'TAMAGO'.charCodeAt(i)), 'stamp card: the card is in its store');
    const u = (o) => (oS[o] | (oS[o + 1] << 8) | (oS[o + 2] << 16) | (oS[o + 3] << 24)) >>> 0, src = oS.subarray(cO.saveOff + 0x1000, cO.saveOff + 0x1000 + u(cO.saveOff + 16)), back = [];
    for (let i = 0; i < src.length;) { const c = src[i++]; if (c & 0x80) { const n = c - 0x80 + 3, b = src[i++]; for (let k = 0; k < n; k++) back.push(b); } else for (let k = 0; k <= c; k++) back.push(src[i++]); }
    const at = 0x198C + 0x12A8 + 0x4B;
    ok(back.length === raw.length && back[at] === 0x0C && back.every((v, i) => i === at || v === raw[i]), 'stamp card: the run state stays, with the card');
    ok(rP.protectedSectors.length > 0 && chS.has(cO.saveOff + 0x1000), 'stamp card: the run state gets written');
  }
  console.log(bad ? bad + ' WRONG' : 'all good'); process.exit(bad ? 1 : 0);
})();
