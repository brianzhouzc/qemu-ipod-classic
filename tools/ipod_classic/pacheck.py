#!/usr/bin/env python3
"""pacheck.py <pa_capture.wav> <reference.mp3>

Prove that a PulseAudio-monitor capture of the LIVE audio (AUDIOLIVE=1, -audio pa)
is the song -- even when it plays at a non-1.0 speed. Companion to wavmatch.py:
wavmatch.py proves the AUDIOWAV *file* is the song; this proves what actually came
OUT of the pa backend is the song.

HOW TO CAPTURE (WSL, while a live run plays):
    parecord --device=RDPSink.monitor --file-format=wav /tmp/pa.wav &
    sh tools/ipod_classic/rbmusic.sh    # or any AUDIOLIVE=1 -audio pa run
    # ...let it play, then stop parecord...
    python3 tools/ipod_classic/pacheck.py /tmp/pa.wav ~/6g/tracks/'Numb - Encore.mp3'

WHAT IT REPORTS
  - the non-silent (active) region, its peak and stereo RMS(L-R) (0 == fake-mono);
  - per-clip normalised cross-correlation of 1.5 s capture clips against the whole
    reference, and the implied playback speed from the clip lags.
A capture whose clips correlate high and whose lags advance monotonically at a
steady speed IS a continuous decode of the song (at that speed). NOTE: recording
the monitor steals host CPU from a decode-limited emulation, which injects gaps
into the *recording*; a clean speaker experience needs an idle/fast host. numpy +
ffmpeg only.
"""
import subprocess
import sys
import wave

import numpy as np


def load(path):
    w = wave.open(path, "rb")
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    w.close()
    return a.reshape(-1, ch), sr


def decode(path, sr):
    p = subprocess.run(
        ["ffmpeg", "-v", "quiet", "-i", path, "-ac", "1", "-ar", str(sr),
         "-f", "s16le", "-"],
        stdout=subprocess.PIPE, check=True)
    return np.frombuffer(p.stdout, dtype=np.int16).astype(np.float64)


def norm(x):
    x = x - x.mean()
    s = np.sqrt((x * x).sum())
    return x / s if s > 0 else x


def best_lag(clip, ref):
    clip = norm(clip)
    m, k = len(ref), len(clip)
    nfft = 1 << int(np.ceil(np.log2(m + k)))
    corr = np.fft.irfft(np.fft.rfft(ref, nfft) * np.fft.rfft(clip[::-1], nfft),
                        nfft)[k - 1:k - 1 + (m - k + 1)]
    cs = np.concatenate([[0.0], np.cumsum(ref ** 2)])
    local = np.sqrt(np.maximum(cs[k:m + 1] - cs[0:m - k + 1], 0.0))
    local[local == 0] = 1e9
    nc = corr[:len(local)] / local
    lag = int(np.argmax(np.abs(nc)))
    return lag, float(np.abs(nc)[lag])


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    pa, sr = load(sys.argv[1])
    mono = pa.mean(axis=1)
    act = np.where(np.abs(mono) > 150)[0]
    if len(act) == 0:
        print("CAPTURE IS SILENT")
        raise SystemExit(1)
    s, e = act[0], act[-1]
    seg = pa[s:e]
    sL, sR = seg[:, 0], seg[:, 1] if seg.shape[1] > 1 else seg[:, 0]
    print(f"pa capture {sr} Hz, {len(pa)/sr:.1f}s total; "
          f"active {s/sr:.2f}..{e/sr:.2f}s ({(e-s)/sr:.1f}s)")
    print(f"  peak={int(np.abs(seg).max())}  "
          f"RMS(L-R)={np.sqrt(((sL-sR)**2).mean()):.0f} (0==fake-mono)  "
          f"L==R {100*np.mean(sL==sR):.1f}%")

    ref = decode(sys.argv[2], sr)
    ts, lags, cors = [], [], []
    t = 2.0
    clip_len = int(1.5 * sr)
    while s + int(t * sr) + clip_len < e and t < 40:
        s0 = s + int(t * sr)
        lag, c = best_lag(mono[s0:s0 + clip_len], ref)
        ts.append(t); lags.append(lag / sr); cors.append(c)
        print(f"  cap +{t:4.1f}s -> ref {lag/sr:7.3f}s  corr={c:.3f}")
        t += 4.0
    if len(ts) < 3:
        print("VERDICT: too little active audio to judge")
        raise SystemExit(1)
    ts, lags, cors = np.array(ts), np.array(lags), np.array(cors)
    speed = np.polyfit(ts, lags, 1)[0]
    mono_inc = bool(np.all(np.diff(lags) > 0))
    good = float(np.mean(cors > 0.85))
    print(f"fitted playback speed={speed:.3f}x  clips corr>0.85: {100*good:.0f}%  "
          f"lags increasing: {mono_inc}")
    ok = mono_inc and (good >= 0.6 or (cors > 0.7).mean() >= 0.6)
    print("VERDICT:", f"MATCH -- the song came out of pa (at {speed:.2f}x)"
          if ok else "NO CLEAR MATCH (host-load gaps in the recording?)")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
