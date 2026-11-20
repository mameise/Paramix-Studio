// Check release artifacts agree, and reject a stale Paramix menu before publishing.
const assert=require('assert'),fs=require('fs'),path=require('path'),vm=require('vm');
const {execFileSync}=require('child_process');
const root=path.resolve(__dirname,'..'),version=fs.readFileSync(path.join(root,'VERSION'),'utf8').trim();
const header=path.join(root,'feld/src/feld/version.h'),before=fs.readFileSync(header);
execFileSync('python3',[path.join(root,'feld/src/feld/gen_version.py')]);
assert.deepEqual(fs.readFileSync(header),before,'version generation must be deterministic');
const binary=fs.readFileSync(path.join(root,'feld/feld.bin'));
assert(binary.includes(Buffer.from('PMXVER:'+version+'\0')),'Paramix menu binary is stale');
assert(fs.readFileSync(path.join(root,'feld/feldc.bin')).includes(Buffer.from('PMXVER:'+version+'\0')),'clean menu binary is stale');
assert(!fs.readFileSync(path.join(root,'flash/flashapp.bin')).includes(Buffer.from('PMXVER:')),'version label belongs in the menu, not Flash-Tool');
const mismatch={...fs,readFileSync(file,...args){return path.resolve(file)===path.join(root,'VERSION')?'0.0.0-plus.999\n':fs.readFileSync(file,...args);},mkdirSync(){throw Error('unexpected output');},writeFileSync(){throw Error('unexpected output');}};
assert.throws(()=>vm.runInNewContext(fs.readFileSync(path.join(root,'build.js'),'utf8'),{__dirname:root,Buffer,console,require:(name)=>name==='fs'?mismatch:require(name)}),/Paramix menu version does not match VERSION/);
console.log('PASS: menu version, standard/clean binary consistency, plain Flash-Tool, stale-build rejection');
