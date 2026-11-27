"""Bass sound profile (Bass Architecture 2.0, P1 lab / P4 fitting): what a bass's drop sounds like, in numbers.

Third-party audio stays outside git: only profiles (numbers) are written into the repository.

usage:
  python tools/bass_profile.py refs <reference_drops.json> <stems folder> <out.json>
      every reference track's drops from its SCNet bass stem "<n>_<n>_(Bass).wav" (HPDG's
      quality-lab/dancefloor-2026-11/reference_drops.json gives the drop bars), one profile per drop + the mean
  python tools/bass_profile.py profile <wav> [start_s end_s]
  python tools/bass_profile.py compare <refs.json> <render folder | wav> [--json out.json]
      each render's distance to the references' mean profile (and to the nearest reference)

profile (frames where the bass sounds: mid level within 30 dB of its loud frames):
  bands      mid level per third-octave band 25 Hz .. 12.5 kHz, dB re the total mid energy
  side       side - mid level, dB, in four regions: < 120 Hz, 120-500, 500-2k, > 2k
  centroid   spectral centroid of the mid above 120 Hz, Hz
  flatness   spectral flatness of the mid 300 Hz .. 3 kHz (0 tonal .. 1 noise): grit / distortion
  movement   the 150-1500 Hz band's level over time: depth (dB std), its strongest rate 0.5-16 Hz and how much
             of the fluctuation sits at that rate
  sub_share  energy under 100 Hz / all
distance = mean |band difference| over 30 Hz .. 8 kHz (dB) + 0.5 x mean |side difference| + 4 x |log2 centroid ratio|
           + 10 x |flatness difference| + 0.5 x |movement depth difference|
"""
import json
import math
import os
import sys

import numpy as np
import soundfile as sf

BANDS = [25 * 2 ** (i / 3) for i in range(0, 28)]  # 25 Hz .. ~12.7 kHz
REGIONS = [(20, 120), (120, 500), (500, 2000), (2000, 16000)]


def load(path, start=None, end=None):
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    if start is not None:
        x = x[int(start * sr):int(end * sr)]
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    return x[:, 0], x[:, 1], sr


def profile(left, right, sr):
    mid, side = 0.5 * (left + right), 0.5 * (left - right)
    n, hop = 8192, 2048
    win = np.hanning(n)
    frames = max(1, (len(mid) - n) // hop)
    spec_m, spec_s, frame_db = [], [], []
    for f in range(frames):
        a = f * hop
        m = np.fft.rfft(mid[a:a + n] * win)
        s = np.fft.rfft(side[a:a + n] * win)
        pm, ps = np.abs(m) ** 2, np.abs(s) ** 2
        spec_m.append(pm)
        spec_s.append(ps)
        frame_db.append(10 * math.log10(pm.sum() + 1e-20))
    frame_db = np.array(frame_db)
    keep = frame_db > np.percentile(frame_db, 90) - 30
    pm = np.mean(np.array(spec_m)[keep], axis=0)
    ps = np.mean(np.array(spec_s)[keep], axis=0)
    freqs = np.fft.rfftfreq(n, 1 / sr)
    total = pm.sum() + 1e-20
    bands = []
    for lo in BANDS:
        hi = lo * 2 ** (1 / 3)
        sel = (freqs >= lo) & (freqs < hi)
        bands.append(10 * math.log10((pm[sel].sum() + 1e-20) / total))
    side = []
    for lo, hi in REGIONS:
        sel = (freqs >= lo) & (freqs < hi)
        side.append(max(-60.0, 10 * math.log10((ps[sel].sum() + 1e-20) / (pm[sel].sum() + 1e-20))))  # mono: -60
    sel = freqs >= 120
    centroid = float((freqs[sel] * pm[sel]).sum() / (pm[sel].sum() + 1e-20))
    sel = (freqs >= 300) & (freqs < 3000)
    flatness = float(np.exp(np.mean(np.log(pm[sel] + 1e-20))) / (np.mean(pm[sel]) + 1e-20))
    sub_share = float(pm[freqs < 100].sum() / total)

    # movement: the 150-1500 Hz level every 256 samples, while the bass sounds
    hop2 = 256
    spec = []
    n2 = 1024
    win2 = np.hanning(n2)
    f2 = np.fft.rfftfreq(n2, 1 / sr)
    band = (f2 >= 150) & (f2 < 1500)
    for a in range(0, len(mid) - n2, hop2):
        spec.append((np.abs(np.fft.rfft(mid[a:a + n2] * win2))[band] ** 2).sum())
    env = 10 * np.log10(np.array(spec) + 1e-20)
    loud = env > np.percentile(env, 90) - 30
    env_k = env[loud] if loud.sum() > 16 else env
    depth = float(np.std(env_k))
    e = env - np.mean(env)
    e[~loud] = 0.0
    fr = np.fft.rfftfreq(len(e), hop2 / sr)
    mag = np.abs(np.fft.rfft(e * np.hanning(len(e))))
    sel = (fr >= 0.5) & (fr <= 16)
    rate, focus = 0.0, 0.0
    if sel.any():
        k = np.argmax(mag[sel])
        rate = float(fr[sel][k])
        focus = float(mag[sel][k] ** 2 / ((mag[sel] ** 2).sum() + 1e-20))
    return {
        'bands': [round(b, 2) for b in bands],
        'side': [round(s, 2) for s in side],
        'centroid': round(centroid, 1),
        'flatness': round(flatness, 4),
        'movement': {'depth_db': round(depth, 2), 'rate_hz': round(rate, 2), 'focus': round(focus, 3)},
        'sub_share': round(sub_share, 3),
    }


def mean_profile(profiles):
    keys = ['centroid', 'flatness', 'sub_share']
    out = {
        'bands': [round(float(np.mean([p['bands'][i] for p in profiles])), 2) for i in range(len(BANDS))],
        'side': [round(float(np.mean([p['side'][i] for p in profiles])), 2) for i in range(len(REGIONS))],
    }
    for k in keys:
        out[k] = round(float(np.mean([p[k] for p in profiles])), 4)
    out['centroid'] = round(float(2 ** np.mean([math.log2(p['centroid']) for p in profiles])), 1)
    out['movement'] = {k: round(float(np.mean([p['movement'][k] for p in profiles])), 3) for k in ['depth_db', 'rate_hz', 'focus']}
    out['bands_sd'] = [round(float(np.std([p['bands'][i] for p in profiles])), 2) for i in range(len(BANDS))]
    return out


def distance(a, b):
    sel = [i for i, f in enumerate(BANDS) if 30 <= f <= 8000]
    band = float(np.mean([abs(a['bands'][i] - b['bands'][i]) for i in sel]))
    side = float(np.mean([abs(a['side'][i] - b['side'][i]) for i in range(len(REGIONS))]))
    cent = abs(math.log2(max(a['centroid'], 1) / max(b['centroid'], 1)))
    flat = abs(a['flatness'] - b['flatness'])
    move = abs(a['movement']['depth_db'] - b['movement']['depth_db'])
    return round(band + 0.5 * side + 4 * cent + 10 * flat + 0.5 * move, 2), {
        'band': round(band, 2), 'side': round(side, 2), 'centroid': round(cent, 2), 'flatness': round(flat, 3), 'movement': round(move, 2)}


def cmd_refs(drops_json, stems, out):
    drops = json.load(open(drops_json))
    result = {'tracks': [], 'note': 'SCNet bass stems of the maintainer\'s 8 reference records, drops only; numbers only'}
    allp = []
    for t in drops:
        n = t['track']
        path = os.path.join(stems, f'{n}_{n}_(Bass).wav')
        if not os.path.exists(path):
            continue
        bar = 4 * 60.0 / t['bpm']
        for d in t['drops']:
            a, b = d['bars']
            l, r, sr = load(path, a * bar, b * bar)
            p = profile(l, r, sr)
            p['track'], p['bars'] = n, [a, b]
            result['tracks'].append(p)
            allp.append(p)
            print(n, a, b, 'centroid', p['centroid'], 'flat', p['flatness'], 'side', p['side'], 'move', p['movement'], 'sub', p['sub_share'])
    result['mean'] = mean_profile(allp)
    json.dump(result, open(out, 'w'), indent=1)
    print('mean', json.dumps(result['mean']))


def cmd_compare(refs_json, target, out=None):
    refs = json.load(open(refs_json))
    files = [target] if target.lower().endswith('.wav') else sorted(
        os.path.join(target, f) for f in os.listdir(target) if f.lower().endswith('.wav'))
    rows = []
    mean = refs['mean']
    for f in files:
        l, r, sr = load(f)
        p = profile(l, r, sr)
        d, parts = distance(p, mean)
        near = min(distance(p, q)[0] for q in refs['tracks'])
        rows.append({'file': os.path.basename(f), 'distance': d, 'nearest': near, 'parts': parts, 'profile': p})
        print(f'{os.path.basename(f):28s} dist {d:6.2f} nearest {near:6.2f}  band {parts["band"]:5.2f} side {parts["side"]:5.2f} '
              f'cent {p["centroid"]:7.1f} flat {p["flatness"]:.3f} move {p["movement"]["depth_db"]:.2f}@{p["movement"]["rate_hz"]:.1f} '
              f'sub {p["sub_share"]:.2f} side {p["side"]}')
    print(f'{"references":28s}                                   cent {mean["centroid"]:7.1f} flat {mean["flatness"]:.3f} '
          f'move {mean["movement"]["depth_db"]:.2f}@{mean["movement"]["rate_hz"]:.1f} sub {mean["sub_share"]:.2f} side {mean["side"]}')
    if out:
        json.dump(rows, open(out, 'w'), indent=1)


if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == 'refs' and len(a) == 4:
        cmd_refs(a[1], a[2], a[3])
    elif a and a[0] == 'profile' and len(a) in (2, 4):
        l, r, sr = load(a[1], *(map(float, a[2:4]) if len(a) == 4 else (None, None)))
        print(json.dumps(profile(l, r, sr), indent=1))
    elif a and a[0] == 'compare' and len(a) >= 3:
        cmd_compare(a[1], a[2], a[a.index('--json') + 1] if '--json' in a else None)
    else:
        print(__doc__)
