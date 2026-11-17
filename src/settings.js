/* Shared game Settings extension with paradise-zh-patcher. Pure ROM transforms.
 * A full owned erase page holds the table, safe scene dispatcher and versioned
 * metadata. Empty save-pool pages are allocations, never candidate free space. */
/* Runtime Language firmware profiles and entry behavior follow sawaYch's
 * tamagotchi-paradise-zh-patcher (https://github.com/sawaYch/tamagotchi-paradise-zh-patcher).
 * Retain its MIT notice here so single-file offline builds carry it as well.
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
(function (root) {
'use strict';
const PAGE = 4096, ABSENT = 0xFFFFFFFF, SENTINEL = 0x7FFE;
const PROFILES = [
  { table: 0xA4FFC, original: [28,70,0,27,71,0,26,0,0], ptr: 0x582CC, select: 0x583A6, next: 0x33CFC, scene: 0x138CC, stateSite: 0x138D6, state: 0x18001CB4, lang: 113, settings: 55, date: 114, inline: true },
  { table: 0x91A68, original: [28,68,0,27,69,0,26,0,0], ptr: 0x525E0, select: 0x526BA, next: 0x30448, scene: 0x8E948, stateSite: 0x1CCF6, state: 0x18001BF4, lang: 109, settings: 54, date: 110, inline: false },
  { table: 0x8C7F0, original: [28,67,0,27,68,0,26,0,0], ptr: 0x5061C, select: 0x506F6, next: 0x2F104, scene: 0x8A430, stateSite: 0x1CA8E, state: 0x18001BF4, lang: 106, settings: 53, date: 107, inline: false },
];
const U = (b,o) => (b[o] | b[o+1]<<8 | b[o+2]<<16 | b[o+3]<<24) >>> 0;
const H = (b,o) => b[o] | b[o+1]<<8;
function w16(b,o,v) { b[o]=v&255; b[o+1]=v>>>8&255; }
function w32(b,o,v) { for(let i=0;i<4;i++) b[o+i]=v>>>(i*8)&255; }
function cat(...parts) { const b=new Uint8Array(parts.reduce((n,p)=>n+p.length,0)); let at=0; for(const p of parts){b.set(p,at);at+=p.length;} return b; }
function words(v) { const b=new Uint8Array(v.length*4);v.forEach((n,i)=>w32(b,i*4,n));return b; }
function same(b,o,w) { return o>=0 && o+w.length<=b.length && w.every((v,i)=>b[o+i]===v); }
function blank(b,a,z) { return a>=0 && z<=b.length && b.subarray(a,z).every(v=>v===255); }
function sum(b,a,z) { let n=0;for(let i=a;i<z;i++)n+=b[i];return n>>>0; }
function alias(a) { return (0x10000000+a-0x11000)>>>0; }
function pack(a,b) { return Uint8Array.of(a&255,a>>>8,b&255,b>>>8); }
function movw(r,n) { return pack(0xF240|((n>>>11&1)<<10)|(n>>>12),((n>>>8&7)<<12)|(r<<8)|(n&255)); }
function movt(r,n) { return pack(0xF2C0|((n>>>11&1)<<10)|(n>>>12),((n>>>8&7)<<12)|(r<<8)|(n&255)); }
function branch(from,to,link=true) { const off=(to-from-4)>>>0,s=off>>>24&1,i1=off>>>23&1,i2=off>>>22&1,j1=~(i1^s)&1,j2=~(i2^s)&1;return pack(0xF000|(s<<10)|(off>>>12&1023),(link?0xD000:0x9000)|(j1<<13)|(j2<<11)|(off>>>1&2047)); }
function pointer(a) { const n=alias(a);return cat(movw(1,n&65535),movt(1,n>>>16)); }
function state(p) { return cat(movw(1,p.state&65535),movt(1,p.state>>>16)); }
function originalNext(p) { return cat(Uint8Array.of(p.date,0x20),branch(p.next+2,p.scene)); }
function trampoline(p,base) { const a=cat(state(p),Uint8Array.of(0x8A,0x88,p.date,0x20,p.settings,0x2A,0,0xD1,p.settings,0x20));return cat(a,p.inline?Uint8Array.of(0x48,0x80,0x70,0x47):branch(base+0x40+a.length,p.scene,false)); }
function dispatcher(p,base,stub) { return cat(movw(1,SENTINEL),Uint8Array.of(0x88,0x42,1,0xD0),branch(base+0x68,p.scene,false),Uint8Array.of(0,0x49,8,0x47),words([alias(stub+0x20)|1])); }
function sources(p,b) { return same(b,p.table,words(p.original)) && same(b,p.stateSite,state(p)); }
function hookEdits(info) {
  const p=PROFILES[info.profileIndex],n=3+!!(info.flags&1)+!!(info.flags&2);
  return [
    [p.ptr,pointer(info.page)], [p.ptr+10,Uint8Array.of(n,0x21)],
    [p.next,info.flags&1?cat(branch(p.next,info.page+0x40),Uint8Array.of(0,0xBF)):originalNext(p)],
    [p.select,branch(p.select,info.flags&2?info.page+0x60:p.scene)],
  ];
}
function originalEdits(p) { return [[p.ptr,pointer(p.table)],[p.ptr+10,Uint8Array.of(3,0x21)],[p.next,originalNext(p)],[p.select,branch(p.select,p.scene)]]; }
function payload(info) {
  const p=PROFILES[info.profileIndex],a=p.original.slice();
  if(info.flags&1)a.push(info.languageLabel,p.lang,0);
  if(info.flags&2)a.push(info.paramixLabel,SENTINEL,0);
  const b=new Uint8Array(PAGE).fill(255);b.set(words(a));
  if(info.flags&1)b.set(trampoline(p,info.page),0x40);
  if(info.flags&2)b.set(dispatcher(p,info.page,info.stub),0x60);
  b.set(Array.from('ZHLANG02',c=>c.charCodeAt(0)),0x80);
  [[0x88,2],[0x8C,info.page],[0x90,info.languageLabel],[0x94,info.profileIndex],[0x98,PAGE],[0x9C,sum(b,0,0x80)],[0xA0,info.flags],[0xA4,info.paramixLabel],[0xA8,info.stub]].forEach(([o,v])=>w32(b,o,v));
  w32(b,0xAC,sum(b,0,0xAC));return b;
}
function readMarker(b,page) {
  const magic=String.fromCharCode(...b.subarray(page+0x80,page+0x88));
  if(magic!=='ZHLANG01' && magic!=='ZHLANG02')return null;
  const v=magic==='ZHLANG02'?2:1,info={page,profileIndex:U(b,page+0x94),languageLabel:U(b,page+0x90),flags:v===2?U(b,page+0xA0):1,paramixLabel:v===2?U(b,page+0xA4):ABSENT,stub:v===2?U(b,page+0xA8):0,version:v};
  if(U(b,page+0x88)!==v || U(b,page+0x8C)!==page || U(b,page+0x98)!==PAGE || info.profileIndex>=3 || U(b,page+0x9C)!==sum(b,page,page+0x80) || !info.flags || info.flags>3 ||
     ((info.flags&1)?info.languageLabel===ABSENT:info.languageLabel!==ABSENT) || ((info.flags&2)?(!info.stub || info.paramixLabel===ABSENT):(info.stub!==0 || info.paramixLabel!==ABSENT)))throw new Error('damaged Settings patch metadata');
  if(v===2 && (U(b,page+0xAC)!==sum(b,page,page+0xAC) || !blank(b,page+0xB0,page+PAGE)))throw new Error('damaged Settings patch reservation');
  if(v===1 && !blank(b,page+0xA0,page+PAGE))throw new Error('damaged legacy Settings patch reservation');
  return info;
}
function validPatch(b,info) {
  const p=PROFILES[info.profileIndex];if(!sources(p,b))return false;
  if((info.flags&2) && !installedStub(b,info.stub))return false;
  if(info.version===2)return same(b,info.page,payload(info)) && hookEdits(info).every(([a,v])=>same(b,a,v));
  const a=p.original.concat([info.languageLabel,p.lang,0]);
  return same(b,info.page,words(a)) && same(b,info.page+0x40,trampoline(p,info.page)) && hookEdits(info).every(([a,v])=>same(b,a,v));
}
function inspect(b) {
  let marked=null;
  for(let page=0x90000;page<0x10F000;page+=PAGE){const info=readMarker(b,page);if(info){if(marked)throw new Error('multiple Settings patch reservations');if(!validPatch(b,info))throw new Error('Settings patch code does not match its metadata');marked=info;}}
  if(marked)return marked;
  // Original unmarked language patch, before the shared reservation protocol.
  const page=0xB6000;
  if(blank(b,page+0x80,page+0xA0))for(let i=0;i<3;i++){
    const info={page,profileIndex:i,flags:1,languageLabel:U(b,page+36),paramixLabel:ABSENT,stub:0,version:0};
    if(validPatch(b,info))return info;
  }
  for(let i=0;i<3;i++){const p=PROFILES[i];if(sources(p,b) && originalEdits(p).every(([a,v])=>same(b,a,v)))return {original:true,profileIndex:i,flags:0,page:0,languageLabel:ABSENT,paramixLabel:ABSENT,stub:0};}
  return null;
}
function reservations(b) {
  const ranges=[[0x10F000,0x110000]],add=(a,n)=>{if(a%PAGE || !n || a+n>b.length)throw new Error('invalid Paramix allocation');ranges.push([a,(a+n+4095)&~4095]);};
  if([0x43584D50,0x37584D50,0x38584D50,0x31584D50].includes(U(b,0xA0140)))ranges.push([0xA0000,0x110000]);
  for(let off=0x90000;off<0x110000;off+=PAGE){const c=off+0x140;if(U(b,c)!==0x444C4546)continue;const version=U(b,c+4);if(version<4 || version>10)throw new Error('unsupported Paramix allocation version');
    const stub=U(b,c+32),count=U(b,c+276),catAt=U(b,c+272);if(stub<0x90000 || stub>=0x110000 || count>2048)throw new Error('invalid Paramix allocation');
    add(stub,PAGE);add(off,0x10000);
    if(catAt){if(U(b,catAt)!==0x54414350 || U(b,catAt+4)!==count)throw new Error('invalid Paramix catalog');let end=catAt+8+count*28;if(U(b,end)===0x54584550)end+=4+12*count;if(U(b,end)===0x56525054){let n=U(b,end+12),size=U(b,end+8);if(n>256 || size>128)throw new Error('invalid preview catalog');end+=24+n*size;}add(catAt,end-catAt);}
    const slots=U(b,c+16),size=U(b,c+12),first=U(b,c+8),second=U(b,c+364),split=U(b,c+368);if(slots>6 || (slots && (size<8192 || size%PAGE || split>slots || (second && !split))))throw new Error('invalid Paramix slots');
    for(let i=0;i<slots;i++)add(second && i>=split?second+(i-split)*size:first+i*size,size);
    const id=U(b,c+28);if(id<PAGE || id>b.length-PAGE)throw new Error('invalid Paramix ID page');ranges.push([id-PAGE,b.length]);
    const apps=version>=5?U(b,c+392):0;if(apps>(version>=8?12:6))throw new Error('invalid Paramix apps');
    const app=(v)=>{if(v>=0x10000000 && v<0x11000000)add(v-0x10000000+0x11000,0x10000);};
    for(let i=0;i<apps;i++)app(U(b,c+(i<6?396+16*i:504+16*(i-6))));
    if(version>=7)app(U(b,c+500));if(version>=9)app(U(b,c+600));
  }return ranges;
}
function planPage(b) {
  const current=inspect(b);if(!current)return null;if(!current.original)return current.page;
  const ranges=reservations(b);for(let p=0x10E000;p>=0x90000;p-=PAGE)if(blank(b,p,p+PAGE) && !ranges.some(([a,z])=>p<z && a<p+PAGE))return p;
  throw new Error('no unallocated 4 KiB page for the Settings entry');
}
function parseStrings(b) { const count=H(b,0),index=H(b,2);if(index!==count+2 || H(b,(index-1)*2)*2!==b.length)throw new Error('unsupported string table');const a=[];for(let i=0;i<count;i++){const lo=H(b,(i+1)*2)*2,hi=H(b,(i+2)*2)*2;if(lo<index*2 || hi<lo || hi>b.length)throw new Error('invalid string offsets');a.push(b.slice(lo,hi));}return a; }
function stringBytes(a) {const n=a.length+2,total=n*2+a.reduce((s,b)=>s+b.length,0);if(total/2>65535)throw new Error('Settings strings exceed table capacity');const b=new Uint8Array(total);w16(b,0,a.length);let p=n*2;a.forEach((s,i)=>{w16(b,(i+1)*2,p/2);b.set(s,p);p+=s.length;});w16(b,(a.length+1)*2,p/2);return b;}
function paramixLabel(rom) {
  const C=root.ParamixCore || require('./core.js'),ctx=C.openRom(rom),strings=C.arc2Files(ctx.files[2],0).map(parseStrings);
  if(strings.length!==9 || !strings.every(s=>s.length===strings[0].length))throw new Error('Settings labels require matching language tables');
  const text=new Uint8Array(16);Array.from('PARAMIX').forEach((c,i)=>w16(text,i*2,318+c.charCodeAt(0)-65));
  for(let i=0;i<strings[0].length;i++)if(strings.every(s=>same(s[i],0,text) && s[i].length===text.length))return {rom,label:i};
  const label=strings[0].length;strings.forEach(s=>s.push(text));
  const arc=C.arc2Build([ctx.files[0],ctx.files[1],C.arc2Build(strings.map(stringBytes))]),oldEnd=ctx.assetEnd,newEnd=0x111000+arc.length;
  if(newEnd>rom.length || (newEnd>oldEnd && (!blank(rom,oldEnd,newEnd) || reservations(rom).some(([a,z])=>oldEnd<z && a<newEnd))))throw new Error('PARAMIX label would enter an allocated flash region');
  const out=new Uint8Array(rom);out.set(arc,0x111000);if(newEnd<oldEnd)out.fill(255,newEnd,oldEnd);return {rom:out,label};
}
function writePatch(b,info) {b.set(payload(info),info.page);for(const [a,v]of hookEdits(info))b.set(v,a);return b;}
function installedStub(rom,stub) {
  const target=U(rom,stub+0x28),entry=target>=0x10000000 && target<0x11000000?target-0x10000000+0x11000:-1,cfg=entry+0x140;
  return !(stub%PAGE) && stub>=0x90000 && stub<0x110000 && same(rom,stub+0x20,Uint8Array.of(0x72,0xB6,1,0x4C)) && !(entry%PAGE) && entry>=0x90000 && entry<0x110000 &&
    U(rom,cfg)===0x444C4546 && U(rom,cfg+4)>=4 && U(rom,cfg+4)<=10 && U(rom,cfg+32)===stub;
}
function removeLastLabel(rom,label) {
  const C=root.ParamixCore || require('./core.js'),ctx=C.openRom(rom),strings=C.arc2Files(ctx.files[2],0).map(parseStrings);
  const text=new Uint8Array(16);Array.from('PARAMIX').forEach((c,i)=>w16(text,i*2,318+c.charCodeAt(0)-65));
  if(strings.length!==9 || !strings.every(s=>s.length===label+1 && s[label].length===text.length && same(s[label],0,text)))return rom;
  strings.forEach(s=>s.pop());const arc=C.arc2Build([ctx.files[0],ctx.files[1],C.arc2Build(strings.map(stringBytes))]);
  const out=new Uint8Array(rom);out.set(arc,0x111000);out.fill(255,0x111000+arc.length,ctx.assetEnd);return out;
}
function apply(rom,stub,plannedPage) {
  const before=inspect(rom);if(!before)throw new Error('Settings menu does not match a supported firmware');
  if(!installedStub(rom,stub))throw new Error('Paramix direct-entry stub is not installed');
  const page=before.original?(plannedPage===undefined?planPage(rom):plannedPage):before.page;
  if(page%PAGE || page<0x90000 || page>=0x10F000)throw new Error('invalid Settings helper page');
  if(before.original && (!blank(rom,page,page+PAGE) || reservations(rom).some(([a,z])=>page<z && a<page+PAGE)))throw new Error('Settings page overlaps a Paramix allocation');
  const named=paramixLabel(rom),info={...before,page,version:2,flags:before.flags|2,paramixLabel:named.label,stub};return writePatch(new Uint8Array(named.rom),info);
}
function stripEntry(rom) {
  const current=inspect(rom);if(!current || current.original || !(current.flags&2))return new Uint8Array(rom);
  const out=new Uint8Array(rom),p=PROFILES[current.profileIndex];
  if(current.flags&1)return removeLastLabel(writePatch(out,{...current,flags:1,paramixLabel:ABSENT,stub:0,version:2}),current.paramixLabel);
  for(const [a,v]of originalEdits(p))out.set(v,a);out.fill(255,current.page,current.page+PAGE);return removeLastLabel(out,current.paramixLabel);
}
root.ParamixSettings={inspect,planPage,apply,stripEntry,prepareLabel:paramixLabel,removeLastLabel,reservations,PROFILES};
if(typeof module!=='undefined')module.exports=root.ParamixSettings;
})(typeof window!=='undefined'?window:globalThis);
