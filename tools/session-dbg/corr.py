import sys, wave, subprocess
import numpy as np
EXT=[(94408704,8254211),(102666240,8254211),(110923776,8254211),(119181312,8254211)]
DISK='/home/dmontero/6g/Emulti.img'
wav=sys.argv[1]; want=int(sys.argv[2]) if len(sys.argv)>2 else 3
def decode(trk):
    off,ln=EXT[trk]
    with open(DISK,'rb') as f: f.seek(off); data=f.read(ln)
    open('/tmp/ref.mp3','wb').write(data)
    raw=subprocess.run(['ffmpeg','-v','error','-i','/tmp/ref.mp3','-f','s16le','-ar','44100','-ac','2','-'],capture_output=True).stdout
    return np.frombuffer(raw,dtype='<i2').astype(np.float64)[0::2]
ref=decode(want)
w=wave.open(wav,'rb'); n=w.getnframes()
a=np.frombuffer(w.readframes(n),dtype='<i2').astype(np.float64); L=a[0::2]; sr=44100
# locate song: loudest 2s window
song=0
for i in range(0,max(1,len(L)-sr),sr//4):
    if np.sqrt((L[i:i+sr]**2).mean())>1500: song=i; break
bestr=(np.sqrt((L[song:song+2*sr]**2).mean()),song)
print('wav %.1fs; song region ~%.1fs (rms=%.0f)'%(len(L)/sr,song/sr,bestr[0]))
def fft_lag(seg):
    seg=seg-seg.mean(); M=len(seg); Lr=len(ref)
    N=1<<int(np.ceil(np.log2(Lr+M)))
    cc=np.fft.irfft(np.fft.rfft(ref,N)*np.fft.rfft(seg[::-1],N),N)[M-1:M-1+Lr-M+1]
    return int(np.argmax(cc))
def pear(seg,lag):
    s=seg-seg.mean(); r=ref[lag:lag+len(seg)]; r=r-r.mean()
    d=np.sqrt((s*s).sum())*np.sqrt((r*r).sum()); return (s*r).sum()/d if d else 0
# main: 5s window from song start
W=min(5*sr, len(L)-song-1)
seg=L[song:song+W]; lag=fft_lag(seg); p=pear(seg,lag)
print('MAIN: %.1fs window Pearson=%.4f at song-offset %.3fs'%(W/sr,p,lag/sr))
# tempo check: 1s windows every 2s; offset should advance ~1:1 with wav time
print('tempo (wav dt vs song-offset dt should be ~1.0):')
prev=None
for k in range(0,W//sr-1,2):
    seg=L[song+k*sr:song+k*sr+sr]
    if np.sqrt((seg**2).mean())<200: continue
    lag=fft_lag(seg); off=lag/sr
    rate='' if prev is None else '  dwav=%.1fs doff=%.2fs'%(2.0,off-prev)
    print('  wav+%2ds -> song %.3fs  P=%.3f%s'%(k,off,pear(seg,lag),rate)); prev=off
