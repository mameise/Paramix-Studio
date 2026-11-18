const fs=require('fs'),os=require('os'),path=require('path'),{execFileSync}=require('child_process');
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'paramix-reset-'));
try { const bin=path.join(dir,'reset-test');execFileSync(process.env.CC||'cc',['-fsanitize=address,undefined',path.join(__dirname,'flash_reset_test.c'),'-o',bin],{stdio:'inherit'});execFileSync(bin,[],{stdio:'inherit'}); }
finally {fs.rmSync(dir,{recursive:true,force:true});}
