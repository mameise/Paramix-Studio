// Synthetic firmware profiles and archives; no game images or user saves.
const assert=require('assert'),C=require('../src/core.js'),S=require('../src/settings.js');
const w32=(b,o,v)=>new DataView(b.buffer,b.byteOffset,b.byteLength).setUint32(o,v,true),u32=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(o,true);
function pack(a,b){return Uint8Array.of(a&255,a>>>8,b&255,b>>>8);}
function mov(r,n,top=false){return pack((top?0xF2C0:0xF240)|((n>>>11&1)<<10)|(n>>>12),((n>>>8&7)<<12)|(r<<8)|(n&255));}
function join(...parts){const b=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let o=0;for(const p of parts){b.set(p,o);o+=p.length;}return b;}
function branch(a,z,link=true){let off=(z-a-4)>>>0,s=off>>>24&1,j1=~((off>>>23&1)^s)&1,j2=~((off>>>22&1)^s)&1;return pack(0xF000|s<<10|(off>>>12&1023),(link?0xD000:0x9000)|j1<<13|j2<<11|(off>>>1&2047));}
function ptr(a){let v=0x10000000+a-0x11000;return join(mov(1,v&65535),mov(1,v>>>16,true));}
function words(a){let b=new Uint8Array(a.length*4);a.forEach((n,i)=>w32(b,i*4,n));return b;}
function sum(b,a,z){let s=0;for(let i=a;i<z;i++)s+=b[i];return s>>>0;}
function fixture(i){
  const b=new Uint8Array(0x1000000).fill(255),p=S.PROFILES[i];b.set(Buffer.from('SONIXDEV'));
  b.set(words(p.original),p.table);b.set(join(mov(1,p.state&65535),mov(1,p.state>>>16,true)),p.stateSite);
  b.set(ptr(p.table),p.ptr);b.set([3,0x21],p.ptr+10);b.set(join(Uint8Array.of(p.date,0x20),branch(p.next+2,p.scene)),p.next);b.set(branch(p.select,p.scene),p.select);
  const sprite=new Uint8Array(24);w32(sprite,0,24);
  const str=Uint8Array.of(1,0,3,0,5,0,62,1,0,0); // one string, "A"
  b.set(C.arc2Build([new Uint8Array(80),C.packJoin([sprite]),C.arc2Build(Array(9).fill(str))]),0x111000);
  for(const[stub,entry]of[[0xB6000,0xB8000],[0xB7000,0xB9000]]){
    b.set([0x72,0xB6,1,0x4C],stub+0x20);w32(b,stub+0x28,0x10000000+entry-0x11000);
    const c=entry+0x140;w32(b,c,0x444C4546);w32(b,c+4,9);w32(b,c+16,0);w32(b,c+28,0xD29000);w32(b,c+32,stub);w32(b,c+36,0);w32(b,c+272,0);w32(b,c+276,0);w32(b,c+392,0);w32(b,c+500,0);w32(b,c+600,0);
  }return b;
}
function languageV1(b,i,base){
  const p=S.PROFILES[i];b.set(words([...p.original,0,p.lang,0]),base);
  let t=join(mov(1,p.state&65535),mov(1,p.state>>>16,true),Uint8Array.of(0x8A,0x88,p.date,0x20,p.settings,0x2A,0,0xD1,p.settings,0x20));
  t=join(t,p.inline?Uint8Array.of(0x48,0x80,0x70,0x47):branch(base+0x40+t.length,p.scene,false));b.set(t,base+0x40);
  b.set(ptr(base),p.ptr);b.set([4,0x21],p.ptr+10);b.set(join(branch(p.next,base+0x40),Uint8Array.of(0,0xBF)),p.next);
  b.set(Buffer.from('ZHLANG01'),base+0x80);for(const[o,v]of[[0x88,1],[0x8C,base],[0x90,0],[0x94,i],[0x98,4096],[0x9C,sum(b,base,base+0x80)]])w32(b,base+o,v);
  return b;
}
for(let i=0;i<3;i++){
  const original=fixture(i),copy=original.slice();assert(S.inspect(original).original);assert.equal(S.planPage(original),0x10E000);
  const installed=S.apply(original,0xB6000),info=S.inspect(installed);assert.equal(info.flags,2);assert.equal(info.paramixLabel,1);assert.equal(info.page,0x10E000);
  assert.deepEqual(original,copy);assert.deepEqual(S.stripEntry(installed),original);
  assert.deepEqual(S.apply(installed,0xB6000),installed,'reapplying must not append duplicate labels');
  const moved=S.apply(installed,0xB7000);assert.equal(S.inspect(moved).stub,0xB7000);assert.equal(u32(moved,info.page+0x70),(0x10000000+0xB7020-0x11000)|1);
  assert.deepEqual(S.stripEntry(moved),original);
  const legacy=languageV1(fixture(i),i,0x10D000),combined=S.apply(legacy,0xB6000);assert.equal(S.inspect(combined).flags,3);assert.equal(S.inspect(combined).languageLabel,0);
  const stripped=S.stripEntry(combined);assert.equal(S.inspect(stripped).flags,1);assert.equal(S.inspect(stripped).languageLabel,0);
  assert.deepEqual(S.apply(stripped,0xB6000),combined,'Language and PARAMIX must merge deterministically');
  const broken=installed.slice();broken[info.page+0x70]^=1;assert.throws(()=>S.inspect(broken),/damaged|does not match/);
  const bad=installed.slice();bad[info.page+0xAC]^=1;assert.throws(()=>S.inspect(bad),/damaged/);
  const buffer=Buffer.from(original),bufferCopy=Buffer.from(buffer);S.apply(buffer,0xB6000);assert.deepEqual(buffer,bufferCopy,'Buffer inputs must not share mutable output storage');
  const noSpace=fixture(i),p=S.PROFILES[i];noSpace.fill(0,0x90000,0x10F000);noSpace.set(original.subarray(p.table,p.table+36),p.table);assert.throws(()=>S.planPage(noSpace),/no unallocated/);
}
assert.equal(S.inspect(new Uint8Array(0x1000000).fill(255)),null);
console.log('PASS: standalone PARAMIX Settings entry, all profiles, language merge, relocation, strip, corruption and idempotence');
