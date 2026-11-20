/* PARAMIX at the end of the game's Space menu (the list containing Connection).
 * Only that list's initializer and normal scene dispatch are hooked. Original
 * descriptors, global descriptor accessor and item indices remain untouched.
 * The Space view has a fixed widget pool: one list plus two widgets per row.
 * Version 2 expands its pool from 12 to 14, retaining one spare after 13 widgets.
 * Version 1 is accepted only for removal/migration; its six rows exhausted pool12. */
(function(root){
'use strict';
const PAGE=4096,SENTINEL=0x7FFE;
const PROFILES=[
  {table:0xA5254,descriptor:0xA5120,accessor:0x2BBBE,init:0x4C436,initTarget:0x2BBB4,select:0x4C7C2,scene:0x138CC,pool:0x2C41C,poolTarget:0x1B158,scenes:[119,127,128,126,125]},
  {table:0x91CC0,descriptor:0x91B8C,accessor:0x28C96,init:0x47752,initTarget:0x28C8C,select:0x47ADE,scene:0x8E948,pool:0x294D8,poolTarget:0x19918,scenes:[115,123,124,122,121]},
  {table:0x8CA48,descriptor:0x8C914,accessor:0x28412,init:0x4579E,initTarget:0x28408,select:0x45B2A,scene:0x8A430,pool:0x28C54,poolTarget:0x1975C,scenes:[112,120,121,119,118]},
  // Neon selects descriptor 1 in the native Space menu. Its four rows leave
  // enough room for one extra row in the original 12-widget pool.
  {table:0xA6CD0,descriptor:0xA6B78,accessor:0x2CC8A,descriptorIndex:1,init:0x4CB6A,initTarget:0x2CC80,select:0x4CEF6,scene:0x13A5C,pool:0x2D508,poolTarget:0x1B4D4,scenes:[119,127,128,126]},
];
const S=()=>root.ParamixSettings || require('./settings.js');
const U=(b,o)=>(b[o]|b[o+1]<<8|b[o+2]<<16|b[o+3]<<24)>>>0;
function w32(b,o,v){for(let i=0;i<4;i++)b[o+i]=v>>>(i*8)&255;}
function cat(...parts){let b=new Uint8Array(parts.reduce((n,p)=>n+p.length,0)),o=0;for(const p of parts){b.set(p,o);o+=p.length;}return b;}
function words(a){let b=new Uint8Array(a.length*4);a.forEach((v,i)=>w32(b,i*4,v));return b;}
function same(b,o,a){return o>=0 && o+a.length<=b.length && a.every((v,i)=>b[o+i]===v);}
function blank(b,a,z){return a>=0 && z<=b.length && b.subarray(a,z).every(v=>v===255);}
function sum(b,a,z){let n=0;for(let i=a;i<z;i++)n+=b[i];return n>>>0;}
function alias(a){return (0x10000000+a-0x11000)>>>0;}
function pack(a,b){return Uint8Array.of(a&255,a>>>8,b&255,b>>>8);}
function mov(r,n,top=false){return pack((top?0xF2C0:0xF240)|((n>>>11&1)<<10)|(n>>>12),((n>>>8&7)<<12)|(r<<8)|(n&255));}
function pointer(r,a){const v=alias(a);return cat(mov(r,v&65535),mov(r,v>>>16,true));}
function branch(from,to,link=true){const off=(to-from-4)>>>0,s=off>>>24&1,j1=~((off>>>23&1)^s)&1,j2=~((off>>>22&1)^s)&1;return pack(0xF000|s<<10|(off>>>12&1023),(link?0xD000:0x9000)|j1<<13|j2<<11|(off>>>1&2047));}
function originalTable(p){return words(p.scenes.flatMap((scene,i)=>[34+i,scene,0]));}
function sources(b,p,pool=12){const i=p.descriptorIndex||0;return same(b,p.pool,Uint8Array.of(pool,0x20,0x19,0x46,0x1A,0x46)) && same(b,p.pool+6,branch(p.pool+6,p.poolTarget)) && same(b,p.table,originalTable(p)) && same(b,p.descriptor,words([alias(p.table),p.scenes.length])) && same(b,p.accessor,pointer(0,p.descriptor-i*8)) && (!i || same(b,p.init-2,Uint8Array.of(i,0x20)));}
function extendedPool(p){return p.scenes.length===4?12:14;}
function originalEdits(p){return [[p.init,branch(p.init,p.initTarget)],[p.select,branch(p.select,p.scene)],[p.pool,Uint8Array.of(12,0x20)]];}
function edits(info){const p=PROFILES[info.profileIndex];return [[p.init,branch(p.init,info.page+0x50)],[p.select,branch(p.select,info.page+0x60)],...(info.version>=2?[[p.pool,Uint8Array.of(extendedPool(p),0x20)]]:[])];}
function installedStub(b,stub){const target=U(b,stub+0x28),entry=target>=0x10000000 && target<0x11000000?target-0x10000000+0x11000:-1,c=entry+0x140;return !(stub%PAGE) && stub>=0x90000 && stub<0x110000 && same(b,stub+0x20,Uint8Array.of(0x72,0xB6,1,0x4C)) && !(entry%PAGE) && entry>=0x90000 && entry<0x110000 && U(b,c)===0x444C4546 && U(b,c+4)>=4 && U(b,c+4)<=11 && U(b,c+32)===stub;}
function payload(info){
  const p=PROFILES[info.profileIndex],b=new Uint8Array(PAGE).fill(255);
  b.set(originalTable(p));b.set(words([info.paramixLabel,SENTINEL,0]),p.scenes.length*12);
  b.set(words([alias(info.page),p.scenes.length+1]),0x48);
  b.set(cat(pointer(0,info.page+0x48),Uint8Array.of(0x70,0x47)),0x50);
  b.set(cat(mov(1,SENTINEL),Uint8Array.of(0x88,0x42,1,0xD0),branch(info.page+0x68,p.scene,false),Uint8Array.of(0,0x49,8,0x47),words([alias(info.stub+0x20)|1])),0x60);
  b.set(Array.from(info.version>=2?'PMHOME02':'PMHOME01',c=>c.charCodeAt(0)),0x80);
  for(const[o,v]of[[0x88,info.version],[0x8C,info.page],[0x90,info.paramixLabel],[0x94,info.profileIndex],[0x98,PAGE],[0x9C,sum(b,0,0x80)],[0xA0,info.stub]])w32(b,o,v);
  w32(b,0xA4,sum(b,0,0xA4));return b;
}
function inspect(b){
  let marked=null;
  for(let page=0x90000;page<0x10F000;page+=PAGE){
    const magic=String.fromCharCode(...b.subarray(page+0x80,page+0x88));if(magic!=='PMHOME01' && magic!=='PMHOME02')continue;const version=magic==='PMHOME02'?2:1;
    if(marked)throw new Error('multiple PARAMIX Space menu reservations');
    const info={page,version,profileIndex:U(b,page+0x94),paramixLabel:U(b,page+0x90),stub:U(b,page+0xA0)};
    if(info.profileIndex>=PROFILES.length || U(b,page+0x88)!==version || U(b,page+0x8C)!==page || U(b,page+0x98)!==PAGE || info.paramixLabel===0xFFFFFFFF || U(b,page+0x9C)!==sum(b,page,page+0x80) || U(b,page+0xA4)!==sum(b,page,page+0xA4) || !blank(b,page+0xA8,page+PAGE))throw new Error('damaged PARAMIX Space menu metadata');
    if(!sources(b,PROFILES[info.profileIndex],version>=2?extendedPool(PROFILES[info.profileIndex]):12) || !installedStub(b,info.stub) || !same(b,page,payload(info)) || !edits(info).every(([a,v])=>same(b,a,v)))throw new Error('PARAMIX Space menu code does not match its metadata');
    marked=info;
  }
  if(marked)return marked;
  for(let i=0;i<PROFILES.length;i++){const p=PROFILES[i];if(sources(b,p) && originalEdits(p).every(([a,v])=>same(b,a,v)))return {original:true,profileIndex:i,page:0};}
  return null;
}
function reservations(b){const ranges=S().reservations(b),settings=S().inspect(b),home=inspect(b);if(settings && !settings.original)ranges.push([settings.page,settings.page+PAGE]);if(home && !home.original)ranges.push([home.page,home.page+PAGE]);return ranges;}
function planPage(b){const current=inspect(b);if(!current)return null;if(!current.original)return current.page;const ranges=reservations(b);for(let p=0x10E000;p>=0x90000;p-=PAGE)if(blank(b,p,p+PAGE) && !ranges.some(([a,z])=>p<z && a<p+PAGE))return p;throw new Error('no unallocated 4 KiB page for PARAMIX Space menu');}
function apply(rom,stub,plannedPage){const before=inspect(rom);if(!before)throw new Error('Space menu does not match a supported firmware');if(!installedStub(rom,stub))throw new Error('Paramix direct-entry stub is not installed');const page=before.original?(plannedPage===undefined?planPage(rom):plannedPage):before.page;if(page%PAGE || page<0x90000 || page>=0x10F000)throw new Error('invalid PARAMIX Space menu helper page');if(before.original && (!blank(rom,page,page+PAGE) || reservations(rom).some(([a,z])=>page<z && a<page+PAGE)))throw new Error('PARAMIX Space menu page overlaps an allocation');const named=S().prepareLabel(rom),info={page,profileIndex:before.profileIndex,version:2,paramixLabel:named.label,stub},out=new Uint8Array(named.rom);out.set(payload(info),page);for(const[a,v]of edits(info))out.set(v,a);return out;}
function stripEntry(rom){const current=inspect(rom);if(!current || current.original)return new Uint8Array(rom);const out=new Uint8Array(rom);for(const[a,v]of originalEdits(PROFILES[current.profileIndex]))out.set(v,a);out.fill(255,current.page,current.page+PAGE);const settings=S().inspect(out);return settings && !settings.original && settings.paramixLabel===current.paramixLabel?out:S().removeLastLabel(out,current.paramixLabel);}
root.ParamixHomeMenu={inspect,planPage,apply,stripEntry,reservations,prepareLabel:(b)=>S().prepareLabel(b),PROFILES};
if(typeof module!=='undefined')module.exports=root.ParamixHomeMenu;
})(typeof window!=='undefined'?window:globalThis);
