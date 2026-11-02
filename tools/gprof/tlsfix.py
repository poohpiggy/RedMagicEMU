import sys,struct
def arm(b):
    n=0
    for i in range(0,len(b)-20,4):
        w=struct.unpack_from('<I',b,i)[0]
        if (w&0x0fff0fff)!=0x03e00a0f or w>>28==15: continue
        x=(w>>12)&15
        for j in range(1,5):
            v=struct.unpack_from('<I',b,i+4*j)[0]
            if (v&0x0ff00fff)==0x0510000f and v>>28!=15 and ((v>>16)&15)==x:
                y=(v>>12)&15; struct.pack_into('<I',b,i+4*j,(v&0xf0000000)|0x0e1d0f70|(y<<12)); n+=1; break
            if ((v>>16)&15)==x or ((v>>12)&15)==x: break   # rX touched otherwise: leave it
    return n
def reads(h,r):
    # register fields read by the few Thumb forms accepted after the load; None: unknown form
    if h&0xf800 in (0x6800,0x6000): return {(h>>3)&7} | ({h&7} if h&0xf800==0x6000 else set())  # ldr/str rd,[rn,#i]
    if h&0xf800==0x3000: return {(h>>8)&7}      # adds rd,#i
    if h&0xf800==0x2000: return set()           # movs rd,#i
    return None
def writes(h):
    if h&0xf800 in (0x6800,0x3000,0x2000): return (h>>8)&7 if h&0xf800!=0x6800 else h&7
    return -1
def thumb(b):
    n=0
    for i in range(0,len(b)-12,2):
        h=struct.unpack_from('<H',b,i)[0]
        if h&0xf800!=0x4800: continue
        rx=(h>>8)&7; lit=((i+4)&~3)+(h&0xff)*4
        if lit+4>len(b) or struct.unpack_from('<I',b,lit)[0]!=0xffff0ff0: continue
        h1=struct.unpack_from('<H',b,i+2)[0]
        if h1&0xffc0!=0x6800 or ((h1>>3)&7)!=rx: continue
        ry=h1&7; dead = rx==ry
        k=i+4
        while not dead and k<i+20:
            t=struct.unpack_from('<H',b,k)[0]
            if t==0x4770 or t&0xff00==0xbd00: dead=True; break
            r=reads(t,rx)
            if r is None or rx in r: break
            if writes(t)==rx: dead=True; break
            k+=2
        if not dead: continue
        struct.pack_into('<HH',b,i,0xee1d,0x0f70|(ry<<12)); n+=1
    return n
for p in sys.argv[1:]:
    b=bytearray(open(p,'rb').read()); a=arm(b); t=thumb(b)
    if a or t: open(p,'wb').write(b)
    print(p,'arm',a,'thumb',t)
