// 1.7.6 PS_DL2: every kind of P's download (tamaplanet) goes where the P's firmware puts it itself.
// The firmware's own result comes from sending the file over emulated IR into DOWNLOAD (werkzeuge/ps_dl).
// node test/ps_dl_test.js <groups.tsv> <flash before> <dir with the firmware result per kind> [ps|idl]
const fs = require('fs');
const E = require('../src/emu.js');
const [, , gt, f0p, dir, DEV = 'ps'] = process.argv;
const f0 = new Uint8Array(fs.readFileSync(f0p));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; };
const runs = (a, b) => { const r = []; for (let i = 0; i < a.length;) { if (a[i] !== b[i]) { let j = i; while (j < a.length && (a[j] !== b[j] || a.subarray(j, j + 64).some((v, k) => v !== b[j + k]))) j++; r.push([i, j]); i = j; } else i++; } return r; };
for (const line of fs.readFileSync(gt, 'utf8').trim().split('\n')) {
  const [k, file] = line.split('\t');
  if (!fs.existsSync(dir + '/' + k + '.bin')) continue;
  const fw = new Uint8Array(fs.readFileSync(dir + '/' + k + '.bin'));
  const want = runs(f0, fw).filter(([a]) => a >= 0x590000);   // the firmware's store write (not its buffer at 0x51000 or the save)
  let got = null, err = '';
  try { const rec = E.dlcRecord(new Uint8Array(fs.readFileSync(file)), DEV); got = E.dlcAdd(f0, rec).guest; } catch (e) { err = e.message; }
  if (!want.length) { ok(!got, k + ': the firmware refuses it, the Studio too (' + err + ')'); continue; }
  if (!got) { ok(false, k + ': ' + err); continue; }
  const [a, z] = want[0];
  const same = fw.subarray(a, z).every((v, i) => v === got[a + i]);
  ok(same, k + ': at ' + a.toString(16) + ' like the firmware');
  if (k.startsWith('815203')) ok(got[0xE04B] === 0x0C && got[0xE4E4 + 0x4B] === 0x0C, k + ': stamp card marked in the save');
  if (DEV === 'idl' && k.startsWith('Happy')) ok(got[0xE04A] === fw[0xE04A] && fw[0xE04A] === 3, k + ': happy mail marked in the save like the firmware');
}
// iD L originals: the P's refuses them
if (DEV === 'ps') for (const f of ['Meals/amakara chijimi.jpg', 'Letters/Anemoriritchi_1.jpg']) {
  const p = '/mnt/user-data/uploads/Paramix/DLC/Tamagotchi iD L/' + f;
  if (!fs.existsSync(p)) continue;
  let e = ''; try { E.dlcRecord(new Uint8Array(fs.readFileSync(p)), 'ps'); } catch (x) { e = x.message; }
  ok(/iD L/.test(e), 'iD L file refused: ' + e);
}
// run state: the save copy in RAM gets the stamp byte
if (DEV === 'ps') {
  const save = f0.slice(0xE000, 0xE4E4), emu = new Uint8Array(0xFE88);
  for (let i = 0; i < emu.length; i++) emu[i] = (i * 7) & 0x3F;
  emu.set(save, 0x198C + 0x12A8); emu[0x198C + 0x12A8 + 0x4B] = 0;
  const gram = new Uint8Array(32768).fill(5), raw = new Uint8Array(emu.length + gram.length); raw.set(emu); raw.set(gram, emu.length);
  const out = []; const enc = (p) => { let i = 0; while (i < p.length) { let r = 1; while (i + r < p.length && r < 130 && p[i + r] === p[i]) r++; if (r >= 3) { out.push(0x80 + r - 3, p[i]); i += r; continue; } const s0 = i; let l = 0; while (i < p.length && l < 128) { let q = 1; while (i + q < p.length && q < 3 && p[i + q] === p[i]) q++; if (q >= 3) break; i++; l++; } out.push(l - 1); for (let k = 0; k < l; k++) out.push(p[s0 + k]); } };
  enc(emu); enc(gram);
  const img = new Uint8Array(0x60000).fill(0xFF), so = 0x1000, w = (o, x) => { img[o] = x & 255; img[o + 1] = (x >>> 8) & 255; img[o + 2] = (x >>> 16) & 255; img[o + 3] = (x >>> 24) & 255; };
  const ns = Math.ceil(out.length / 0x1000); w(so, 0x53584D50); w(so + 4, 3); w(so + 8, 0x1234); w(so + 12, 99); w(so + 16, out.length); w(so + 20, ns); w(so + 24, (0x53584D50 ^ 3 ^ 0x1234 ^ 99 ^ out.length ^ ns) >>> 0); img.set(out, so + 0x1000);
  ok(E.psSessStamp(img, so, save, 0x0C), 'run state found');
  const u = (o) => (img[o] | (img[o + 1] << 8) | (img[o + 2] << 16) | (img[o + 3] << 24)) >>> 0;
  const src = img.subarray(so + 0x1000, so + 0x1000 + u(so + 16)), back = [];
  for (let i = 0; i < src.length;) { const c = src[i++]; if (c & 0x80) { const n = c - 0x80 + 3, b = src[i++]; for (let k = 0; k < n; k++) back.push(b); } else for (let k = 0; k <= c; k++) back.push(src[i++]); }
  ok(back.length === raw.length && back[0x198C + 0x12A8 + 0x4B] === 0x0C && back.every((v, i) => i === 0x198C + 0x12A8 + 0x4B || v === raw[i]), 'run state: only the stamp byte changed');
  ok(u(so + 24) === ((u(so) ^ u(so + 4) ^ u(so + 8) ^ u(so + 12) ^ u(so + 16) ^ u(so + 20)) >>> 0), 'run state header checks out');
}
console.log(bad ? bad + ' WRONG' : 'all good'); process.exit(bad ? 1 : 0);
