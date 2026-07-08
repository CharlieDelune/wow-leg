#!/usr/bin/env python3
# PNG -> BLP2/DXT5 converter for custom WoW 3.3.5 UI textures. The client renders palettized (comp=1) BLPs as a
# solid green square, so custom UI art MUST be DXT5 (comp=2, alphaEncoding=7) -- which this always emits, with a
# full mip chain like Blizzard's own UI textures. Pure stdlib (no PIL). Usage: png2blp.py in.png out.blp
import sys, zlib, struct

def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, w, h, bd, ct, plte, trns, idat = 8, 0, 0, 0, 0, None, None, b""
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos+4])[0]; typ = d[pos+4:pos+8]; dat = d[pos+8:pos+8+ln]
        if typ == b"IHDR": w, h, bd, ct = struct.unpack(">IIBB", dat[:10])
        elif typ == b"PLTE": plte = dat
        elif typ == b"tRNS": trns = dat
        elif typ == b"IDAT": idat += dat
        elif typ == b"IEND": break
        pos += 12 + ln
    assert bd == 8, "only 8-bit channels supported (got depth %d)" % bd
    ch = {0:1, 2:3, 3:1, 4:2, 6:4}[ct]; stride = w*ch
    raw = zlib.decompress(idat); out = bytearray(w*h*4); prev = bytearray(stride); p = 0
    def paeth(a,b,c):
        q=a+b-c; pa=abs(q-a); pb=abs(q-b); pc=abs(q-c)
        return a if pa<=pb and pa<=pc else (b if pb<=pc else c)
    for y in range(h):
        ft = raw[p]; p += 1; line = bytearray(raw[p:p+stride]); p += stride
        if ft==1:
            for i in range(ch,stride): line[i]=(line[i]+line[i-ch])&0xff
        elif ft==2:
            for i in range(stride): line[i]=(line[i]+prev[i])&0xff
        elif ft==3:
            for i in range(stride):
                a=line[i-ch] if i>=ch else 0; line[i]=(line[i]+((a+prev[i])>>1))&0xff
        elif ft==4:
            for i in range(stride):
                a=line[i-ch] if i>=ch else 0; c=prev[i-ch] if i>=ch else 0
                line[i]=(line[i]+paeth(a,prev[i],c))&0xff
        for x in range(w):
            o=(y*w+x)*4
            if ct==6: out[o:o+4]=line[x*4:x*4+4]
            elif ct==2: out[o],out[o+1],out[o+2],out[o+3]=line[x*3],line[x*3+1],line[x*3+2],255
            elif ct==0: g=line[x]; out[o],out[o+1],out[o+2],out[o+3]=g,g,g,255
            elif ct==3:
                i=line[x]; out[o],out[o+1],out[o+2]=plte[i*3],plte[i*3+1],plte[i*3+2]
                out[o+3]=trns[i] if (trns and i<len(trns)) else 255
        prev=line
    return w, h, bytes(out)

def _565(r,g,b): return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
def _d565(c):
    r=(c>>11)&0x1f; g=(c>>5)&0x3f; b=c&0x1f
    return (r*255)//31,(g*255)//63,(b*255)//31

def enc_dxt5(rgba, w, h):
    bw, bh = (w+3)//4, (h+3)//4; res = bytearray()
    for by in range(bh):
        for bx in range(bw):
            px=[]
            for py in range(4):
                for pxx in range(4):
                    x=min(bx*4+pxx,w-1); y=min(by*4+py,h-1); o=(y*w+x)*4
                    px.append((rgba[o],rgba[o+1],rgba[o+2],rgba[o+3]))
            al=[p[3] for p in px]; a0,a1=max(al),min(al)
            apal=[a0]*8 if a0==a1 else [a0,a1]+[((7-i)*a0+i*a1)//7 for i in range(1,7)]
            aidx=0
            for i in range(16):
                best,bd=0,999
                for k,av in enumerate(apal):
                    dd=abs(av-al[i])
                    if dd<bd: bd,best=dd,k
                aidx|=best<<(3*i)
            res += bytes([a0,a1])+aidx.to_bytes(6,"little")
            mx=(max(p[0] for p in px),max(p[1] for p in px),max(p[2] for p in px))
            mn=(min(p[0] for p in px),min(p[1] for p in px),min(p[2] for p in px))
            c0,c1=_565(*mx),_565(*mn)
            if c0<c1: c0,c1=c1,c0
            col=[_d565(c0),_d565(c1)]
            r0,g0,b0=col[0]; r1,g1,b1=col[1]
            col.append(((2*r0+r1)//3,(2*g0+g1)//3,(2*b0+b1)//3))
            col.append(((r0+2*r1)//3,(g0+2*g1)//3,(b0+2*b1)//3))
            cidx=0
            if c0!=c1:
                for i in range(16):
                    best,bd=0,1<<30; pr,pg,pb,_=px[i]
                    for k in range(4):
                        cr,cg,cb=col[k]; dd=(cr-pr)**2+(cg-pg)**2+(cb-pb)**2
                        if dd<bd: bd,best=dd,k
                    cidx|=best<<(2*i)
            res += struct.pack("<HH",c0,c1)+cidx.to_bytes(4,"little")
    return bytes(res)

def downsample(rgba,w,h):
    nw,nh=max(1,w//2),max(1,h//2); out=bytearray(nw*nh*4)
    for y in range(nh):
        for x in range(nw):
            o=(y*nw+x)*4
            for k in range(4):
                s=0
                for dy in range(2):
                    for dx in range(2):
                        sx=min(2*x+dx,w-1); sy=min(2*y+dy,h-1); s+=rgba[(sy*w+sx)*4+k]
                out[o+k]=s//4
    return nw,nh,bytes(out)

def write_blp(path,w,h,rgba):
    mips=[]; cw,ch,cur=w,h,rgba
    while True:
        mips.append(enc_dxt5(cur,cw,ch))
        if cw==1 and ch==1: break
        cw,ch,cur=downsample(cur,cw,ch)
    hdr=bytearray(148)
    hdr[0:4]=b"BLP2"
    struct.pack_into("<IBBBBII",hdr,4,1,2,8,7,1,w,h)  # type,comp=DXT,alphaDepth,alphaEnc=DXT5,hasMips,w,h
    off=148
    for i in range(16):
        if i<len(mips):
            struct.pack_into("<I",hdr,20+i*4,off); struct.pack_into("<I",hdr,84+i*4,len(mips[i])); off+=len(mips[i])
    open(path,"wb").write(bytes(hdr)+b"".join(mips))

if __name__=="__main__":
    w,h,rgba=read_png(sys.argv[1]); write_blp(sys.argv[2],w,h,rgba)
    print("wrote %s (%dx%d, DXT5, %d mips)"%(sys.argv[2],w,h,(max(w,h)).bit_length()))
