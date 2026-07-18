#!/usr/bin/env python3
"""FEB (FNA3D Effect Binary) Builder — with compute shader support.
"""
import json, os, re, struct, subprocess, sys

PARAM_TYPES = {
    "FLOAT":0,"FLOAT2":1,"FLOAT3":2,"FLOAT4":3,"INT":4,"BOOL":5,
    "MATRIX":6,"TEXTURE":7,"TEXTURE1D":8,"TEXTURE2D":9,"TEXTURE3D":10,"TEXTURECUBE":11}
SHADER_STAGE_VERTEX,SHADER_STAGE_PIXEL,SHADER_STAGE_COMPUTE=0,1,2
FEB_MAGIC,FEB_VERSION,HEADER_SIZE=0x42414E46,2,64
SHADER_ENTRY_SIZE=52
SPIRV_MAGIC=0x07230203
OP_DECORATE,OP_VARIABLE,OP_EXECUTION_MODE=71,59,16
DECORATION_BINDING,DECORATION_DESCRIPTOR_SET=33,34
DECORATION_NON_WRITABLE,DECORATION_NON_READABLE=5287,5288
STORAGE_CLASS_UNIFORM_CONSTANT,STORAGE_CLASS_UNIFORM,STORAGE_CLASS_STORAGE_BUFFER=0,2,12
EXECUTION_MODE_LOCAL_SIZE=17

def scan_hlsl_registers(src):
    src=re.sub(r"//[^\n]*","",src); src=re.sub(r"/\*.*?\*/","",src,flags=re.DOTALL)
    t_indices,s_indices,u_indices=set(),set(),set()
    for kind,idx in re.findall(r"register\(\s*([tsu])(\d+)",src):
        idx=int(idx)
        if kind=="t": t_indices.add(idx)
        elif kind=="s": s_indices.add(idx)
        else: u_indices.add(idx)
    return t_indices,s_indices,u_indices

def compile_hlsl_to_spirv(src_path,entry,stage):
    profile={"vertex":"vs_6_0","pixel":"ps_6_0","compute":"cs_6_0"}[stage]
    out=src_path+".spv"
    if stage=="vertex": gs,ss="1","0"
    elif stage=="pixel": gs,ss="3","2"
    else: gs,ss="2","0"
    cmd=["dxc","-spirv","-T",profile,"-E",entry,src_path,"-Fo",out,"-fvk-bind-globals","0",gs]
    with open(src_path) as f: ti,si,ui=scan_hlsl_registers(f.read())
    if stage=="compute":
        for i in sorted(ui): cmd.extend(["-fvk-bind-register",f"u{i}","0",str(i),"1"])
        for i in sorted(ti): cmd.extend(["-fvk-bind-register",f"t{i}","0",str(i),"0"])
        for i in sorted(si): cmd.extend(["-fvk-bind-register",f"s{i}","0",str(i),"0"])
    else:
        for i in sorted(ti|si):
            cmd.extend(["-fvk-bind-register",f"t{i}","0",str(i),ss])
            cmd.extend(["-fvk-bind-register",f"s{i}","0",str(i),ss])
    r=subprocess.run(cmd,capture_output=True,text=True)
    if r.returncode!=0:
        print(f"DXC failed: {src_path}:{entry}",file=sys.stderr); print(r.stderr,file=sys.stderr); sys.exit(1)
    if r.stderr: print(f"DXC [{src_path}:{entry}]: {r.stderr.strip()}")
    with open(out,"rb") as f: spv=f.read()
    os.remove(out)
    return spv

def reflect_spirv(spv,stage):
    if len(spv)<20 or len(spv)%4!=0: raise ValueError("Bad SPIR-V")
    words=struct.unpack(f"<{len(spv)//4}I",spv)
    if words[0]!=SPIRV_MAGIC: raise ValueError("Bad magic")
    if stage=="vertex": ss,gs=0,1
    elif stage=="pixel": ss,gs=2,3
    else: ss,gs=0,2
    decos={}; vars_=[]
    tc=[1,1,1]
    pos=5
    while pos<len(words):
        wc,op=words[pos]>>16,words[pos]&0xFFFF
        if wc==0: raise ValueError("Bad instruction")
        if op==OP_DECORATE and wc>=4:
            t,d,v=words[pos+1],words[pos+2],words[pos+3]
            decos.setdefault(t,{})[d]=v
        elif op==OP_VARIABLE and wc>=4: vars_.append((words[pos+2],words[pos+3],words[pos+1]))
        elif op==OP_EXECUTION_MODE and wc>=3:
            if words[pos+2]==EXECUTION_MODE_LOCAL_SIZE and wc>=6: tc=[words[pos+3],words[pos+4],words[pos+5]]
        pos+=wc
    sc,rost,rosb,rwst,rwsb,uc=0,0,0,0,0,0
    for rid,sc_,tid in vars_:
        dec=decos.get(rid,{})
        dset=dec.get(DECORATION_DESCRIPTOR_SET,0)
        nw=DECORATION_NON_WRITABLE in dec; nr=DECORATION_NON_READABLE in dec
        if sc_==STORAGE_CLASS_UNIFORM_CONSTANT:
            if nw and not nr: rost+=1
            elif nr and not nw: rwst+=1
            else: sc+=1
        elif sc_==STORAGE_CLASS_STORAGE_BUFFER:
            if nr and not nw: rwsb+=1
            else: rosb+=1
        elif sc_==STORAGE_CLASS_UNIFORM:
            if dset==gs and not nw and not nr: uc+=1
            elif nr and not nw: rwsb+=1
            else: rosb+=1
    return {"samplers":sc+rost,"uniforms":uc,"threadCountX":tc[0],"threadCountY":tc[1],"threadCountZ":tc[2],
        "readonlyStorageBufferCount":rosb,"readwriteStorageBufferCount":rwsb,
        "readonlyStorageTextureCount":rost,"readwriteStorageTextureCount":rwst}

class StringTable:
    def __init__(self): self.ss=[]; self.offs={}
    def add(self,s):
        if s in self.offs: return self.offs[s]
        o=sum(len(b) for b in self.ss); self.ss.append(s.encode()+b"\0"); self.offs[s]=o; return o
    def data(self): return b"".join(self.ss)
    def size(self): return sum(len(b) for b in self.ss)

def pack_default(ptype,default):
    v=[0.0]*16
    if ptype=="MATRIX" and default is not None:
        for i in range(min(16,len(default))): v[i]=float(default[i])
    elif ptype in ("FLOAT","FLOAT2","FLOAT3","FLOAT4"):
        c={"FLOAT":1,"FLOAT2":2,"FLOAT3":3,"FLOAT4":4}[ptype]
        if default is not None:
            for i in range(min(c,len(default))): v[i]=float(default[i])
    elif ptype in ("INT","BOOL"):
        if default is not None: v[0]=float(int(default[0] if isinstance(default,list) else default))
    return struct.pack("<16f",*v)

def build_feb(mf,mfd):
    s=StringTable(); techs=mf["techniques"]; params=mf.get("parameters",[])
    entries=[]
    for tech in techs:
        for p in tech["passes"]:
            for ck,stage in [("vertexShader","vertex"),("pixelShader","pixel"),("computeShader","compute")]:
                shd=p.get(ck)
                if shd:
                    src=os.path.join(mfd,shd["source"]); spv=compile_hlsl_to_spirv(src,shd["entry"],stage)
                    info=reflect_spirv(spv,stage)
                    entries.append(({"vertex":0,"pixel":1,"compute":2}[stage],shd["entry"],spv,
                        info["samplers"],info["uniforms"],
                        info["threadCountX"],info["threadCountY"],info["threadCountZ"],
                        info["readonlyStorageBufferCount"],info["readwriteStorageBufferCount"],
                        info["readonlyStorageTextureCount"],info["readwriteStorageTextureCount"]))
    tc,pc,nc,sc=len(techs),sum(len(t["passes"]) for t in techs),len(params),len(entries)
    for tech in techs:
        s.add(tech["name"])
        for p in tech["passes"]: s.add(p["name"])
    for param in params: s.add(param["name"]); s.add(param.get("semantic",""))
    for e in entries: s.add(e[1])
    std=s.data(); sts=s.size()
    off=HEADER_SIZE+sts; param_off=off
    psz=[84+len(param.get("annotations",[]))*40 for param in params]; pss=sum(psz)
    off+=pss; tech_off=off; tss=tc*16
    off+=tss; pass_off=off; pss2=pc*24
    off+=pss2; shader_off=off; sss=sc*SHADER_ENTRY_SIZE
    off+=sss; spirv_off=off
    sps=[]; sos=[]; cso=0
    for e in entries: sps.append(e[2]); sos.append(cso); cso+=len(e[2])
    body=bytearray(); body.extend(std)
    for param in params:
        no=s.offs[param["name"]]; so=s.offs[param.get("semantic","")]; pt=PARAM_TYPES[param["type"]]
        reg=param.get("register",0); dv=pack_default(param["type"],param.get("default"))
        body.extend(struct.pack("<I",no)); body.extend(struct.pack("<I",so))
        body.extend(struct.pack("<B",pt)); body.extend(b"\0\0\0")
        body.extend(struct.pack("<I",reg)); body.extend(dv)
        anns=param.get("annotations",[]); body.extend(struct.pack("<I",len(anns)))
        for ann in anns:
            ano=s.add(ann["name"]); at=PARAM_TYPES[ann["type"]]; av=pack_default(ann["type"],ann.get("value"))
            body.extend(struct.pack("<I",ano)); body.extend(struct.pack("<B",at)); body.extend(b"\0\0\0"); body.extend(av[:32])
    pi=0
    for tech in techs:
        no=s.offs[tech["name"]]; ps=pi; pcc=len(tech["passes"])
        body.extend(struct.pack("<I",no)); body.extend(struct.pack("<I",ps))
        body.extend(struct.pack("<I",pcc)); body.extend(struct.pack("<I",0)); pi+=pcc
    sim={}
    for idx,e in enumerate(entries): sim[(e[0],e[1])]=idx
    for tech in techs:
        for p in tech["passes"]:
            no=s.offs[p["name"]]
            vs_idx=sim.get((SHADER_STAGE_VERTEX,p.get("vertexShader",{}).get("entry","")),-1)
            ps_idx=sim.get((SHADER_STAGE_PIXEL,p.get("pixelShader",{}).get("entry","")),-1)
            cs_idx=sim.get((SHADER_STAGE_COMPUTE,p.get("computeShader",{}).get("entry","")),-1)
            rsc=len(p.get("renderStates",[])); ssc=len(p.get("samplerStates",[]))
            body.extend(struct.pack("<I",no)); body.extend(struct.pack("<i",vs_idx)); body.extend(struct.pack("<i",ps_idx))
            body.extend(struct.pack("<I",rsc)); body.extend(struct.pack("<I",ssc)); body.extend(struct.pack("<i",cs_idx))
    for idx,e in enumerate(entries):
        stg,en,spv,smpl,unf,tx,ty,tz,rob,rwb,rot,rwt=e
        eo=s.offs[en]; sdo=sos[idx]; sds=len(spv)
        body.extend(struct.pack("<B",stg)); body.extend(b"\0\0\0"); body.extend(struct.pack("<I",eo))
        body.extend(struct.pack("<I",sdo)); body.extend(struct.pack("<I",sds))
        body.extend(struct.pack("<I",smpl)); body.extend(struct.pack("<I",unf))
        body.extend(struct.pack("<I",tx)); body.extend(struct.pack("<I",ty)); body.extend(struct.pack("<I",tz))
        body.extend(struct.pack("<I",rob)); body.extend(struct.pack("<I",rwb))
        body.extend(struct.pack("<I",rot)); body.extend(struct.pack("<I",rwt))
    for e in entries: body.extend(e[2])
    total=HEADER_SIZE+len(body)
    hdr=struct.pack("<16I",FEB_MAGIC,FEB_VERSION,tc,pc,nc,sc,sts,param_off,tech_off,pass_off,shader_off,spirv_off,total,0,0,0)
    return bytes(hdr+body)

def main():
    if len(sys.argv)<2: print(f"Usage: {sys.argv[0]} <manifest.json>",file=sys.stderr); sys.exit(1)
    mp=sys.argv[1]; md=os.path.dirname(os.path.abspath(mp))
    with open(mp) as f: mf=json.load(f)
    if mp.endswith(".feb.json"): op=mp[:-5]
    elif mp.endswith(".json"): op=mp[:-5]+".feb"
    else: op=mp+".feb"
    feb=build_feb(mf,md)
    with open(op,"wb") as f: f.write(feb)
    print(f"Built {op} ({len(feb)} bytes, {len(mf['techniques'])} techniques, {sum(len(t['passes']) for t in mf['techniques'])} passes)")

if __name__=="__main__": main()
