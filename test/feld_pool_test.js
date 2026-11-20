const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{execFileSync}=require('node:child_process');
const F=require('../src/feld.js');
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'paramix-pool-'));
try{
  const bin=path.join(tmp,'pool-test');
  execFileSync(process.env.CC||'cc',['-w','-fsanitize=address,undefined','-g',path.join(__dirname,'feld_pool_test.c'),'-o',bin],{stdio:'inherit'});
  const args=[tmp];
  if(process.argv[2]){
    const E=require('../src/emu.js'),rom=fs.readFileSync(process.argv[2]);
    const emb={feld:fs.readFileSync(path.join(__dirname,'../feld/feld.bin')),stub:fs.readFileSync(path.join(__dirname,'../feld/stub_feld_tpl.bin'))};
    const rebuilt=F.feldApply(F.feldStrip(rom,emb,E),{fields:true,bag:true,tama:true,saves:true,paramixShortcut:false},F.feldSlots(rom,emb.stub),emb,E);
    const file=path.join(tmp,'rebuilt.bin'),f=F.feldFind(rebuilt,emb.stub);assert(f);
    fs.writeFileSync(file,rebuilt);args.push(file,String(f.off+0x140));
  }
  execFileSync(bin,args,{stdio:'inherit',timeout:120000});
}finally{fs.rmSync(tmp,{recursive:true,force:true});}
// Rebuilding/relocating must retain sharing and commit nothing when full.
const f={slotOff:0x1000,slotSize:0x4000,nslots:6},out=new Uint8Array(0x20000).fill(255),used=new Set();
const pg=new Uint8Array(4096);let r=0x32187123;for(let i=0;i<pg.length;i++){r^=r<<13;r^=r>>>17;r^=r<<5;pg[i]=r;}
const source=F.slotBuild(new Map([[0xD50000,pg]]),0,0xD50000,0xF00000);
for(let i=0;i<6;i++)F.slotPlace(out,f,i,source,used);
assert.equal(used.size,Math.ceil(source.bytes/4096));
for(let i=0;i<6;i++){const b=F.slotAddr(f,i),s=F.slotParse(f,i,out.subarray(b,b+4096));s.stream=new Uint8Array(s.bytes);s.list.forEach((a,k)=>s.stream.set(out.subarray(a,a+Math.min(4096,s.bytes-k*4096)),k*4096));assert.deepEqual(F.slotPages(s).get(0xD50000),pg);}
const snapshot=out.slice(),taken=new Set(used);for(let x=0;x<f.nslots*(f.slotSize/4096-1);x++)used.add(x);
const different=pg.map(v=>v^255),other=F.slotBuild(new Map([[0xD50000,different]]),0,0xD50000,0xF00000);
assert.throws(()=>F.slotPlace(out,f,0,other,used),/do not fit/);assert.deepEqual(out,snapshot);
console.log('PASS: Studio sharing, round-trip, immutable existing pages and atomic capacity rejection');

// Extra pool indices append to old geometry, preserving every old index.
const expanded={...f,poolExtraOff:0xFB7000,poolExtraPages:4},xrom=new Uint8Array(0x1000000).fill(255),xused=new Set();
const base=F.poolCount(f);for(let x=0;x<base;x++)xused.add(x);
F.slotPlace(xrom,expanded,0,source,xused);
assert(F.poolCount(expanded)===base+4);
const ep=F.slotParse(expanded,0,xrom.subarray(F.slotAddr(expanded,0),F.slotAddr(expanded,0)+4096));
assert(ep.list.every(a=>a>=0xFB7000 && a<0xFBB000));
assert(F.slotRanges(expanded).some(([a,b])=>a===0xFB7000 && b===0xFBB000));
// SIXB referenced all-FF cell tails count as occupied even without non-FF bytes.
const six=new Uint8Array(0x1000000).fill(255),put=(a,v)=>new DataView(six.buffer).setUint32(a,v,true);
put(0x10F000,0x31584953);put(0x10F004,7);put(0x10F01C,0);put(0x10F020,0);
put(0x111024,0x10000);const pack=0x121000,h=pack+0x100,pix=h+24;put(pack,0x100);six[h+20]=24;six[h+21]=0;
put(0xF00000,0x42584953);put(0xF00004,1);put(0xF00008,0);put(0xF0000C,0);put(pix+4,4096);
assert.equal(F.sixPoolTail(six).poolExtraOff,0xF02000);
six[0xF03000]=1;assert.equal(F.sixPoolTail(six).poolExtraOff,0xF04000);
put(0x10F020,4);put(0x10F024,0xF04000);put(0xF04000,10);
assert.equal(F.sixPoolTail(six).poolExtraOff,0xF05000);
put(0xF0000C,0xFFFFFF00);assert.equal(F.sixPoolTail(six),null);
console.log('PASS: expanded pool geometry, SIXB referenced tails, foreign data and game restoration bounds');
if(process.argv[2]) (async()=>{
  const E=require('../src/emu.js'),FL=require('../src/flash.js'),original=new Uint8Array(fs.readFileSync(process.argv[2]));
  const emb={feld:new Uint8Array(fs.readFileSync(path.join(__dirname,'../feld/feld.bin'))),stub:new Uint8Array(fs.readFileSync(path.join(__dirname,'../feld/stub_feld_tpl.bin')))};
  const old=F.feldFind(original,emb.stub),saved=F.feldSlots(original,emb.stub);
  const opts={fields:true,bag:true,tama:true,saves:true,paramixShortcut:false};
  const stripped=F.feldStrip(original,emb,E),next=F.feldApply(stripped,opts,saved,emb,E),nf=F.feldFind(next,emb.stub);
  assert.equal(nf.version,11);assert.equal(nf.poolExtraOff,0xFB7000);assert.equal(nf.poolExtraPages,57);
  function savesEqual(img){const slots=F.feldSlots(img,emb.stub);assert.equal(slots.length,saved.length);for(const s of saved){const dst=slots.find(v=>v.index===s.index);assert(dst);assert.deepEqual(dst.stream,s.stream);}}
  savesEqual(next);
  assert.deepEqual(next.slice(0xF00000,nf.poolExtraOff),original.slice(0xF00000,nf.poolExtraOff),'rain/game cells unchanged');
  assert.deepEqual(next.slice(0xFF0000),original.slice(0xFF0000),'rescue unchanged');
  const noextra=F.feldApply(stripped,{...opts,appsLow:0xF00000},saved,emb,E),none=F.feldFind(noextra,emb.stub);assert.equal(none.poolExtraPages,0);
  const dirty=next.slice();dirty[nf.poolExtraOff]=0x32;assert.equal(F.feldStrip(dirty,emb,E)[nf.poolExtraOff],255);
  const bad=next.slice(),put32=(img,a,v)=>new DataView(img.buffer,img.byteOffset,img.byteLength).setUint32(a,v,true),ci=nf.off+0x140;
  put32(bad,ci+616,0x1000);assert.equal(F.feldFind(bad,emb.stub),null,'malformed extension fails closed');
  const changes=(a,b)=>{const s=new Set();for(let p=0;p<a.length;p+=4096)if(!Buffer.from(a.subarray(p,p+4096)).equals(Buffer.from(b.subarray(p,p+4096))))s.add(p);return s;};
  const plan=FL.keepPlan(F,E,emb,{}),dev=rom=>({info:{edition:F.edition(rom).id},read:async(a,n)=>rom.slice(a,a+n)});
  let changed=changes(original,next),kept=await plan(dev(original),next,changed);savesEqual(kept.image);
  assert(!kept.notes.some(n=>n.code==='noteNoFit'));
  // Keep Saves must fail before writing when even one store cannot migrate.
  const tooSmall=next.slice();
  put32(tooSmall,ci+12,0x2000); put32(tooSmall,ci+612,0); put32(tooSmall,ci+616,0);
  await assert.rejects(plan(dev(original),tooSmall,changes(original,tooSmall)),{code:'slotMigration'});
  const corrupted=original.slice(),capacitySlot=saved[0].index,oldHeader=F.slotAddr(old,capacitySlot);
  const data=F.slotParse(old,capacitySlot,corrupted.subarray(oldHeader,oldHeader+4096)).list[0];
  corrupted[data]^=0x40;
  // Same-geometry writes protect the existing pool in place. Force relocation
  // here so that Keep Saves must read and validate the damaged slot.
  await assert.rejects(plan(dev(corrupted),noextra,changes(corrupted,noextra)),{code:'slotMigration'});
  await assert.rejects(plan(dev(original),stripped,changes(original,stripped)),{code:'slotMenuRequired'});
  // Same geometry protects the entire newly reserved tail, even when empty in the new image.
  changed=new Set([nf.poolExtraOff]);kept=await plan(dev(dirty),next,changed);assert.equal(kept.image[nf.poolExtraOff],0x32);
  // A stored page in the extension is read before migrating to a layout without it.
  const withExtra=next.slice(),index=saved[0].index,b=F.slotAddr(nf,index),src=F.slotParse(nf,index,withExtra.subarray(b,b+4096));
  withExtra.set(withExtra.slice(src.list[0],src.list[0]+4096),nf.poolExtraOff);
  const base=nf.nslots*(nf.slotSize/4096-1);withExtra[b+2032]=base;withExtra[b+2033]=base>>8;
  const target=noextra.slice();target[nf.poolExtraOff]=0x55;changed=changes(withExtra,target);
  kept=await plan(dev(withExtra),target,changed);savesEqual(kept.image);assert.equal(kept.image[nf.poolExtraOff],0x55);
  assert(!kept.notes.some(n=>n.code==='noteNoFit'));
  console.log(`PASS: real-ROM v11 allocation (${F.poolCount(nf)*4} KiB), strip, exclusions, keep-saves and extension relocation`);
})().catch(e=>{console.error(e);process.exitCode=1;});
