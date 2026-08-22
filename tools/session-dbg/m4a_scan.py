import sys, struct
path='/home/dmontero/6g/Emulti.img'
f=open(path,'rb'); data=f.read(); f.close()
# find all 'ftyp' box signatures (top-level box: [size:4][ 'ftyp':4 ])
files=[]
i=0
while True:
    j=data.find(b'ftyp', i)
    if j<0: break
    # ftyp box: size is 4 bytes before the tag
    if j>=4:
        boxstart=j-4
        size=struct.unpack('>I', data[boxstart:boxstart+4])[0]
        # sanity: ftyp box size small (< 64), and boxstart aligned-ish
        if 8<=size<=64:
            files.append(boxstart)
    i=j+4
print('ftyp candidates:', [hex(x) for x in files], 'count', len(files))
# For each ftyp, walk boxes to find moov end / mdat end = file extent
def walk(off):
    p=off; end=len(data)
    mdat_end=None
    boxes=[]
    while p+8<=end:
        size=struct.unpack('>I', data[p:p+4])[0]
        tag=data[p+4:p+8]
        if size==0: break
        if size==1:
            size=struct.unpack('>Q', data[p+8:p+16])[0]
        if size<8 or size>end-p: break
        boxes.append((tag.decode('latin1'), p, size))
        if tag==b'mdat': mdat_end=p+size
        p+=size
        if tag==b'mdat': break  # file ends after mdat (typical)
        if len(boxes)>12: break
    return boxes, mdat_end
for k,off in enumerate(files):
    boxes,mend=walk(off)
    fileend=mend if mend else (files[k+1] if k+1<len(files) else None)
    length=(fileend-off) if fileend else None
    print('track%d off=%d(0x%x) end=%s len=%s boxes=%s'%(k,off,off, fileend, length, [(t,hex(p),s) for t,p,s in boxes]))
