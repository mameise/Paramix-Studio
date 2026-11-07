import sys
for d in sys.argv[1:]:
    r=open(d+'/ref.txt').read().split(); j=open(d+'/jit.txt').read().split()
    out=[]
    for k,(a,b) in enumerate(zip(r,j)):
        base = 0 if k==0 else 0x100
        for i in range(0,len(a),2):
            if a[i:i+2]!=b[i:i+2]: out.append('%x:%s/%s'%(base+i//2, a[i:i+2], b[i:i+2]))
    print(d, ' '.join(out[:8]))
