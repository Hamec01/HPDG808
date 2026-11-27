"""Memphis Trap's six M808, dirtier as on the Memphis records (owner 2026-11-27: "сделай как в треках"), within their
listened spec (Tests/Bass808MemphisLab.cpp; the owner kept it): H2-H5 under its ceiling, the decay (to within 0.5 dB of the sustain) within 12 % of the spec, the
energy between the harmonics under -60 dB.

The grit is the 808's own saturation (Saturation up to 1, then Drive up to 8: whole harmonics). A drive presses the
decay shorter, so for each preset the largest grit is kept whose measured decay still meets the spec (scanned in
steps of 0.05), then its level goes back to the original RMS through the gain, which sits after the voice (the decay
does not move with it). Measured on the engine (master neutral), as the spec is; the level on the whole plugin path.

usage: python -I tools/bass_tune_m808.py <exe> <original .inc> <out .inc> <work>
"""
import math
import os
import re
import subprocess
import sys

import numpy as np
import soundfile as sf

SPEC_DECAY_MS = {'Core': 360, 'Tight': 140, 'Dark Long': 720, 'Bounce': 280, 'Hard': 240, 'Vintage': 480}
# the spec's H2-H5 ceiling on A1 (Bass808MemphisLab specs[].high): the owner kept the spec (2026-11-27)
SPEC_H25_HIGH = {'Core': -15, 'Tight': -18, 'Dark Long': -22, 'Bounce': -12, 'Hard': -8, 'Vintage': -14}
F0 = 55.0
NEUTRAL_MASTER = ' masterSub=0 masterDynamics=0 masterLimit=0 masterLowGainDb=0 masterWidth=0 masterHarmonics=0'


def setkv(row, key, value):
    if re.search(rf'(^| ){key}=', row):
        return re.sub(rf'(^| ){key}=[-\d.e]+', lambda m: f'{m.group(1)}{key}={value}', row)
    return row + f' {key}={value}'


def getkv(row, key, default):
    m = re.search(rf'(^| ){key}=([-\d.e]+)', row)
    return float(m.group(2)) if m else default


def grit(row, g):
    s0, d0 = getkv(row, 'saturation', 0.8), getkv(row, 'drive', 4.5)
    sat = s0 + (1.0 - s0) * min(1.0, 2.0 * g)
    drive = d0 + max(0.0, 8.0 - d0) * max(0.0, 2.0 * g - 1.0)
    return setkv(setkv(row, 'saturation', round(sat, 4)), 'drive', round(drive, 4))


def amp(x, sr, t0, dur, f):
    a = int(t0 * sr)
    seg = x[a:a + int(dur * sr)]
    w = np.hanning(len(seg))
    return abs(np.sum(seg * w * np.exp(-2j * np.pi * f * np.arange(len(seg)) / sr))) * 2 / np.sum(w)


def measure(path):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    h1 = amp(x, sr, 0.8, 0.5, F0)
    h25 = 20 * math.log10(math.sqrt(sum(amp(x, sr, 0.8, 0.5, F0 * k) ** 2 for k in range(2, 6))) / h1)
    steady = amp(x, sr, 4.2, 0.6, F0)
    reached = 0.0
    for t in np.arange(0.0, 3.0, 0.01):
        if abs(20 * math.log10(amp(x, sr, t, 4 / F0, F0) / steady)) > 0.5:
            reached = t + 2 / F0
    pure = max(20 * math.log10(amp(x, sr, 0.8, 0.5, (k + 0.5) * F0) / h1 + 1e-12) for k in range(2, 40) if (k + 0.5) * F0 < 10000)
    m = x
    n = 1 << int(math.log2(len(m)))
    sp = np.abs(np.fft.rfft(m[:n] * np.hanning(n))) ** 2
    fr = np.fft.rfftfreq(n, 1 / sr)
    audible = 10 * math.log10(sp[(fr >= 100) & (fr < 1000)].sum() / sp[(fr >= 20) & (fr < 100)].sum())
    return {'h25': h25, 'decay': 1000 * reached, 'pure': pure, 'audible': audible,
            'rms': 10 * math.log10(np.mean(x[: int(3 * sr)] ** 2) + 1e-20), 'peak': 20 * math.log10(np.max(np.abs(x)) + 1e-12)}


def render(exe, work, tag, rows, engine_only=True):
    spec = os.path.join(work, tag + '.txt')
    with open(spec, 'w') as f:
        for k, r in rows.items():
            f.write(f'{k} | {r + (NEUTRAL_MASTER if engine_only else "")} | 33:0:5\n')
    folder = os.path.join(work, tag)
    subprocess.run([exe, '--adv-render', spec, folder, '60'], check=True, stdout=subprocess.DEVNULL)
    return {k: measure(os.path.join(folder, k + '.wav')) for k in rows}


def main():
    exe, inc, out_inc, work = sys.argv[1:5]
    os.makedirs(work, exist_ok=True)
    text = open(inc, encoding='utf-8').read()
    orig = {}
    for name in SPEC_DECAY_MS:
        m = re.search(r'\{ "Trap", "Memphis Trap - M808 ' + re.escape(name) + r'", "([^"]*)" \}', text)
        orig[name.replace(' ', '_')] = (name, m.group(1))
    base = render(exe, work, 'orig', {k: r for k, (n, r) in orig.items()})
    base_level = render(exe, work, 'orig_level', {k: r for k, (n, r) in orig.items()}, engine_only=False)
    best = {k: 0.0 for k in orig}
    steps = [round(0.05 * i, 2) for i in range(1, 21)]
    scan = render(exe, work, 'scan', {f'{k}__{g}': grit(r, g) for k, (n, r) in orig.items() for g in steps})
    for k, (n, r) in orig.items():
        for g in steps:
            m = scan[f'{k}__{g}']
            if abs(m['decay'] / SPEC_DECAY_MS[n] - 1.0) < 0.12 and m['pure'] <= -60.0 and m['h25'] <= SPEC_H25_HIGH[n] - 0.5:
                best[k] = g
    rows = {k: grit(r, best[k]) if best[k] > 0 else r for k, (n, r) in orig.items()}
    for step in range(3):
        res = render(exe, work, f'level{step}', rows, engine_only=False)
        for k in rows:
            delta = base_level[k]['rms'] - res[k]['rms']
            if res[k]['peak'] + delta > -1.0:
                delta = -1.0 - res[k]['peak']
            rows[k] = setkv(rows[k], 'gainDb', round(getkv(rows[k], 'gainDb', 0.0) + delta, 3))
    after = render(exe, work, 'after', rows)
    after_level = render(exe, work, 'after_level', rows, engine_only=False)
    for k, (n, r) in orig.items():
        b, a, bl, al = base[k], after[k], base_level[k], after_level[k]
        print(f'M808 {n:10s} grit {best[k]:.2f}  H2-H5 {b["h25"]:6.1f} -> {a["h25"]:6.1f}  audible {b["audible"]:6.1f} -> {a["audible"]:6.1f}'
              f'  decay {b["decay"]:5.0f} -> {a["decay"]:5.0f} (spec {SPEC_DECAY_MS[n]})  between {a["pure"]:6.1f}'
              f'  rms {bl["rms"]:6.1f} -> {al["rms"]:6.1f}  peak {al["peak"]:5.1f}')
    out = open(out_inc, encoding='utf-8').read()
    for k, (n, r) in orig.items():
        pat = re.compile(r'\{ "Trap", "Memphis Trap - M808 ' + re.escape(n) + r'", "[^"]*" \},')
        out = pat.sub('{ "Trap", "Memphis Trap - M808 %s", "%s" },' % (n, rows[k]), out, count=1)
    open(out_inc, 'w', encoding='utf-8', newline='').write(out)


if __name__ == '__main__':
    main()
