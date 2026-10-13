#!/usr/bin/env python3
"""Item catalog from a Paradise image (ID, English name, fixed).
Based on werkzeuge/gegenstaende-lesen.py and Bilder.kt from the TamaParadise app."""
import struct, sys
u16=lambda d,o: struct.unpack_from('<H',d,o)[0]
u32=lambda d,o: struct.unpack_from('<I',d,o)[0]
SATZ=46; A=318
SONDER={305:' ',317:' ',375:'&',376:"'",381:'.'}
def bildtab(d):
    o=0
    while o+4<len(d):
        e0=u32(d,o)
        if 0x1000<=e0<=0x8000 and e0%4==0:
            prev=e0; ok=True
            for i in range(1,60):
                w=u32(d,o+4*i)
                if w<=prev or w>0x1000000: ok=False;break
                prev=w
            if ok:
                k=o+e0
                if k+24<len(d):
                    g=u32(d,k); p=u32(d,k+20)
                    if 24<p<g and 1<=g<=0x20000 and p%8==0 and d[k+8]>0 and d[k+9]>0: return o
        o+=4
    return -1
def itemtab(d,nb):
    best,bn=-1,0; o=0
    while o+SATZ<len(d):
        if 5001<=u16(d,o)<=9999:
            seen=set(); n=0
            while n<400 and o+n*SATZ+5<len(d):
                k=u16(d,o+n*SATZ); b=u16(d,o+n*SATZ+4)
                if not (5001<=k<=9999) or b>=nb or k in seen: break
                seen.add(k); n+=1
            if n>bn: bn,best=n,o
            if n>8: o+=n*SATZ-2
        o+=2
    return best,bn
def s_at(d,t,n):
    ab=u16(d,t+2*n)
    if ab<=0 or ab>0x8000: return ''
    o=t+2*ab; s=''
    while len(s)<40:
        c=u16(d,o)
        if c==0: break
        if A<=c<A+26: s+=chr(65+c-A)
        elif c in SONDER: s+=SONDER[c]
        else: return ''
        o+=2
    return s.strip()
def word3(d,t,n):
    ab=u16(d,t+2*n)
    if ab<=0 or ab>0x8000: return False
    return all(A<=u16(d,t+2*ab+2*k)<A+26 for k in range(3))
WORDS=["FOOD","SNACKS","OUTSIDE TOY","LAB LOG","MILK"]
def langtab(d):
    best=None;bp=0;o=0
    while o<len(d)-8:
        n=u16(d,o)
        if 100<=n<=4000 and u16(d,o+2)>n and u16(d,o+4)>u16(d,o+2) and u16(d,o+6)>u16(d,o+4) and word3(d,o,1):
            prev=n;ok=True
            for i in range(1,min(n,200)):
                w=u16(d,o+2*i)
                if w<prev or w>0x8000: ok=False;break
                prev=w
            if ok:
                p=sum(1 for k in range(1,120) if s_at(d,o,k) in WORDS)
                if p>bp: bp,best=p,o
        o+=2
    return best if bp>=2 else None
def catalog(d):
    bt=bildtab(d); nb=u32(d,bt)//4
    it,n=itemtab(d,nb)
    lt=langtab(d)
    out=[]
    for i in range(n):
        o=it+i*SATZ; k=u16(d,o); nm=u16(d,o+2)+1; price=u16(d,o+24)
        if not (5001<=k<=8999): continue
        name=s_at(d,lt,nm) if lt is not None else ''
        out.append((k,price==0xFFFF,name))
    return sorted(out),hex(bt),hex(it),n,lt
if __name__=='__main__':
    for f in sys.argv[1:]:
        d=open(f,'rb').read()
        c,bt,it,n,lt=catalog(d)
        print(f.split('/')[-1],bt,it,n,hex(lt) if lt else None,len(c),'fixed',[x[0] for x in c if x[1]],'without name',sum(1 for x in c if not x[2]))
        print('  ',c[:3],c[-3:], max(len(x[2]) for x in c))
