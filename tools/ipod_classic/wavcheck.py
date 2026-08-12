#!/usr/bin/env python3
"""wavcheck.py <captured.wav> [reference.mp3]

Measure a captured WAV and, if given the source MP3, PROVE it is that song.

  - RMS, peak, silence fraction, duration: a WAV of zeros has RMS 0.
  - If a reference MP3 is given, decode it with ffmpeg to the SAME sample rate,
    mono, and cross-correlate the captured audio against a window of the
    reference. A high normalised correlation at a consistent lag means the
    captured PCM IS the decoded song (not noise, not silence).

Needs numpy + ffmpeg (both present in this WSL).
"""
import subprocess
import sys
import wave

import numpy as np


def load_wav_mono(path):
    w = wave.open(path, "rb")
    n, ch, sr, sw = (w.getnframes(), w.getnchannels(),
                     w.getframerate(), w.getsampwidth())
    raw = w.readframes(n)
    w.close()
    dt = {1: np.int8, 2: np.int16, 4: np.int32}[sw]
    a = np.frombuffer(raw, dtype=dt).astype(np.float64)
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return a, sr


def decode_mp3_mono(path, sr):
    """ffmpeg -> raw s16le mono at sr -> float array."""
    p = subprocess.run(
        ["ffmpeg", "-v", "quiet", "-i", path, "-ac", "1", "-ar", str(sr),
         "-f", "s16le", "-"],
        stdout=subprocess.PIPE, check=True)
    return np.frombuffer(p.stdout, dtype=np.int16).astype(np.float64)


def norm(x):
    x = x - x.mean()
    s = np.sqrt((x * x).sum())
    return x / s if s > 0 else x


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cap_path = sys.argv[1]
    cap, sr = load_wav_mono(cap_path)
    full = 32768.0
    rms = float(np.sqrt((cap ** 2).mean())) if len(cap) else 0.0
    peak = float(np.abs(cap).max()) if len(cap) else 0.0
    nz = float((cap != 0).mean()) if len(cap) else 0.0
    print("== captured ==")
    print(f"  file        {cap_path}")
    print(f"  samples     {len(cap)}  ({len(cap)/sr:.3f}s @ {sr} Hz mono)")
    print(f"  RMS         {rms:.1f}  ({20*np.log10(rms/full+1e-12):.1f} dBFS)")
    print(f"  peak        {peak:.0f}  ({20*np.log10(peak/full+1e-12):.1f} dBFS)")
    print(f"  non-zero    {100*nz:.1f}% of samples")
    if rms == 0:
        print("  VERDICT: SILENCE (all zeros) -- no audio")
        return

    if len(sys.argv) < 3:
        print("  (no reference MP3 given; RMS>0 means non-silent audio)")
        return

    ref = decode_mp3_mono(sys.argv[2], sr)
    print("== reference ==")
    print(f"  file        {sys.argv[2]}")
    print(f"  samples     {len(ref)}  ({len(ref)/sr:.1f}s @ {sr} Hz mono)")

    # FFT-based normalised cross-correlation (O(N log N)). Take a short captured
    # clip (skip leading zeros) and slide it over a window of the reference.
    nzidx = np.nonzero(cap != 0)[0]
    c0 = int(nzidx[0]) if len(nzidx) else 0
    clip = norm(cap[c0:c0 + min(len(cap) - c0, sr * 2)])   # up to 2 s
    if len(clip) < sr // 4:
        print("  captured clip too short for correlation")
        return
    ref_search = ref[:min(len(ref), sr * 40)].astype(np.float64)
    m, k = len(ref_search), len(clip)
    nfft = 1 << int(np.ceil(np.log2(m + k)))
    R = np.fft.rfft(ref_search, nfft)
    C = np.fft.rfft(clip[::-1], nfft)            # correlation = conv with reversed
    corr = np.fft.irfft(R * C, nfft)[k - 1:k - 1 + (m - k + 1)]
    # local reference energy per lag via cumulative sum of squares
    cs = np.concatenate([[0.0], np.cumsum(ref_search ** 2)])
    local = np.sqrt(np.maximum(cs[k:m + 1] - cs[0:m - k + 1], 0.0))
    local[local == 0] = 1e9
    ncorr = corr[:len(local)] / local
    lag = int(np.argmax(np.abs(ncorr)))
    best = float(np.abs(ncorr)[lag])
    print("== match ==")
    print(f"  best normalised correlation {best:.3f} at lag {lag} "
          f"({lag/sr:.3f}s into reference)")
    if best > 0.5:
        print("  VERDICT: MATCH -- the captured PCM is the decoded song")
    elif best > 0.2:
        print("  VERDICT: PARTIAL -- correlated but weak (resample/clip?)")
    else:
        print("  VERDICT: NO CLEAR MATCH (but audio is non-silent)")


if __name__ == "__main__":
    main()
