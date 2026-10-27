// 1.6.8: figures for a further T7Emu, download items for a further TamaEmu (save kept on the device)
// node test/emu_xitems_test.js <land.bin> <tama-go.bin> <fig1.bin> <fig2.bin> <idl.bin>
const fs = require('fs'), S = __dirname + '/../';
const C = require(S + 'src/core.js'), E = require(S + 'src/emu.js'), F = require(S + 'src/feld.js'), FL = require(S + 'src/flash.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const [land, go, f1, f2, idl] = process.argv.slice(2);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const progs = { t7: rd(S + 't7/t7.bin'), tama: rd(S + 'emu/emu.bin'), t18: rd(S + 't18/t18.bin'), v1: rd(S + 'v1/v1.bin') }; progs.t18.xip = rd(S + 't18/t18x.bin');
const fe = { feld: rd(S + 'feld/feld.bin'), stub: rd(S + 'feld/stub_feld_tpl.bin') };
const ctx = C.openRom(rd(land)); const base = C.buildRom(ctx, C.launcherChanges(ctx)).rom;
function build(xs) {
  const r = E.appsPlace(base, xs, progs, { launch: true, dlApp: rd(S + 'flash/flashapp.bin') });
  return F.feldApply(r.rom, { launch: true, apps: r.list, dl: r.dl }, [], fe, E);
}
function secs(a, b) { const o = new Set(); for (let s = 0; s < Math.max(a.length, b.length); s += 0x1000) if (!Buffer.from(a.subarray(s, s + 0x1000)).equals(Buffer.from(b.subarray(s, s + 0x1000)))) o.add(s); return o; }
(async () => {
  // figures behind a further Tama-Go
  const figs = [{ name: '(03)_Tama-Go_Mametchi.bin', data: E.t7FigData(rd(f1)) }, { name: '(04)_Tama-Go_Memetchi.bin', data: E.t7FigData(rd(f2)) }];
  const a = build([{ rom: rd(go), figs }]);
  const back = E.appsRead(a, F.feldApps(a, fe.stub).apps);
  ok(back.length === 1 && back[0].figs.length === 2 && back[0].figs[0].name === E.t7FigName(figs[0].name), 'figures read back: ' + back[0].figs.map((f) => f.name).join(', '));
  ok(back[0].figs.every((f, i) => Buffer.from(f.data).equals(Buffer.from(E.t7Trim ? E.t7Trim(figs[i].data) : figs[i].data))), 'figure data identical');
  const a2 = build([{ rom: back[0].rom, figs: back[0].figs }]);
  ok(Buffer.from(a2).equals(Buffer.from(a)), 'built again from the ROM: identical');
  const cl = E.appsClear(a, back);
  ok(back[0].figs.length && cl.subarray(back[0].off, back[0].off + E.extraSize({ rom: back[0].rom, figs: back[0].figs })).every((v) => v === 0xFF), 'removed: figures freed as well');
  // download items in a further iD L: the device keeps the pet's own writes, the Studio's item sectors are written
  const mk = (nm) => {
    const rec = new Uint8Array(0x100); rec.set(Buffer.from('TAMAGO'));
    for (let i = 0; i < nm.length; i++) rec[7 + 2 * i] = nm.charCodeAt(i);
    rec[0x4E] = 0x81; rec[0x50] = 0x02; rec[0x51] = 0x01; rec[0x4B] = 0x00; rec[0x4A] = 0x01;
    let sum = 0; for (let i = 0; i < 0xFE; i++) sum += rec[i]; rec[0xFE] = sum >> 8 & 255; rec[0xFF] = sum & 255; return rec;
  };
  // the store already holds an item (its block is in the layout); a second item keeps the layout
  const G0 = rd(idl), G = E.dlcAdd(G0, E.dlcRecord(mk('DL_X_tea.jpg'), E.emuDetect(G0))).guest;
  const r0 = build([{ kind: 'tama', guest: G, dev: E.emuDetect(G) }]);
  const x0 = E.appsRead(r0, F.feldApps(r0, fe.stub).apps)[0];
  ok(Buffer.from(x0.guest).equals(Buffer.from(G)), 'read back: the whole firmware, also the parts kept as spare pages (1.7.1)');
  const ad = E.dlcAdd(x0.guest, E.dlcRecord(mk('DL_X_cake.jpg'), x0.dev));
  const r1 = build([{ kind: 'tama', guest: ad.guest, dev: x0.dev }]);
  const mine = secs(r0, r1);
  ok(mine.size > 0 && mine.size < 8, 'items change ' + mine.size + ' sectors');
  const c = E.extraCfg(r0, x0.off);
  // the device: the pet wrote into its state and into a guest data sector the Studio did not touch
  const device = new Uint8Array(r0); device.fill(0x5A, c.saveOff, c.saveOff + 0x1000);
  let petSec = -1; for (let s = c.guestOff + 0x10000; s < c.guestOff + c.guestSize; s += 0x1000) if (!mine.has(s) && device[s] !== 0xFF) { petSec = s; break; }
  device.fill(0x33, petSec, petSec + 0x1000);
  const changed = secs(device, r1);
  const dev = { info: { edition: F.edition(r1).id }, read: async (x, n) => device.slice(x, x + n) };
  const plan = await FL.keepPlan(F, E, { stub: fe.stub }, { mine })(dev, r1, changed);
  ok(!plan.notes.length, 'no notes: ' + JSON.stringify(plan.notes));
  ok(plan.image.subarray(c.saveOff, c.saveOff + 0x1000).every((v) => v === 0x5A), 'run state kept');
  ok(plan.image.subarray(petSec, petSec + 0x1000).every((v) => v === 0x33), 'the guest\'s own write kept (0x' + petSec.toString(16) + ')');
  ok([...mine].every((s) => Buffer.from(plan.image.subarray(s, s + 0x1000)).equals(Buffer.from(r1.subarray(s, s + 0x1000)))), 'item sectors from the Studio');
  const st = E.dlcStores(E.appsRead(plan.image, F.feldApps(plan.image, fe.stub).apps)[0].guest);
  ok(st.some((k) => k.slots.some((y) => y.name === 'cake')), 'item in the store after writing');
  // clock off for this program only: the flags change, the save stays
  const r2 = build([{ kind: 'tama', guest: ad.guest, dev: x0.dev, clock: false }]);
  const x2 = E.appsRead(r2, F.feldApps(r2, fe.stub).apps)[0];
  ok(x2.clock === false && (r2[x0.off + 0x140 + 156] & 2) === 0, 'clock flag per program');
  const dev2 = new Uint8Array(r1); dev2.fill(0x5A, c.saveOff, c.saveOff + 0x1000);
  const ch2 = secs(dev2, r2), d2 = { info: { edition: F.edition(r2).id }, read: async (x, n) => dev2.slice(x, x + n) };
  const plan3 = await FL.keepPlan(F, E, { stub: fe.stub }, { mine: secs(r1, r2) })(d2, r2, ch2);
  ok(!plan3.notes.length && plan3.image.subarray(c.saveOff, c.saveOff + 0x1000).every((v) => v === 0x5A), 'clock switched: run state kept');
  // without 'mine' (old way) the extra counted as new firmware
  const plan2 = await FL.keepPlan(F, E, { stub: fe.stub }, {})(dev, r1, changed);
  ok(plan2.notes.length === 1, 'old way: note');
  console.log(bad ? bad + ' FAILED' : 'all good'); process.exit(bad ? 1 : 0);
})();
