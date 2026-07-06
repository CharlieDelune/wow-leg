import struct, shutil, os
src="client/vanilla/DBFilesClient/SkillRaceClassInfo.dbc"
dst="source/client-patch/overlay/DBFilesClient/SkillRaceClassInfo.dbc"
d=bytearray(open(src,'rb').read())
nrec,nfld,rsize,sblk=struct.unpack('<IIII', d[4:20])
assert nfld==8 and rsize==32, (nfld,rsize)
CLASSMASK_FIELD=3
changed=0
for i in range(nrec):
    off=20 + i*rsize + CLASSMASK_FIELD*4
    if d[off:off+4] != b'\xff\xff\xff\xff':
        struct.pack_into('<i', d, off, -1)   # 0xFFFFFFFF = all classes
        changed+=1
open(dst,'wb').write(d)
print(f"wrote {dst}: {nrec} records, set ClassMask=-1 on {changed} records, size={len(d)} (vanilla={os.path.getsize(src)})")
# verify round-trip
v=open(dst,'rb').read()
recs=[struct.unpack('<8i', v[20+i*32:20+i*32+32]) for i in range(nrec)]
from collections import Counter
print("modified ClassMask(field3) distribution:", Counter(r[3] for r in recs).most_common(3))
