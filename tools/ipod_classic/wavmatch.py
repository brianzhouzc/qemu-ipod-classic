#!/usr/bin/env python3
"""wavmatch.py <captured.wav> <reference.mp3>

The STRONG proof that a captured WAV is the continuous decode of a song (not a
loop, not fake-mono, not a lucky 2-second coincidence). Complements wavcheck.py:

  - Stereo check: L RMS, R RMS, RMS(L-R) and the fraction of frames where L==R
    exactly. Real stereo has RMS(L-R) well above zero; a fake-mono dump has it 0.
  - Continuity check: take 1.5 s clips at several offsets into the CAPTURE and
    cross-correlate each against the whole reference (decoded by ffmpeg at the
    capture's own sample rate). If the best lag advances by exactly the clip
    spacing, with correlation ~1.0 at each, the capture is a linear walk through
    the real song.

numpy + ffmpeg only (both present in this WSL). See wiki/16 s.7.
"""
import subprocess
import sys
import wave

import numpy as np


def load_stereo(path):
    w = wave.open(path, "rb")
    n, ch, sr, sw = (w.getnframes(), w.getnchannels(),
                     w.getframerate(), w.getsampwidth())
    raw = w.readframes(n)
    w.close()
    dt = {1: np.int8, 2: np.int16, 4: np.int32}[sw]
    a = np.frombuffer(raw, dtype=dt).astype(np.float64).reshape(-1, ch)
    return a, sr, sw


def decode_mp3_mono(path, sr):
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
    """Normalised cross-correlation of clip over ref; return (lag, |corr|)."""
    clip = norm(clip)
    m, k = len(ref), len(clip)
    nfft = 1 << int(np.ceil(np.log2(m + k)))
    R = np.fft.rfft(ref, nfft)
    C = np.fft.rfft(clip[::-1], nfft)          # correlation = conv w/ reversed
    corr = np.fft.irfft(R * C, nfft)[k - 1:k - 1 + (m - k + 1)]
    cs = np.concatenate([[0.0], np.cumsum(ref ** 2)])
    local = np.sqrt(np.maximum(cs[k:m + 1] - cs[0:m - k + 1], 0.0))
    local[local == 0] = 1e9
    nc = corr[:len(local)] / local
    lag = int(np.argmax(np.abs(nc)))
    return lag, float(np.abs(nc)[lag])


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    a, sr, sw = load_stereo(sys.argv[1])
    n = len(a)
    L = a[:, 0]
    R = a[:, 1] if a.shape[1] > 1 else a[:, 0]
    mono = a.mean(axis=1)
    print("== capture ==")
    print(f"  {n} frames, {a.shape[1]}ch, {sr} Hz, {sw*8}-bit, {n/sr:.2f}s")
    print(f"  L RMS {np.sqrt((L**2).mean()):.1f}   R RMS {np.sqrt((R**2).mean()):.1f}")
    print(f"  RMS(L-R) {np.sqrt(((L-R)**2).mean()):.1f}   (0 == fake-mono)")
    print(f"  L==R exactly for {100*np.mean(L==R):.2f}% of frames")

    ref = decode_mp3_mono(sys.argv[2], sr)
    print("== reference ==")
    print(f"  {sys.argv[2]}  ({len(ref)/sr:.1f}s @ {sr} Hz)")

    print("== continuity (1.5 s capture clips vs the whole song) ==")
    clip_len = int(1.5 * sr)
    offsets = [t for t in (3, 8, 13, 18, 23) if t * sr + clip_len <= n]
    prev = None
    ok = True
    for t in offsets:
        clip = mono[t * sr:t * sr + clip_len]
        lag, c = best_lag(clip, ref)
        step = "" if prev is None else f"  (+{(lag-prev)/sr:.2f}s vs prev +5.00s)"
        print(f"  cap t={t:2d}s -> ref {lag/sr:7.3f}s  corr={c:.3f}{step}")
        if c < 0.5:
            ok = False
        if prev is not None and abs((lag - prev) / sr - 5.0) > 0.05:
            ok = False
        prev = lag
    print("VERDICT:", "MATCH -- continuous decode of the real song, in stereo"
          if ok and offsets else "NO CLEAR MATCH")
    raise SystemExit(0 if ok and offsets else 1)


if __name__ == "__main__":
    main()
