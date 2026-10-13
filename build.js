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
// Flash app for the device tab (our own code, source in flash/src)
let flash = fs.readFileSync(__dirname + '/src/flash.js', 'utf8');
{
  const f = __dirname + '/flash/flashapp.bin';
  if (!fs.existsSync(f)) throw new Error('flash/flashapp.bin fehlt');
  flash = flash.replace('__FLASH_APP_B64__', fs.readFileSync(f).toString('base64'));
}
if (flash.includes('</script')) throw new Error('script end tag inside source');
if (feld.includes('</script')) throw new Error('script end tag inside source');
if (doom.includes('</script')) throw new Error('script end tag inside source');
if (core.includes('</script') || app.includes('</script') || emu.includes('</script')) throw new Error('script end tag inside source');
html = html.replace('__CORE__', () => core).replace('__EMU__', () => emu).replace('__DOOM__', () => doom).replace('__FELD__', () => feld).replace('__FLASH__', () => flash).replace('__APP__', () => app).split('__VERSION__').join('v' + v);
fs.mkdirSync(__dirname + '/dist', { recursive: true });
fs.writeFileSync(__dirname + '/dist/ParamixStudio.html', html);
fs.writeFileSync(__dirname + '/dist/index.html', html);
console.log('dist/ParamixStudio.html', html.length, 'Byte, Version', v);
