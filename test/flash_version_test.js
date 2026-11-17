// Check release artifacts agree, and reject a stale Flash-Tool before publishing.
const assert=require('assert'),fs=require('fs'),path=require('path'),vm=require('vm');
const {execFileSync}=require('child_process');
const root=path.resolve(__dirname,'..'),version=fs.readFileSync(path.join(root,'VERSION'),'utf8').trim();
const header=path.join(root,'flash/src/version.h'),before=fs.readFileSync(header);
execFileSync('python3',[path.join(root,'flash/src/gen_version.py')]);
assert.deepEqual(fs.readFileSync(header),before,'version generation must be deterministic');
const binary=fs.readFileSync(path.join(root,'flash/flashapp.bin'));
assert(binary.includes(Buffer.from('PMXVER:'+version+'\0')),'Flash-Tool binary is stale');
assert(binary.length<=0xE000,'Flash-Tool does not fit its rescue allocation');
const pixels=before.toString().split('flash_version_bits[14][16]=')[1];
assert.equal((pixels.match(/0x[0-9a-f]{2}/g)||[]).length,14*16,'expected two seven-row pixel labels');
const mismatch={...fs,readFileSync(file,...args){return path.resolve(file)===path.join(root,'VERSION')?'0.0.0-plus.999\n':fs.readFileSync(file,...args);},mkdirSync(){throw Error('unexpected output');},writeFileSync(){throw Error('unexpected output');}};
assert.throws(()=>vm.runInNewContext(fs.readFileSync(path.join(root,'build.js'),'utf8'),{__dirname:root,Buffer,console,require:(name)=>name==='fs'?mismatch:require(name)}),/Flash-Tool version does not match VERSION/);
console.log('PASS: deterministic version pixels, matching binary, rescue size, stale-build rejection');
