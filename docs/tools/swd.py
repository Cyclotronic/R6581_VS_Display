import socket,time,re,threading
_LOCK=threading.Lock()
class SWD:
    def __init__(s,port=4444):
        s.c=socket.create_connection(("127.0.0.1",port),timeout=5); s._read_until(b"> ")
    def _read_until(s,tok):
        b=b""
        while not b.endswith(tok): b+=s.c.recv(4096)
        return b
    def mem(s,addr,n):
        s.c.sendall(f"read_memory 0x{addr:08x} 8 {n}\r\n".encode())
        r=s._read_until(b"> ").decode(errors="replace")
        nums=re.findall(r"(?<![\w])(0x[0-9a-fA-F]+|\d+)(?![\w:])",r.split("\n",1)[-1] if "read_memory" in r.split("\n")[0] else r)
        return bytes(int(x,0)&0xff for x in nums[-n:])
NAMES="SMPL IDLE AUTO LOP NULL DFILT MATH AZERO ERR INFO FRONT REAR SLOT LO_G RMT TLK LTN SRQ".split()
ADDR=dict(G=0x20000278,Annunc=0x200002a8,um=0x200002e4,chars=0x200003e4)   # defaults: Ian's R6581_VS_Display.hex build
def use_elf(path):
    """Take the variable addresses from another build's ELF (symbol table)."""
    import struct
    d=open(path,'rb').read(); shoff=struct.unpack_from('<I',d,0x20)[0]; shentsize,shnum=struct.unpack_from('<HH',d,0x2e)
    secs=[struct.unpack_from('<IIIIIIIIII',d,shoff+i*shentsize) for i in range(shnum)]
    want={'G':'G','Annunc':'Annunc','um':'unmatchedBitmap','chars':'chars'}; inv={v:k for k,v in want.items()}
    for s in secs:
        if s[1]==2:
            st=secs[s[6]]
            for o in range(s[4],s[4]+s[5],16):
                n,v,sz,_,_,_=struct.unpack_from('<IIIBBH',d,o); nm=d[st[4]+n:d.index(b'\x00',st[4]+n)].decode()
                if nm in inv: ADDR[inv[nm]]=v
def snap(w):
    with _LOCK:
        G=w.mem(ADDR['G'],48);A=w.mem(ADDR['Annunc'],19);um=w.mem(ADDR['um'],7);ch=w.mem(ADDR['chars'],329)
    main=G[1:19].decode('latin1');aux=G[19:48].decode('latin1')
    miss=[(i+1,ch[i*7:i*7+7].hex(' ')) for i in range(47) if G[i+1:i+2]==b'?']
    return dict(main=main,aux=aux,ann=[NAMES[i-1] for i in range(1,19) if A[i]],um=um.hex(' '),miss=miss)
if __name__=="__main__":
    w=SWD(); import json
    t=time.time(); r=snap(w); print(round(time.time()-t,3),'s'); print(json.dumps(r))
