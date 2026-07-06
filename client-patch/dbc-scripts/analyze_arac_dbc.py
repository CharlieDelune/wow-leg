import struct
def load(p):
    d=open(p,'rb').read()
    nrec,nfld,rsize,sblk=struct.unpack('<IIII', d[4:20])
    recs=[struct.unpack('<%di'%nfld, d[20+i*rsize:20+i*rsize+rsize]) for i in range(nrec)]
    return (nrec,nfld,rsize,sblk,recs)
van=load("client/vanilla/DBFilesClient/SkillRaceClassInfo.dbc")
mod=load("source/client-patch/overlay/DBFilesClient/SkillRaceClassInfo.dbc")
ar =load("client/arac/patch-contents/DBFilesContent/SkillRaceClassInfo.dbc")
for lbl,x in [("vanilla",van),("OUR modified",mod),("ARAC",ar)]:
    print(f"{lbl}: nrec={x[0]} nfld={x[1]} rsize={x[2]}")
# ARAC vs vanilla: record IDs added/removed, and which fields changed on shared IDs
def byid(recs): return {r[0]:r for r in recs}
vb, ab = byid(van[4]), byid(ar[4])
added=sorted(set(ab)-set(vb)); removed=sorted(set(vb)-set(ab))
print(f"\nARAC vs vanilla: +{len(added)} records added, -{len(removed)} removed")
print("  added IDs sample:", added[:10])
# field-change histogram on shared IDs
from collections import Counter
chg=Counter()
sample=[]
for i in set(vb)&set(ab):
    for f in range(8):
        if vb[i][f]!=ab[i][f]:
            chg[f]+=1
            if len(sample)<6: sample.append((i,f,vb[i][f],ab[i][f]))
print("  fields changed on shared IDs (fieldIdx->count):", dict(chg))
print("  samples (id, field, vanilla, ARAC):", sample)
# field meaning: 2=RaceMask, 3=ClassMask
print("\nARAC RaceMask(f2) distribution:", Counter(r[2] for r in ar[4]).most_common(5))
print("ARAC ClassMask(f3) distribution:", Counter(r[3] for r in ar[4]).most_common(5))
