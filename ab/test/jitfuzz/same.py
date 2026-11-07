import sys
d=sys.argv[1]; r=open(d+'/ref.txt').read().split(); j=open(d+'/jit.txt').read().split()
if len(r)!=2 or len(j)!=2: sys.exit(1)
a=bytearray.fromhex(r[0]); b=bytearray.fromhex(j[0]); a[0x5f]&=0xDF; b[0x5f]&=0xDF
sys.exit(0 if a==b and r[1]==j[1] else 1)
