"""Generate the lowercase-sigma user-defined glyphs for the LT7680 (16x32 = 64 bytes, 12x24 = 48 bytes).
Shape: an oval ring with a flag from the top running to the right edge, sitting at x-height like the
other lowercase letters; baseline matches the ohm glyph (feet end on row 25 of 32 / row 18 of 24).
Row bytes are left-justified, MSB = leftmost pixel. 12-wide rows use 2 bytes with the low 4 bits unused."""
import math,sys

def ring(w,h,cx,cy,orx,ory,irx,iry,top,flag_rows,flag_x0):
    g=[[0]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            px,py=x+0.5,y+0.5
            o=((px-cx)/orx)**2+((py-cy)/ory)**2<=1.0
            i=((px-cx)/irx)**2+((py-cy)/iry)**2<=1.0
            if o and not i: g[y][x]=1
    for y in range(top,top+flag_rows):
        for x in range(flag_x0,w): g[y][x]=1
    return g

def art(g): return "\n".join("".join("#" if v else "." for v in r) for r in g)

def pack(g,w):
    out=[]
    for r in g:
        bits=0
        for x in range(16):
            bits=(bits<<1)|(r[x] if x<w else 0)
        out+= [(bits>>8)&0xFF,bits&0xFF]
    return out

# 16x32: ring rows 11..25
g16=ring(16,32,cx=7.5,cy=18.5,orx=5.6,ory=7.6,irx=3.4,iry=5.4,top=11,flag_rows=2,flag_x0=8)
# 12x24: ring rows 8..18
g12=ring(12,24,cx=5.5,cy=13.5,orx=4.3,ory=5.6,irx=2.5,iry=3.7,top=8,flag_rows=2,flag_x0=6)
if __name__=="__main__":
    print("16x32 sigma\n"+art(g16)); print("\n12x24 sigma\n"+art(g12))
    b16=pack(g16,16); b12=pack(g12,12)
    def c(b): return ",\n".join("    "+", ".join("0x%02X"%x for x in b[i:i+16]) for i in range(0,len(b),16))
    print("\nstatic const uint8_t sigma16x32[64] = {\n"+c(b16)+"\n};")
    print("static const uint8_t sigma12x24[48] = {\n"+c(b12)+"\n};")
