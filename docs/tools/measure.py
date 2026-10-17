import subprocess,sys,time,struct,re,os
OC=os.environ.get("OPENOCD_DIR", "")   # openocd install dir containing bin/openocd.exe and openocd/scripts (xPack layout)
def run(cmds,t=120):
    a=[OC+"/bin/openocd.exe","-s",OC+"/openocd/scripts","-f","interface/stlink.cfg","-f","target/stm32f1x.cfg"]
    for c in cmds: a+=["-c",c]
    r=subprocess.run(a,capture_output=True,text=True,timeout=t); return r.stdout+r.stderr
def syms(elf,names):
    d=open(elf,'rb').read(); shoff=struct.unpack_from('<I',d,0x20)[0]; shentsize,shnum=struct.unpack_from('<HH',d,0x2e)
    secs=[struct.unpack_from('<IIIIIIIIII',d,shoff+i*shentsize) for i in range(shnum)]; out={}
    for s in secs:
        if s[1]==2:
            st=secs[s[6]]
            for o in range(s[4],s[4]+s[5],16):
                n,v,sz,_,_,_=struct.unpack_from('<IIIBBH',d,o); nm=d[st[4]+n:d.index(b'\0',st[4]+n)].decode()
                if nm in names: out[nm]=v
    return out
def readvars(elf,names):
    sy=syms(elf,names); cmds=["init"]+[f"echo \"V:{n}:[mdw 0x{sy[n]:08x}]\"" for n in names]+["shutdown"]
    cmds=["init"]+[f"set v [read_memory 0x{sy[n]:08x} 32 1]; echo \"V:{n}:$v\"" for n in names]+["shutdown"]
    o=run(cmds); res={}
    for n in names:
        m=re.search(rf"V:{n}:(\S+)",o); res[n]=int(m.group(1),0) if m else None
    return res
if __name__=="__main__":
    label,hexf,elf=sys.argv[1:4]
    o=run(["init","halt",f"program {hexf} verify","reset run","shutdown"]); ok="Verified OK" in o
    print(label,"flash verified:",ok,flush=True)
    time.sleep(float(sys.argv[4]) if len(sys.argv)>4 else 25)
    names=["SystemCoreClock","tp_blocks","tp_main_loops","tp_draw_last","tp_draw_min","tp_draw_max","tp_total_last","tp_total_max","tp_period_last","tp_period_min","tp_period_max","dbg_loop_per_sec"]
    r=readvars(elf,names); hz=r["SystemCoreClock"] or 72000000
    ms=lambda c: None if c in (None,0xFFFFFFFF) else round(c*1000/hz,2)
    print(label,"SYSCLK",hz,"| blocks",r["tp_blocks"],"| main loops",r["tp_main_loops"],"| speed-test LS",r["dbg_loop_per_sec"])
    print(label,"draw ms last/min/max:",ms(r["tp_draw_last"]),ms(r["tp_draw_min"]),ms(r["tp_draw_max"]),"| total block ms last/max:",ms(r["tp_total_last"]),ms(r["tp_total_max"]),"| period ms last/min/max:",ms(r["tp_period_last"]),ms(r["tp_period_min"]),ms(r["tp_period_max"]))
