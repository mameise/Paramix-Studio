// Builds a single HTML file: dist/ParamixStudio.html (runs offline).
// Usage: node build.js
const fs = require('fs');
const v = fs.readFileSync(__dirname + '/VERSION', 'utf8').trim();
let html = fs.readFileSync(__dirname + '/src/index.html', 'utf8');
const core = fs.readFileSync(__dirname + '/src/core.js', 'utf8');
const app = fs.readFileSync(__dirname + '/src/app.js', 'utf8');
// Emulator: embed the prebuilt program and stub template as Base64 (if present)
let emu = fs.readFileSync(__dirname + '/src/emu.js', 'utf8');
const eb = __dirname + '/emu/emu.bin', es = __dirname + '/emu/stub_tpl.bin';
if (!fs.existsSync(eb) || !fs.existsSync(es)) throw new Error('emu/emu.bin oder emu/stub_tpl.bin fehlt');
{
  emu = emu.replace('__EMU_APP_B64__', fs.readFileSync(eb).toString('base64')).replace('__EMU_STUB_B64__', fs.readFileSync(es).toString('base64'));
}
// T7Emu: V7 program (source in t7/src). No ROM, no figures.
{
  const t7 = __dirname + '/t7/t7.bin';
  if (!fs.existsSync(t7)) throw new Error('t7/t7.bin fehlt');
  emu = emu.replace('__T7_APP_B64__', fs.readFileSync(t7).toString('base64'));
  const t7x = __dirname + '/t7/t7x.bin';   // 1.6.12 test2: second part (JIT)
  if (fs.existsSync(t7x)) emu = emu.replace('__T7X_APP_B64__', fs.readFileSync(t7x).toString('base64'));
  // V1Emu: E0C6S46/48 program (source in v1/src, core TamaLIB). No ROM.
  const v1 = __dirname + '/v1/v1.bin';
  if (!fs.existsSync(v1)) throw new Error('v1/v1.bin fehlt');
  emu = emu.replace('__V1_APP_B64__', fs.readFileSync(v1).toString('base64'));
  // V3Emu (1.6.9): Connection V3 program (source in v3/src, core from BrickEmuPy). No ROM.
  const v3 = __dirname + '/v3/v3.bin';
  if (!fs.existsSync(v3)) throw new Error('v3/v3.bin fehlt');
  emu = emu.replace('__V3_APP_B64__', fs.readFileSync(v3).toString('base64'));
  // GBEmu (1.6.9 test36): Game Boy program (source in gb/src, core Peanut-GB). No ROM.
  const gbb = __dirname + '/gb/gb.bin';
  if (!fs.existsSync(gbb)) throw new Error('gb/gb.bin fehlt');
  emu = emu.replace('__GB_APP_B64__', fs.readFileSync(gbb).toString('base64'));
  // SMSEmu (1.6.10 test2): Master System program (source in sms/src). No ROM.
  const smb = __dirname + '/sms/sms.bin';
  if (!fs.existsSync(smb)) throw new Error('sms/sms.bin fehlt');
  emu = emu.replace('__SMS_APP_B64__', fs.readFileSync(smb).toString('base64'));
  // Wolf3D (1.6.10 test1): program (source in wolf/src, Wolf4SDL by id Software and others, GPL). No game data.
  const wb = __dirname + '/wolf/wolf.bin';
  if (!fs.existsSync(wb)) throw new Error('wolf/wolf.bin fehlt');
  emu = emu.replace('__WOLF_APP_B64__', fs.readFileSync(wb).toString('base64'));
  // Spear of Destiny (1.6.11 test2): the same program built for SOD. No game data.
  const wsb = __dirname + '/wolf/wolfsod.bin';
  if (!fs.existsSync(wsb)) throw new Error('wolf/wolfsod.bin fehlt');
  emu = emu.replace('__WOLFSOD_APP_B64__', fs.readFileSync(wsb).toString('base64'));
  // T18Emu: TG18 program (source in t18/src). No firmware.
  const t18 = __dirname + '/t18/t18.bin';
  if (!fs.existsSync(t18)) throw new Error('t18/t18.bin fehlt');
  const t18x = __dirname + '/t18/t18x.bin';
  if (!fs.existsSync(t18x)) throw new Error('t18/t18x.bin fehlt');
  emu = emu.replace('__T18_APP_B64__', fs.readFileSync(t18).toString('base64')).replace('__T18_XIP_B64__', fs.readFileSync(t18x).toString('base64'));
}
// DOOM: program (two parts) and WAD converter (WebAssembly) as Base64 (if present). No WAD, no graphics.
let doom = fs.readFileSync(__dirname + '/src/doom.js', 'utf8');
{
  const d = (f) => fs.readFileSync(__dirname + '/doom/' + f).toString('base64');
  for (const f of ['doom_app.bin', 'doom_xip.bin', 'mcuwad.wasm']) if (!fs.existsSync(__dirname + '/doom/' + f)) throw new Error('doom/' + f + ' fehlt');
  doom = doom.replace('__DOOM_APP_B64__', d('doom_app.bin')).replace('__DOOM_XIP_B64__', d('doom_xip.bin')).replace('__DOOM_WASM_B64__', d('mcuwad.wasm'));
}
// Field switch: program and stub template
let feld = fs.readFileSync(__dirname + '/src/feld.js', 'utf8');
{
  const d = (f) => fs.readFileSync(__dirname + '/feld/' + f).toString('base64');
  for (const f of ['feld.bin', 'stub_feld_tpl.bin']) if (!fs.existsSync(__dirname + '/feld/' + f)) throw new Error('feld/' + f + ' fehlt');
  feld = feld.replace('__FELD_B64__', d('feld.bin')).replace('__FELD_STUB_B64__', d('stub_feld_tpl.bin'));
}
// Six fields: helper page (source in six/src)
let six = fs.readFileSync(__dirname + '/src/six.js', 'utf8');
{
  const f = __dirname + '/six/six.bin';
  if (!fs.existsSync(f)) throw new Error('six/six.bin fehlt');
  six = six.replace('__SIX_B64__', fs.readFileSync(f).toString('base64'));
}
if (six.includes('</script')) throw new Error('script end tag inside source');
// Flash app for the device tab (our own code, source in flash/src)
let flash = fs.readFileSync(__dirname + '/src/flash.js', 'utf8');
{
  const f = __dirname + '/flash/flashapp.bin';
  if (!fs.existsSync(f)) throw new Error('flash/flashapp.bin fehlt');
  flash = flash.replace('__FLASH_APP_B64__', fs.readFileSync(f).toString('base64'));
}
// every script must parse (a stray quote in a text broke the whole page once)
{
  const vm = require('vm');
  for (const [n, src] of [['core.js', core], ['app.js', app], ['emu.js', emu], ['doom.js', doom], ['feld.js', feld], ['six.js', six], ['flash.js', flash], ['saves.js', fs.readFileSync(__dirname + '/src/saves.js', 'utf8')]]) {
    try { new vm.Script(src, { filename: n }); } catch (e) { throw new Error(n + ': ' + e.message); }
  }
}
const saves = fs.readFileSync(__dirname + '/src/saves.js', 'utf8');
const visit = fs.readFileSync(__dirname + '/src/visit.js', 'utf8');
const wolf = fs.readFileSync(__dirname + '/src/wolf.js', 'utf8');
if (wolf.includes('</script')) throw new Error('script end tag inside source');
new (require('vm').Script)(wolf, { filename: 'wolf.js' });
if (visit.includes('</script')) throw new Error('script end tag inside source');
new (require('vm').Script)(visit, { filename: 'visit.js' });
if (saves.includes('</script')) throw new Error('script end tag inside source');
if (flash.includes('</script')) throw new Error('script end tag inside source');
if (feld.includes('</script')) throw new Error('script end tag inside source');
if (doom.includes('</script')) throw new Error('script end tag inside source');
if (core.includes('</script') || app.includes('</script') || emu.includes('</script')) throw new Error('script end tag inside source');
html = html.replace('__CORE__', () => core).replace('__EMU__', () => emu).replace('__DOOM__', () => doom).replace('__FELD__', () => feld).replace('__SIX__', () => six).replace('__FLASH__', () => flash).replace('__SAVES__', () => saves).replace('__VISIT__', () => visit).replace('__WOLF__', () => wolf).replace('__APP__', () => app).split('__VERSION__').join('v' + v);
fs.mkdirSync(__dirname + '/dist', { recursive: true });
fs.writeFileSync(__dirname + '/dist/ParamixStudio.html', html);
fs.writeFileSync(__dirname + '/dist/index.html', html);
console.log('dist/ParamixStudio.html', html.length, 'Byte, Version', v);
