// Synthetic code profiles and minimal archives; no copyrighted ROM data.
const assert=require('assert'),C=require('../src/core.js'),H=require('../src/home_menu.js'),S=require('../src/settings.js');
const w32=(b,o,v)=>new DataView(b.buffer,b.byteOffset,b.byteLength).setUint32(o,v,true),u32=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(o,true);
function pack(a,b){return Uint8Array.of(a&255,a>>>8,b&255,b>>>8);}
function mov(r,n,top=false){return pack((top?0xF2C0:0xF240)|((n>>>11&1)<<10)|(n>>>12),((n>>>8&7)<<12)|(r<<8)|(n&255));}
function join(...parts){const b=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let o=0;for(const p of parts){b.set(p,o);o+=p.length;}return b;}
function branch(a,z,link=true){let off=(z-a-4)>>>0,s=off>>>24&1,j1=~((off>>>23&1)^s)&1,j2=~((off>>>22&1)^s)&1;return pack(0xF000|s<<10|(off>>>12&1023),(link?0xD000:0x9000)|j1<<13|j2<<11|(off>>>1&2047));}
function alias(a){return (0x10000000+a-0x11000)>>>0;}
function ptr(r,a){let v=alias(a);return join(mov(r,v&65535),mov(r,v>>>16,true));}
function words(a){let b=new Uint8Array(a.length*4);a.forEach((n,i)=>w32(b,i*4,n));return b;}
function fixture(i){
 const b=new Uint8Array(0x1000000).fill(255),p=H.PROFILES[i],s=S.PROFILES[i];b.set(Buffer.from('SONIXDEV'));
 b.set(Uint8Array.of(12,0x20,0x19,0x46,0x1A,0x46),p.pool);b.set(branch(p.pool+6,p.poolTarget),p.pool+6);
 b.set(words(p.scenes.flatMap((scene,i)=>[34+i,scene,0])),p.table);b.set(words([alias(p.table),5]),p.descriptor);b.set(ptr(0,p.descriptor),p.accessor);b.set(branch(p.init,p.initTarget),p.init);b.set(branch(p.select,p.scene),p.select);
 b.set(words(s.original),s.table);b.set(join(mov(1,s.state&65535),mov(1,s.state>>>16,true)),s.stateSite);b.set(ptr(1,s.table),s.ptr);b.set([3,0x21],s.ptr+10);b.set(join(Uint8Array.of(s.date,0x20),branch(s.next+2,s.scene)),s.next);b.set(branch(s.select,s.scene),s.select);
 const sprite=new Uint8Array(24);w32(sprite,0,24);const str=Uint8Array.of(1,0,3,0,5,0,62,1,0,0);
 b.set(C.arc2Build([new Uint8Array(80),C.packJoin([sprite]),C.arc2Build(Array(9).fill(str))]),0x111000);
 for(const[stub,entry]of[[0xB6000,0xB8000],[0xB7000,0xB9000]]){b.set([0x72,0xB6,1,0x4C],stub+0x20);w32(b,stub+0x28,alias(entry));const c=entry+0x140;for(const[o,v]of[[0,0x444C4546],[4,9],[16,0],[28,0xD29000],[32,stub],[272,0],[276,0],[392,0],[500,0],[600,0]])w32(b,c+o,v);}
 return b;
}
for(let i=0;i<3;i++){
 const original=fixture(i),copy=original.slice(),p=H.PROFILES[i];assert(H.inspect(original).original);assert.equal(H.planPage(original),0x10E000);
 const patched=H.apply(original,0xB6000),info=H.inspect(patched),page=info.page;assert.equal(info.paramixLabel,1);assert.deepEqual(original,copy);assert.equal(info.version,2);assert.equal(patched[p.pool],14,'six rows need 13 widget slots plus original one spare');
 assert.deepEqual(patched.subarray(page,page+60),original.subarray(p.table,p.table+60),'original item order and conditions must remain');
 assert.equal(u32(patched,page+60),1);assert.equal(u32(patched,page+64),0x7FFE);assert.equal(u32(patched,page+68),0);
 assert.deepEqual(patched.subarray(page+0x48,page+0x50),words([alias(page),6]));
 const requiredWidgets=1+2*u32(patched,page+0x4C);assert.equal(requiredWidgets,13);assert(requiredWidgets>original[p.pool],'regression: original fixed pool cannot build six rows');assert(requiredWidgets<=patched[p.pool]);assert.equal(patched[p.pool]-requiredWidgets,1,'retain original spare widget slot');
 assert.deepEqual(patched.subarray(page+0x50,page+0x5A),join(ptr(0,page+0x48),Uint8Array.of(0x70,0x47)),'initializer returns cloned descriptor');
 assert.deepEqual(patched.subarray(p.init,p.init+4),branch(p.init,page+0x50));assert.deepEqual(patched.subarray(p.select,p.select+4),branch(p.select,page+0x60));
 assert.deepEqual(patched.subarray(page+0x68,page+0x6C),branch(page+0x68,p.scene,false),'ordinary selections tail call original scene function with LR preserved');
 assert.equal(u32(patched,page+0x70),(alias(0xB6020)|1)>>>0,'PARAMIX alone jumps to direct entry');
 assert.deepEqual(patched.subarray(p.descriptor,p.descriptor+8),original.subarray(p.descriptor,p.descriptor+8));assert.deepEqual(patched.subarray(p.accessor,p.accessor+8),original.subarray(p.accessor,p.accessor+8),'global descriptor accessor is untouched');
 assert.deepEqual(S.inspect(patched),S.inspect(original),'Settings remains unchanged');assert.deepEqual(H.apply(patched,0xB6000),patched);assert.deepEqual(H.stripEntry(patched),original);
 const legacy=patched.slice();legacy.set(Buffer.from('PMHOME01'),page+0x80);w32(legacy,page+0x88,1);legacy[p.pool]=12;let checksum=0;for(let j=page;j<page+0xA4;j++)checksum+=legacy[j];w32(legacy,page+0xA4,checksum);assert.equal(H.inspect(legacy).version,1);assert.deepEqual(H.apply(legacy,0xB6000),patched,'legacy helper migrates to safe widget pool');assert.deepEqual(H.stripEntry(legacy),original);
 const moved=H.apply(patched,0xB7000);assert.equal(H.inspect(moved).stub,0xB7000);assert.deepEqual(H.stripEntry(moved),original);
 const oldSettings=S.apply(original,0xB6000),withBoth=H.apply(oldSettings,0xB6000);assert.equal(H.inspect(withBoth).page,0x10D000,'Settings helper page must be reserved');assert.equal(S.inspect(withBoth).paramixLabel,H.inspect(withBoth).paramixLabel);assert.deepEqual(H.stripEntry(withBoth),oldSettings,'shared label remains live for old Settings helper');
 const tooSmall=patched.slice();tooSmall[p.pool]=12;assert.throws(()=>H.inspect(tooSmall),/does not match/,'v2 cannot silently retain unsafe v1 pool');
 const bad=patched.slice();bad[page+0xA4]^=1;assert.throws(()=>H.inspect(bad),/damaged/);const badHook=patched.slice();badHook[p.select]^=1;assert.throws(()=>H.inspect(badHook),/does not match/);const badDescriptor=original.slice();badDescriptor[p.descriptor+4]=4;assert.equal(H.inspect(badDescriptor),null);
 const buffer=Buffer.from(original),bufferCopy=Buffer.from(buffer);H.apply(buffer,0xB6000);assert.deepEqual(buffer,bufferCopy);
}
assert.equal(H.inspect(new Uint8Array(0x1000000).fill(255)),null);
console.log('PASS: PARAMIX appended Space menu, all profiles, descriptor/count, original indices, dispatch, strip, corruption and language reservation');

// cfg11 preserves the direct entry and reserves its extra pool even while erased.
{
 const b=fixture(0),c=0xB8000+0x140;
 w32(b,c+4,11);w32(b,c+608,0);w32(b,c+612,0xFB7000);w32(b,c+616,57);
 assert(S.reservations(b).some(([a,z])=>a===0xFB7000 && z===0xFF0000));
 assert(H.inspect(H.apply(b,0xB6000)));
 w32(b,c+612,0xFF0000);assert.throws(()=>S.reservations(b),/extra pool/);
}
