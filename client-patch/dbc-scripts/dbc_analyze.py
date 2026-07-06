import struct, sys

def hdr(path):
    with open(path,'rb') as f: d=f.read()
    magic=d[0:4]; nrec,nfld,rsize,sblk=struct.unpack('<IIII', d[4:20])
    return d, nrec, nfld, rsize, sblk

def fields(d, nrec, nfld, rsize):
    # returns list of records, each a tuple of nfld int32 (assumes all int32)
    recs=[]
    off=20
    for i in range(nrec):
        rec=struct.unpack('<%di'%nfld, d[off:off+rsize])
        recs.append(rec); off+=rsize
    return recs

for label,path in [
    ("vanilla SkillLineAbility","client/vanilla/DBFilesClient/SkillLineAbility.dbc"),
    ("MODIFIED SkillLineAbility","source/client-patch/overlay/DBFilesClient/SkillLineAbility.dbc"),
    ("vanilla SkillRaceClassInfo","client/vanilla/DBFilesClient/SkillRaceClassInfo.dbc"),
    ("server SkillRaceClassInfo","install/bin/dbc/SkillRaceClassInfo.dbc"),
]:
    try:
        d,nrec,nfld,rsize,sblk=hdr(path)
        print(f"{label}: magic={d[0:4]} nrec={nrec} nfld={nfld} rsize={rsize} sblk={sblk}  (rsize/nfld={rsize/nfld if nfld else '?'})")
    except Exception as e:
        print(f"{label}: ERR {e}")

print("\n=== SkillLineAbility: diff modified vs vanilla (which field(s) changed) ===")
dv,nv,fv,rv,sv=hdr("client/vanilla/DBFilesClient/SkillLineAbility.dbc")
dm,nm,fm,rm,sm=hdr("source/client-patch/overlay/DBFilesClient/SkillLineAbility.dbc")
rvv=fields(dv,nv,fv,rv); rmm=fields(dm,nm,fm,rm)
changed_fields={}
sample=[]
for a,b in zip(rvv,rmm):
    for idx,(x,y) in enumerate(zip(a,b)):
        if x!=y:
            changed_fields[idx]=changed_fields.get(idx,0)+1
            if len(sample)<8: sample.append((a[0], idx, x, y))
print("fields changed (fieldIndex -> count):", changed_fields)
print("samples (recID, fieldIdx, vanilla, modified):")
for s in sample: print("  ", s)

print("\n=== SkillLineAbility field 4 (ClassMask) value distribution: vanilla vs modified ===")
from collections import Counter
cv=Counter(r[4] for r in rvv); cm=Counter(r[4] for r in rmm)
print("vanilla ClassMask top:", cv.most_common(6))
print("modified ClassMask top:", cm.most_common(6))
