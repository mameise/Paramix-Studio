// test25: "Keep saves" with an imported T7Emu save (start device with a TamaEmu and T7Emu): only that save changes, the
// Paradise save and the TamaEmu flash stay as on the device; without "Keep saves" all come from the file.
// node test/keep_import_test.js <start device ROM with TamaEmu and T7Emu in the menu, e.g. r2.bin of ui_save_pick_test>
const S=__dirname+'/../';
const FL=require(S+'src/flash.js'),F=require(S+'src/feld.js'),E=require(S+'src/emu.js'),SV=require(S+'src/saves.js'),fs=require('fs');
function fakeDevice(mem){const devSide=new FL.Link({write:async()=>{}});const sec=new Uint8Array(0x1000),io={onData:null};
 io.write=async(d)=>{devSide.feed(d);for(const f of devSide.frames.splice(0)){const p=f.payload,u=(o)=>(p[o]|p[o+1]<<8|p[o+2]<<16|p[o+3]<<24)>>>0;let out=new Uint8Array(0);
  if(f.type===1){out=new Uint8Array(44);out[8]=mem[0xD49000];out[10]=0;out[11]=16;out.set([0,0,0,1],32);}
  else if(f.type===5)out=mem.slice(f.off,f.off+u(0));
  else if(f.type===6){const c=FL.crc32(mem,f.off,u(0));out=new Uint8Array([c&255,c>>>8&255,c>>>16&255,c>>>24]);}
  else if(f.type===13){out=new Uint8Array(16);out[12]=1;}
  else if(f.type===9)out=new Uint8Array(4);
  else if(f.type===10){sec.set(p,f.off&0xFFF);}
  else if(f.type===11){mem.set(sec,f.off&~0xFFF);out=new Uint8Array(8);const c=FL.crc32(sec);out.set([c&255,c>>>8&255,c>>>16&255,c>>>24]);}
  setImmediate(()=>io.onData(devSide.frame(f.type,f.seq,f.off,out)));}};return io;}
let bad=0; const ok=(c,m)=>{console.log((c?'ok   ':'FAIL ')+m); if(!c) bad++;};
async function run(withKeep){
 const stub=new Uint8Array(fs.readFileSync(S+'feld/stub_feld_tpl.bin'));
 const r2=new Uint8Array(fs.readFileSync(process.argv[2]));
 const apps=F.feldApps(r2,stub).apps, xs=E.appsRead(r2,apps); const ix=xs.findIndex(x=>E.extraKind(x)==='t7'); const t7=xs[ix];
 const mem=new Uint8Array(r2);
 // the device has played: Paradise save, colour Tama flash (guest area), T7 pet
 const ed=1, pa=0xD4A000; mem.fill(0x11,pa,pa+0x1000);
 const cfg=SV.emuCfg(mem.subarray(0xA0000,0xA1000)); const gpage=cfg.guestOff+0x10000; const gold=mem.slice(gpage,gpage+16); mem[gpage]^=0xFF; 
 const oldT7=SV.t7SaveOut(mem,t7.off+0x10000); const devT7=new Uint8Array(oldT7||new Uint8Array(4098)); devT7[10]^=0x22; const mem2=SV.t7SaveIn(mem,devT7,t7.off+0x10000); mem.set(mem2);
 const io=fakeDevice(mem); const {dev}=await FL.connect(io,new Uint8Array(16),{});
 // Studio: ROM read from the device, the T7 save imported
 const imp=new Uint8Array(4098).fill(0); imp[0]=1; imp[2]=0x77; imp[200]=0x55;
 const img=SV.t7SaveIn(new Uint8Array(r2),imp,t7.off+0x10000);   // an older file than the device
 const keep=withKeep?FL.keepPlan(F,E,{stub},{t7SaveX:ix}):null;
 const r=await dev.write(img,{onlyChanged:true,keep},null);
 const got=SV.t7SaveOut(mem,t7.off+0x10000);
 const k=mem.subarray(pa,pa+0x1000).every(v=>v===0x11), g=mem[gpage]===(gold[0]^0xFF);
 ok(Buffer.from(got).equals(Buffer.from(imp)),(withKeep?'keep: ':'no keep: ')+'T7Emu has the imported save');
 ok(withKeep?k&&g:!k&&!g,(withKeep?'keep: Paradise save and TamaEmu flash stay as on the device':'no keep: everything from the file'));
}
(async()=>{ await run(true); await run(false); console.log(bad?'FAIL '+bad:'all good'); })();
