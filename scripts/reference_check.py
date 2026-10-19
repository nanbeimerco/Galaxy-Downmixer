"""Cross-checks gdm_cli against an independent NumPy/SciPy implementation.

Generates a 7.1.4 noise file, runs it through gdm_cli with the sample EQ
file, mute, swap and master gain, recomputes the expected stereo output here
and compares sample by sample.

    python scripts/reference_check.py [path/to/gdm_cli.exe]
"""

import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "pc", "Release", "gdm_cli.exe")
EQ = os.path.join(ROOT, "dsp", "tests", "data", "ig955_eq.txt")
FS = 48000
MASK_714 = 0x2D63F
NAMES = ["FL", "FR", "FC", "LFE", "BL", "BR", "SL", "SR", "TFL", "TFR", "TBL", "TBR"]
MATRIX = {  # name -> (L, R)
    "FL": (1, 0), "FR": (0, 1), "FC": (0.70710678, 0.70710678),
    "LFE": (2.26464431, 2.26464431), "BL": (1, 0), "BR": (0, 1),
    "SL": (1, 0), "SR": (0, 1), "TFL": (1, 0), "TFR": (0, 1),
    "TBL": (1, 0), "TBR": (0, 1),
}
DELAY = 172


def write_wav_float(path, data, mask):
    frames, ch = data.shape
    raw = data.astype("<f4").tobytes()
    guid = bytes([3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71])
    fmt = struct.pack("<HHIIHHHHI", 0xFFFE, ch, FS, FS * ch * 4, ch * 4, 32, 22, 32, mask) + guid
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(raw)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
        f.write(b"data" + struct.pack("<I", len(raw)) + raw)


def read_wav_float(path):
    with open(path, "rb") as f:
        d = f.read()
    pos, ch, data = 12, None, None
    while pos + 8 <= len(d):
        cid, size = d[pos:pos + 4], struct.unpack("<I", d[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            ch = struct.unpack("<H", d[pos + 10:pos + 12])[0]
        elif cid == b"data":
            data = np.frombuffer(d[pos + 8:pos + 8 + size], dtype="<f4")
        pos += 8 + size + (size & 1)
    return data.reshape(-1, ch).astype(np.float64)


def parse_apo(path):
    """Independent, minimal reader for the sample file (PK / LSC / HSC only)."""
    sections = {"common": [0.0, []], "L": [0.0, []], "R": [0.0, []]}
    cur = "common"
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, val = line.partition(":")
        key, tok = key.strip().lower(), val.split()
        if key == "preamp":
            sections[cur][0] += float(tok[0])
        elif key == "channel":
            cur = {"l": "L", "r": "R"}.get(tok[0].lower(), "common")
        elif key.startswith("filter") and tok[0].upper() == "ON":
            kv = {tok[i].lower(): float(tok[i + 1]) for i in range(2, len(tok) - 1)
                  if tok[i].lower() in ("fc", "gain", "q")}
            sections[cur][1].append((tok[1].upper(), kv["fc"], kv["gain"], kv["q"]))
    return sections


def rbj(kind, f0, gain_db, q):
    A = 10 ** (gain_db / 40)
    w0 = 2 * np.pi * f0 / FS
    c, s = np.cos(w0), np.sin(w0)
    alpha = s / (2 * q)
    if kind == "PK":
        b = [1 + alpha * A, -2 * c, 1 - alpha * A]
        a = [1 + alpha / A, -2 * c, 1 - alpha / A]
    elif kind == "LSC":
        k = 2 * np.sqrt(A) * alpha
        b = [A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k)]
        a = [(A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k]
    elif kind == "HSC":
        k = 2 * np.sqrt(A) * alpha
        b = [A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k)]
        a = [(A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k]
    else:
        raise ValueError(kind)
    return np.array(b) / a[0], np.array(a) / a[0]


def apply_section(x, section):
    preamp, filters = section
    y = x * 10 ** (preamp / 20)
    for kind, f0, g, q in filters:
        b, a = rbj(kind, f0, g, q)
        y = signal.lfilter(b, a, y)
    return y


def reference(x, mute, swap, master_db):
    frames = x.shape[0]
    dry = np.zeros((frames, 2))
    lfe = np.zeros((frames, 2))
    sos = signal.butter(4, 125, btype="low", fs=FS, output="sos")
    for i, name in enumerate(NAMES):
        if name in mute:
            continue
        cl, cr = MATRIX[name]
        if name == "LFE":
            y = signal.sosfilt(sos, x[:, i])
            lfe[:, 0] += cl * y
            lfe[:, 1] += cr * y
        else:
            dry[:, 0] += cl * x[:, i]
            dry[:, 1] += cr * x[:, i]
    delayed = np.zeros_like(dry)
    delayed[DELAY:] = dry[:-DELAY]
    lr = (delayed + lfe) * 10 ** (master_db / 20)
    eq = parse_apo(EQ)
    L = apply_section(lr[:, 0], eq["common"])
    R = apply_section(lr[:, 1], eq["common"])
    if swap:
        L, R = R, L
    return np.stack([apply_section(L, eq["L"]), apply_section(R, eq["R"])], axis=1)


def run_case(tmp, x, mute, swap, master_db):
    src = os.path.join(tmp, "in.wav")
    dst = os.path.join(tmp, "out.wav")
    write_wav_float(src, x, MASK_714)
    cmd = [CLI, src, dst, "--eq", EQ, "--master", str(master_db), "--quiet"]
    if mute:
        cmd += ["--mute", ",".join(mute)]
    if swap:
        cmd.append("--swap")
    subprocess.run(cmd, check=True)
    got = read_wav_float(dst)
    want = reference(x.astype(np.float32).astype(np.float64), mute, swap, master_db)
    err = np.max(np.abs(got - want))
    scale = np.max(np.abs(want))
    return err, scale


def main():
    rng = np.random.default_rng(7)
    x = rng.uniform(-0.25, 0.25, size=(FS * 3, 12))
    cases = [
        ([], False, 0.0),
        (["TBL", "FC"], False, -3.0),
        (["LFE"], True, -6.0),
        ([], True, 0.0),
    ]
    worst = 0.0
    with tempfile.TemporaryDirectory() as tmp:
        for mute, swap, master in cases:
            err, scale = run_case(tmp, x, mute, swap, master)
            worst = max(worst, err)
            print(f"mute={mute or '-'} swap={swap} master={master:+.1f} dB: "
                  f"max |diff| = {err:.3e} (signal peak {scale:.3f}, {20*np.log10(err/scale):.1f} dB)")
    ok = worst < 1e-5
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
