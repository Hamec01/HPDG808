"""Tune the existing Boom Bap / Trap bass presets in place (2026-11-27, second pass): the character stays, the 808's own
saturation and the gain move.

1 audibility: the share of energy at 100 Hz - 1 kHz against under 100 Hz (what a phone / laptop plays) is raised to
  the substyle's target with the 808 saturation (a memoryless curve on the oscillator: whole harmonics only, like an
  808 pushed into a console). One knob g = 0 .. 1: Saturation (wet blend) up to 1 over the first half, then Drive up to
  8 over the second. A preset already over its target is left as it is (never made cleaner).
2 purity: the energy half way between the harmonics of a held A1 stays under -60 dB (the M808 spec's bar); g is backed
  off until it does. (The first pass used Phantom: its envelope resets off the period and left -24 .. -45 dB of
  non-harmonic buzz - reverted.)
3 level: every preset of a substyle to the substyle's median RMS (+-0.5 dB), peaks under -1 dBFS

Targets (dB, 100 Hz-1 kHz vs < 100 Hz): the Memphis reference stems (-4.5) for Memphis, otherwise the genre. Rage /
Cloud Trap: hidden from the menu (2026-11-27), not tuned (the Carti references measured cleaner than the genre guess).

usage: python -I tools/bass_tune_presets.py <exe> <original .inc> <out .inc> <work> [--only "Trap/Memphis Trap"]
"""
import math
import os
import re
import subprocess
import sys

import numpy as np
import soundfile as sf

TARGETS = {
    ('Trap', 'ATL Classic'): -9.0, ('Trap', 'Dark Trap'): -7.0, ('Trap', 'Luxury Trap'): -9.0, ('Trap', 'Memphis Trap'): -5.0,
    ('Boom Bap', 'Classic'): -7.0, ('Boom Bap', 'Boom Bap Gold'): -7.0, ('Boom Bap', 'Dusty'): -9.0, ('Boom Bap', 'Jazzy'): -8.0,
    ('Boom Bap', 'Underground'): -8.0, ('Boom Bap', 'Lofi Rap'): -11.0,
}
PHRASES = {
    'Trap': '31:0:1.5 31:1.75:0.5 34:2.5:1.5 29:4:2 36:6.25:0.5 31:6.75:1.25',
    'Boom Bap': '36:0:0.75 36:1:0.5 43:1.75:0.5 41:2.5:1 39:4:0.75 36:5:0.5 34:5.75:1 36:7:0.75',
}
BPM = {'Trap': 140, 'Boom Bap': 90}
HELD = '33:0:3'  # A1 held 3 beats: purity


def rows_of(inc):
    out = []
    for line in open(inc, encoding='utf-8').read().replace('\r\n', '\n').split('\n'):
        m = re.match(r'\{ "([^"]+)", "([^"]+)", "([^"]*)" \},', line.strip())
        if m:
            out.append((m.group(1), m.group(2), m.group(3)))
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


def grit(row, g):
    s0, d0 = getkv(row, 'saturation', 0.8), getkv(row, 'drive', 4.5)
    sat = s0 + (1.0 - s0) * min(1.0, 2.0 * g)
    drive = d0 + max(0.0, 8.0 - d0) * max(0.0, 2.0 * g - 1.0)
    return setkv(setkv(row, 'saturation', round(sat, 4)), 'drive', round(drive, 4))


def phrase_measure(path):
    x, sr = sf.read(path, always_2d=True)
    m = x.mean(axis=1)
    n = 1 << int(math.log2(len(m)))
    spec = np.abs(np.fft.rfft(m[:n] * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    low, mid = spec[(f >= 20) & (f < 100)].sum(), spec[(f >= 100) & (f < 1000)].sum()
    return 10 * math.log10(mid / low + 1e-20), 10 * math.log10(np.mean(m ** 2) + 1e-20), 20 * math.log10(np.max(np.abs(x)) + 1e-12)


def purity(path):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    f0 = 440 * 2 ** ((33 - 69) / 12)

    def amp(f):
        a = int(0.8 * sr)
        seg = x[a:a + int(0.5 * sr)]
        w = np.hanning(len(seg))
        return abs(np.sum(seg * w * np.exp(-2j * np.pi * f * np.arange(len(seg)) / sr))) * 2 / np.sum(w)
    h1 = amp(f0)
    return max(20 * math.log10(amp((k + 0.5) * f0) / h1 + 1e-12) for k in range(2, 40) if (k + 0.5) * f0 < 10000)


def render(exe, work, tag, items, held=False):
    """items: [(key, genre, row)] -> {key: measures}"""
    out = {}
    for genre in PHRASES:
        sel = [(k, r) for k, g, r in items if g == genre]
        if not sel:
            continue
        spec = os.path.join(work, f'{tag}_{genre.replace(" ", "")}.txt')
        with open(spec, 'w') as fh:
            for k, r in sel:
                fh.write(f'{k} | {r} | {HELD if held else PHRASES[genre]}\n')
        folder = os.path.join(work, f'{tag}_{genre.replace(" ", "")}')
        subprocess.run([exe, '--adv-render', spec, folder, str(BPM[genre])], check=True, stdout=subprocess.DEVNULL)
        for k, _ in sel:
            p = os.path.join(folder, k + '.wav')
            out[k] = purity(p) if held else phrase_measure(p)
    return out


def main():
    a = sys.argv[1:]
    exe, inc, out_inc, work = a[:4]
    only = a[a.index('--only') + 1] if '--only' in a else None
    os.makedirs(work, exist_ok=True)
    todo = []
    for i, (genre, name, row) in enumerate(rows_of(inc)):
        style = style_of(genre, name)
        if style and (only is None or only == f'{genre}/{style}'):
            todo.append({'key': f'p{i:03d}', 'genre': genre, 'style': style, 'name': name, 'row': row, 'orig': row})
    items = lambda ts, f=lambda t: t['row']: [(t['key'], t['genre'], f(t)) for t in ts]
    before = render(exe, work, 'before', items(todo))
    pure0 = render(exe, work, 'pure0', items(todo), held=True)
    for t in todo:
        t['before'], t['pure0'] = before[t['key']], pure0[t['key']]
    low = [t for t in todo if t['before'][0] < TARGETS[(t['genre'], t['style'])]]
    for t in low:
        t['lo'], t['hi'] = 0.0, 1.0
    for step in range(7):
        res = render(exe, work, f'g{step}', items(low, lambda t: grit(t['orig'], 0.5 * (t['lo'] + t['hi']))))
        for t in low:
            mid = 0.5 * (t['lo'] + t['hi'])
            if res[t['key']][0] < TARGETS[(t['genre'], t['style'])]:
                t['lo'] = mid
            else:
                t['hi'] = mid
    for t in low:
        t['g'] = t['hi']
    # purity: back the grit off while the held tone is not clean (never below the original's own purity bar)
    for attempt in range(6):
        pure = render(exe, work, f'p{attempt}', items(low, lambda t: grit(t['orig'], t['g'])), held=True)
        dirty = [t for t in low if pure[t['key']] > max(-60.0, t['pure0']) + 0.5 and t['g'] > 0.0]
        for t in low:
            t['pure'] = pure[t['key']]
        if not dirty:
            break
        for t in dirty:
            t['g'] = round(t['g'] * 0.7, 4) if t['g'] > 0.02 else 0.0
    # never worse: a preset the grit does not make more audible (by 0.5 dB) keeps its original sound
    check = render(exe, work, 'gain', items(low, lambda t: grit(t['orig'], t['g'])))
    for t in low:
        if check[t['key']][0] < t['before'][0] + 0.5:
            t['g'] = 0.0
        t['row'] = grit(t['orig'], t['g']) if t['g'] > 0.0 else t['orig']
    mid = render(exe, work, 'mid', items(todo))
    by_style = {}
    for t in todo:
        by_style.setdefault((t['genre'], t['style']), []).append(t)
    for key, ts in by_style.items():
        median = float(np.median([mid[t['key']][1] for t in ts]))
        for t in ts:
            _, rms, peak = mid[t['key']]
            delta = median - rms
            if peak + delta > -1.0:
                delta = -1.0 - peak
            t['row'] = setkv(t['row'], 'gainDb', round(getkv(t['row'], 'gainDb', 0.0) + delta, 3))
    after = render(exe, work, 'after', items(todo))
    pure1 = render(exe, work, 'pure1', items(todo), held=True)
    print(f'{"preset":42s} {"target":>6s}  audible before -> after  between-harmonics before -> after  peak  grit')
    for key, ts in by_style.items():
        for t in ts:
            b, af = t['before'], after[t['key']]
            print(f'{t["genre"] + " " + t["name"]:42.42s} {TARGETS[key]:6.1f}  {b[0]:6.1f} -> {af[0]:6.1f}       '
                  f'{t["pure0"]:6.1f} -> {pure1[t["key"]]:6.1f}            {af[2]:5.1f}  {t.get("g", 0):.2f}')
        ra = [after[t['key']][1] for t in ts]
        print(f'   {key[0]} {key[1]}: level spread {max(ra) - min(ra):.1f} dB')
    text = open(out_inc, encoding='utf-8').read()
    n = 0
    for t in todo:
        # the out .inc holds the first pass's row under the same name: replace that row
        m = re.search(r'\{ "' + re.escape(t['genre']) + r'", "' + re.escape(t['name']) + r'", "[^"]*" \},', text)
        if m:
            text = text[:m.start()] + '{ "%s", "%s", "%s" },' % (t['genre'], t['name'], t['row']) + text[m.end():]
            n += 1
    open(out_inc, 'w', encoding='utf-8', newline='').write(text)
    print('written', n)


if __name__ == '__main__':
    main()
