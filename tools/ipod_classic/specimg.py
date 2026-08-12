#!/usr/bin/env python3
"""specimg.py <in.wav> <out.png> [seconds]

Render a log-magnitude spectrogram of a WAV to a PNG (numpy only, no matplotlib)
so you can SEE that the capture is music: harmonic bands and beats, not a flat
noise floor or an empty (silent) field. Time on X, frequency on Y (0..sr/2),
a simple blue->green->yellow->red heatmap of energy in dB.
"""
import struct
import sys
import wave
import zlib

import numpy as np


def load_mono(path, seconds):
    w = wave.open(path, "rb")
    sr, ch, sw, n = (w.getframerate(), w.getnchannels(),
                     w.getsampwidth(), w.getnframes())
    n = min(n, int(sr * seconds)) if seconds else n
    raw = w.readframes(n)
    w.close()
    dt = {1: np.int8, 2: np.int16, 4: np.int32}[sw]
    a = np.frombuffer(raw, dtype=dt).astype(np.float64)
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return a, sr


def spectrogram(a, sr, nfft=1024, hop=512):
    win = np.hanning(nfft)
    cols = 1 + (len(a) - nfft) // hop if len(a) >= nfft else 0
    S = np.empty((nfft // 2 + 1, cols))
    for i in range(cols):
        seg = a[i * hop:i * hop + nfft] * win
        S[:, i] = np.abs(np.fft.rfft(seg))
    db = 20 * np.log10(S + 1e-6)
    return db


def colormap(x):
    """x in [0,1] -> (r,g,b) blue->cyan->green->yellow->red."""
    r = np.clip(1.5 * x - 0.5, 0, 1)
    g = np.clip(1.5 - np.abs(2 * x - 1) * 1.5 + 0.0, 0, 1)
    g = np.clip(1.0 - np.abs(x - 0.6) * 2.0, 0, 1)
    b = np.clip(1.0 - 1.8 * x, 0, 1)
    return r, g, b


def write_png(path, rgb):  # rgb: HxWx3 uint8
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xffffffff))
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b""))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 12.0
    a, sr = load_mono(sys.argv[1], seconds)
    db = spectrogram(a, sr)
    # normalise to visible range
    lo, hi = np.percentile(db, 5), np.percentile(db, 99)
    x = np.clip((db - lo) / (hi - lo + 1e-9), 0, 1)
    # flip so low freq at bottom; downsample height to <=256
    x = x[::-1]
    H = x.shape[0]
    if H > 256:
        idx = np.linspace(0, H - 1, 256).astype(int)
        x = x[idx]
    r, g, b = colormap(x)
    rgb = np.stack([r, g, b], axis=-1)
    rgb = (rgb * 255).astype(np.uint8)
    write_png(sys.argv[2], rgb)
    print(f"{sys.argv[2]}  {rgb.shape[1]}x{rgb.shape[0]}  "
          f"({len(a)/sr:.1f}s @ {sr} Hz)")


if __name__ == "__main__":
    main()
