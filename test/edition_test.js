// 1.7.3 test11: edition from the ID page with its footer, Tropical/Glacier first (a Glacier was read as Land when its
// save data at 0xD49000 began with 1..6). node test/edition_test.js <rom.bin...> (the edition is the expected one by name)
const fs = require('fs'), path = require('path'), C = require('../src/core.js'), F = require('../src/feld.js'), FL = require('../src/flash.js');
const E = require('../src/emu.js'), H = require('../src/home_menu.js'), SV = require('../src/saves.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
(async () => {
  for (const f of process.argv.slice(2)) {
    const b = new Uint8Array(fs.readFileSync(f)), want = C.openRom(b).editionId, nm = f.split('/').pop();
    for (const g of [0, 1, 6]) {
      const r = new Uint8Array(b); if (g && want >= 5) { r.fill(0, 0xD49000, 0xD49004); r[0xD49000] = g; }
      const p = Object.create(FL.Paradise.prototype), pay = new Uint8Array(44); pay[8] = 1;
      p.link = { request: async () => ({ payload: pay }) }; p.read = async (o, n) => r.slice(o, o + n);
      const i = await p.hello(), e = F.edition(r);
      ok(C.openRom(r).editionId === want && e && e.id === want && i.edition === want, nm + (g ? ' with ' + g + ' at 0xD49000' : '') + ': ' + FL.EDITIONS[want]);
    }
    if (want === 8) {
      const save = SV.paraDecode(SV.paraEncode(SV.paraFromImage(b, 8), 8, null));
      ok(SV.paraCheck(save, 8).complete && !SV.paraCheck(save, 8).errors.length, 'Neon save makes a complete backup of 0xD86000 through 0xEFFFFF');
      const before = SV.neonProgress(save);
      const edited = SV.neonProgressSet(save, { points: 54321, level: 6, mission: { index: 20, complete: true } });
      const after = SV.neonProgress(edited);
      const changedPages = [...edited.pages].filter(([address, page]) => page !== save.pages.get(address));
      ok(changedPages.length === 1 && changedPages[0][0] === 0xD87000 && SV.settingsOk(changedPages[0][1]), 'Neon edits commit one sealed page after the active generation');
      ok(after.points === 54321 && after.level === 6 && after.missions[20].count === 16 && after.missions[20].complete &&
        JSON.stringify(after.missions[19]) === JSON.stringify(before.missions[19]) &&
        JSON.stringify(after.missions[21]) === JSON.stringify(before.missions[21]), 'Neon balance, level and adjacent planet missions are distinct');
      const reset = SV.neonProgress(SV.neonProgressSet(edited, { mission: { index: 20, complete: false } }));
      ok(reset.missions[20].count === 15 && !reset.missions[20].complete && reset.points === 54321, 'Neon mission reset does not change the balance');
      save.pages.delete(0xD8A000);
      ok(SV.paraCheck(save, 8).errors.some(e => e.code === 'saveNoSettings'), 'Neon restore requires all rotating settings pages');
      const asset = name => new Uint8Array(fs.readFileSync(path.join(__dirname, '..', name)));
      const emb = { feld: asset('feld/feld.bin'), stub: asset('feld/stub_feld_tpl.bin') };
      const bag = F.feldApply(b, { bag: true, neonTama: true }, [], emb, E);
      const bagInfo = F.feldFind(bag, emb.stub), items = F.itemCatalog(b);
      ok(items.length === 393 && items.filter(x => !x.fixed).length === 385 &&
        items.find(x => x.id === 5004)?.max === 10 && items.some(x => x.id >= 6000 && x.id < 7000),
      'Neon item table includes snacks, per-item limits, and fixed entries');
      ok((bagInfo.flags & (F.F_BAG | F.F_NEON_TAMA)) === (F.F_BAG | F.F_NEON_TAMA) &&
        Buffer.from(F.feldStrip(bag, emb, E)).equals(Buffer.from(b)), 'Neon bag and Tama editors coexist with a reversible install');
      const guest = new Uint8Array(0x3000);
      const placed = E.appsPlace(b, [{ kind: 'v1', rom: guest }], { v1: asset('v1/v1.bin') });
      const out = F.feldApply(placed.rom, { appsOnly: true, apps: placed.list }, [], emb, E);
      const info = F.feldFind(out, emb.stub), home = H.inspect(out), apps = E.appsRead(out, F.feldApps(out, emb.stub).apps);
      ok(info && (info.flags & (F.F_APPS | F.F_NEON_EDIT)) === (F.F_APPS | F.F_NEON_EDIT) &&
        !info.nslots && home && home.profileIndex === 3, 'Neon menu installs with its own save tools');
      ok(apps.length === 1 && apps[0].off >= C.openRom(b).assetEnd && apps[0].off < 0xD85000 && Buffer.from(guest).equals(Buffer.from(apps[0].rom)), 'program uses free flash between Neon assets and edition page');
      ok(Buffer.from(out.subarray(0xD85000)).equals(Buffer.from(b.subarray(0xD85000))), 'edition page and game saves unchanged');
      const stripped = E.appsClear(F.feldStrip(out, emb, E), apps);
      ok(Buffer.from(stripped).equals(Buffer.from(b)), 'removing the Programs menu restores the original ROM byte for byte');
      const slotsImg = F.feldApply(b, { fields: true }, [], emb, E);
      const slots = F.saveSlots(slotsImg, emb.stub), [saveStart, saveEnd] = SV.region(8);
      const [slotStart, slotEnd] = F.neonSlotRange();
      ok(slots?.saves && slots.live === 0 && slots.list.length === 3 &&
        C.openRom(slotsImg).limit === slotStart && slotEnd === 0xD85000 &&
        F.fieldNames(slotsImg).join('|') === 'Cosmic Planet|New Tamagotchi Planet|Tamagotchi Planet',
      'Neon installs three slots and reserves space before the active record');
      ok(Buffer.from(slotsImg.subarray(saveStart, saveEnd)).equals(Buffer.from(b.subarray(saveStart, saveEnd))) &&
        Buffer.from(F.feldStrip(slotsImg, emb, E)).equals(Buffer.from(b)), 'Neon switch installer preserves the active save and can remove its boot hook');
      F.slotPlace(slotsImg, slots.f, 1, F.slotBuild(edited.pages, 1, saveStart, saveEnd), new Set());
      const cleared = SV.paraClear(slotsImg, 8);
      ok(cleared.subarray(saveStart, saveEnd).every(byte => byte === 255) &&
        Buffer.from(cleared.subarray(0, saveStart)).equals(Buffer.from(slotsImg.subarray(0, saveStart))) &&
        Buffer.from(cleared.subarray(saveEnd)).equals(Buffer.from(slotsImg.subarray(saveEnd))),
      'clearing the active Neon save leaves the stored slots, edition and rescue area untouched');
      const carried = F.feldSlots(slotsImg, emb.stub);
      const renewed = F.feldApply(F.feldStrip(slotsImg, emb, E), { fields: true }, carried, emb, E);
      const existing = F.feldSlots(renewed, emb.stub);
      const slotPages = SV.paraFromImage(new Uint8Array(0x1000000).fill(255), 8);
      for (const [a, p] of F.slotPages(existing[0])) slotPages.set(a, p);
      ok(existing[0].index === 1 && existing.neonActive === 0 &&
        SV.neonProgress({ edition: 8, pages: slotPages }).points === 54321, 'rebuilding preserves a stored Neon save and the active slot number');
      const journal = new DataView(renewed.buffer, slotStart, 24), magic = 0x4A575350;
      [magic, ~magic, slotStart, ~slotStart, 0, ~0].forEach((v, i) => journal.setUint32(4 * i, v, true));
      let blocked = false;
      try { F.feldSlots(renewed, emb.stub); } catch (e) { blocked = /recovery/.test(e.message); }
      ok(blocked, 'menu rebuild preserves pending Neon recovery');
    }
  }
  // 1.7.3 test13: a clean ROM keeps its ID page at 0xEFD000
  for (const f of process.argv.slice(2)) {
    const src = new Uint8Array(fs.readFileSync(f)), want = C.openRom(src).editionId, b = F.cleanBase(src), e = F.edition(b);
    const p = Object.create(FL.Paradise.prototype), pay = new Uint8Array(44); pay[8] = 1;
    p.link = { request: async () => ({ payload: pay }) }; p.read = async (o, n) => b.slice(o, o + n);
    const i = await p.hello();
    ok(e && e.id === want && e.at === 0xEFD000 && i.edition === want && b.subarray(0xD29000, 0xD4A000).every((v) => v === 0xFF), f.split('/').pop() + ' clean: ID page at 0xEFD000, ' + FL.EDITIONS[want]);
  }
  console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
