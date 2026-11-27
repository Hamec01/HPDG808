"""Tune the existing Boom Bap / Trap bass presets in place (2026-11-27): only two controls move, the character stays.

1 audibility: the share of energy at 100 Hz - 1 kHz against under 100 Hz (what a phone / laptop plays) is raised to
  the substyle's target with Phantom (harmonics 2-5 of the fundamental, made for exactly that); a preset already over
  its target is left as it is (never made cleaner)
2 level: every preset of a substyle to the substyle's median RMS (+-0.5 dB), peaks kept under -1 dBFS

Targets (dB, 100-1k vs < 100 Hz): measured references (Memphis stems -4.5, the DnB drops -3.7) and the genre - grittier
Rage / Dark / Memphis, softer Cloud / Lofi. Renders: HPDG_Bass808Snapshot --adv-render (the plugin path, master
included) over a genre phrase.

usage: python -I tools/bass_tune_presets.py <exe> <BassFactoryPresets.inc> <work> [--write]
       prints before / after per preset; --write rewrites the .inc rows (the old rows go to BassRetiredPresets.inc by hand)
"""
import math
import os
import re
import subprocess
import sys

import numpy as np
import soundfile as sf

TARGETS = {
    ('Trap', 'ATL Classic'): -9.0, ('Trap', 'Dark Trap'): -7.0, ('Trap', 'Cloud Trap'): -12.0, ('Trap', 'Rage Trap'): -6.0,
    ('Trap', 'Luxury Trap'): -9.0,  # Memphis M808: the owner's spec (Bass808MemphisLab) - not tuned here
    ('Boom Bap', 'Classic'): -7.0, ('Boom Bap', 'Boom Bap Gold'): -7.0, ('Boom Bap', 'Dusty'): -9.0, ('Boom Bap', 'Jazzy'): -8.0,
    ('Boom Bap', 'Underground'): -8.0, ('Boom Bap', 'Lofi Rap'): -11.0,
}
# a trap line at 140 BPM (long 808s, a short one, a slide) and a boom bap line at 90 BPM (shorter, higher)
PHRASES = {
    'Trap': '31:0:1.5 31:1.75:0.5 34:2.5:1.5 29:4:2 36:6.25:0.5 31:6.75:1.25',
    'Boom Bap': '36:0:0.75 36:1:0.5 43:1.75:0.5 41:2.5:1 39:4:0.75 36:5:0.5 34:5.75:1 36:7:0.75',
}
BPM = {'Trap': 140, 'Boom Bap': 90}


def rows_of(inc):
    out = []
    for line in open(inc, encoding='utf-8').read().split('\n'):
        m = re.match(r'\{ "([^"]+)", "([^"]+)", "([^"]*)" \},', line.strip())
        if m:
            out.append((m.group(1), m.group(2), m.group(3), line))
    return out


def style_of(genre, name):
    for (g, s) in TARGETS:
        if g == genre and name.startswith(s + ' - '):
            return s
    return None


def setkv(row, key, value):
    if re.search(rf'(^| ){key}=', row):
        return re.sub(rf'(^| ){key}=[-\d.e]+', lambda m: f'{m.group(1)}{key}={value}', row)
    return row + f' {key}={value}'


def getkv(row, key, default):
    m = re.search(rf'(^| ){key}=([-\d.e]+)', row)
    return float(m.group(2)) if m else default


def measure(path):
    x, sr = sf.read(path, always_2d=True)
    m = x.mean(axis=1)
    n = 1 << int(math.log2(len(m)))
    spec = np.abs(np.fft.rfft(m[:n] * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    low, mid = spec[(f >= 20) & (f < 100)].sum(), spec[(f >= 100) & (f < 1000)].sum()
    return 10 * math.log10(mid / low + 1e-20), 10 * math.log10(np.mean(m ** 2) + 1e-20), 20 * math.log10(np.max(np.abs(x)) + 1e-12)


def render(exe, work, tag, items):
    """items: [(key, genre, row)] -> {key: (audible, rms, peak)}"""
    out = {}
    for genre in PHRASES:
        sel = [(k, r) for k, g, r in items if g == genre]
        if not sel:
            continue
        spec = os.path.join(work, f'{tag}_{genre.replace(" ", "")}.txt')
        with open(spec, 'w') as fh:
            for k, r in sel:
                fh.write(f'{k} | {r} | {PHRASES[genre]}\n')
        folder = os.path.join(work, f'{tag}_{genre.replace(" ", "")}')
        subprocess.run([exe, '--adv-render', spec, folder, str(BPM[genre])], check=True, stdout=subprocess.DEVNULL)
        for k, _ in sel:
            out[k] = measure(os.path.join(folder, k + '.wav'))
    return out


def main():
    exe, inc, work = sys.argv[1:4]
    write = '--write' in sys.argv
    os.makedirs(work, exist_ok=True)
    rows = rows_of(inc)
    todo = []
    for i, (genre, name, row, line) in enumerate(rows):
        style = style_of(genre, name)
        if style:
            todo.append({'i': i, 'key': f'p{i:03d}', 'genre': genre, 'style': style, 'name': name, 'row': row, 'orig': row})
    before = render(exe, work, 'before', [(t['key'], t['genre'], t['row']) for t in todo])
    for t in todo:
        t['before'] = before[t['key']]
    # 1 audibility: bisection on Phantom (0 .. 1) for the presets under their target
    low_side = [t for t in todo if t['before'][0] < TARGETS[(t['genre'], t['style'])]]
    for t in low_side:
        t['lo'], t['hi'] = getkv(t['row'], 'phantom', 0.0), 1.0
    for step in range(7):
        for t in low_side:
            t['try'] = round(0.5 * (t['lo'] + t['hi']), 4)
        res = render(exe, work, f'ph{step}', [(t['key'], t['genre'], setkv(t['row'], 'phantom', t['try'])) for t in low_side])
        for t in low_side:
            if res[t['key']][0] < TARGETS[(t['genre'], t['style'])]:
                t['lo'] = t['try']
            else:
                t['hi'] = t['try']
    for t in low_side:
        t['row'] = setkv(t['row'], 'phantom', round(t['hi'], 4))
    # 2 level: each substyle's presets to its median RMS, peaks under -1 dBFS
    mid = render(exe, work, 'mid', [(t['key'], t['genre'], t['row']) for t in todo])
    by_style = {}
    for t in todo:
        by_style.setdefault((t['genre'], t['style']), []).append(t)
    for key, ts in by_style.items():
        median = float(np.median([mid[t['key']][1] for t in ts]))
        for t in ts:
            audible, rms, peak = mid[t['key']]
            delta = median - rms
            delta = min(delta, -1.0 - peak) if peak + delta > -1.0 else delta
            t['row'] = setkv(t['row'], 'gainDb', round(getkv(t['row'], 'gainDb', 0.0) + delta, 3))
    after = render(exe, work, 'after', [(t['key'], t['genre'], t['row']) for t in todo])
    print(f'{"preset":40s} {"target":>6s}  audible before -> after   rms before -> after   peak after  phantom')
    for key, ts in by_style.items():
        for t in ts:
            b, a = t['before'], after[t['key']]
            print(f'{t["genre"] + " " + t["name"]:40.40s} {TARGETS[key]:6.1f}  {b[0]:6.1f} -> {a[0]:6.1f}       {b[1]:6.1f} -> {a[1]:6.1f}    {a[2]:6.1f}     '
                  f'{getkv(t["orig"], "phantom", 0):.2f} -> {getkv(t["row"], "phantom", 0):.2f}')
        rb = [t['before'][1] for t in ts]
        ra = [after[t['key']][1] for t in ts]
        print(f'   {key[0]} {key[1]}: level spread {max(rb) - min(rb):.1f} -> {max(ra) - min(ra):.1f} dB')
    if write:
        text = open(inc, encoding='utf-8').read()
        for t in todo:
            text = text.replace(f'"{t["name"]}", "{t["orig"]}"', f'"{t["name"]}", "{t["row"]}"')
        open(inc, 'w', encoding='utf-8', newline='').write(text)
        print('written', len(todo))


if __name__ == '__main__':
    main()
