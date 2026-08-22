#!/usr/bin/env python3
# Search the guest-DRAM dump for on-disk AAC bytes of each track.
# guest_phys = 0x08000000 + offset_in_dump  (SDRAM base on S5L8702).
import sys, os
DRAM_BASE = 0x08000000
dump = '/home/dmontero/6g/dram.bin'
carve = '/home/dmontero/6g/carve'
mdat = {0:(51240,1650579),1:(51240,2648834),2:(66078,5169117),
        3:(51240,917277),4:(51240,2454490),5:(51240,2112296)}

d = open(dump,'rb').read()
print('dump size = %d bytes (%.1f MB)'%(len(d), len(d)/1048576))

def find_all(hay, needle, limit=6):
    out=[]; i=0
    while len(out)<limit:
        j=hay.find(needle,i)
        if j<0: break
        out.append(j); i=j+1
    return out

for t in range(6):
    fn='%s/t%d.m4a'%(carve,t)
    if not os.path.exists(fn): continue
    f=open(fn,'rb').read()
    mo,ms=mdat[t]
    needles=[('file@0x2000',   f[0x2000:0x2000+64]),
             ('mdat+0x2000',   f[mo+0x2000:mo+0x2000+64]),
             ('mdat+0x40000',  f[mo+0x40000:mo+0x40000+64] if ms>0x40000 else b''),
             ('mdat+0x100000', f[mo+0x100000:mo+0x100000+64] if ms>0x100000 else b'')]
    print('\n=== TRACK t%d (%s) mdat@%d size=%d ==='%(t,fn,mo,ms))
    hit_any=False
    for name,nd in needles:
        if not nd: continue
        locs=find_all(d,nd)
        if locs:
            hit_any=True
            print('  %-14s -> %s'%(name, ', '.join('0x%08X'%(DRAM_BASE+x) for x in locs)))
        else:
            print('  %-14s -> (none)'%name)
    if hit_any:
        # measure contiguous coverage: anchor on mdat+0x2000 first hit, align file mdat to dram
        nd=f[mo+0x2000:mo+0x2000+64]; locs=find_all(d,nd,1)
        if locs:
            dram_off=locs[0]           # dram offset of file byte (mo+0x2000)
            file_anchor=mo+0x2000
            base_dram_off=dram_off-file_anchor   # dram offset that maps to file byte 0
            # walk mdat comparing file vs dram to find contiguous run around anchor
            # forward
            fwd=0; step=4096
            while file_anchor+fwd+step<=mo+ms and base_dram_off+file_anchor+fwd+step<=len(d):
                a=f[file_anchor+fwd:file_anchor+fwd+step]
                b=d[base_dram_off+file_anchor+fwd:base_dram_off+file_anchor+fwd+step]
                if a!=b: break
                fwd+=step
            bwd=0
            while file_anchor-bwd-step>=0 and base_dram_off+file_anchor-bwd-step>=0:
                a=f[file_anchor-bwd-step:file_anchor-bwd]
                b=d[base_dram_off+file_anchor-bwd-step:base_dram_off+file_anchor-bwd]
                if a!=b: break
                bwd+=step
            lo_file=file_anchor-bwd; hi_file=file_anchor+fwd
            print('  CONTIG (>=4K step): file[%d..%d] len=%d  guest 0x%08X..0x%08X'%(
                lo_file,hi_file,hi_file-lo_file,
                DRAM_BASE+base_dram_off+lo_file, DRAM_BASE+base_dram_off+hi_file))
