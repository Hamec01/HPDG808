"""Fit Advanced-engine patches to a reference profile (Bass Architecture 2.0, P4 offline fitting).

A family template names the parameters and their ranges; random search + local refinement renders candidates through
HPDG_Bass808Snapshot --adv-render (the whole plugin path) and keeps the ones nearest the references' bass profile
(tools/bass_profile.py). Seeds and ranges are written next to the result, so a fit is reproducible.

usage:
  python tools/bass_fit.py <family> <refs.json> <exe> <work folder> [--rounds 4] [--pop 24] [--seed 1]
families: see FAMILIES below (base row + ranges). Output: <work>/<family>_best.txt (preset row), <work>/<family>_log.tsv
"""
import json
import math
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bass_profile as bp  # noqa: E402

# a phrase like a Dancefloor drop line: G1 roots, a re-attack, Bb / F moves (beats at 174 BPM)
PHRASE = '31:0:1.75 31:1.75:1 34:3:1 31:4:2.5 29:6.5:0.75 26:7.25:0.75'

FAMILIES = {
    # full bass (sub + mids): the reference's whole bass stem
    'reese': {
        'base': 'advEngine=1 mode=1 advSubOn=1 advSubOct=0 advOscAOn=1 advOscAWave=2 advOscBOn=1 advOscBWave=2 '
                'advAmpA=2 advAmpS=1 advAmpR=60 advFltAType=1 masterLimit=1',
        'ranges': {
            'advSubLevel': (-12, 0), 'advSubWarm': (0, 0.6),
            'advOscAUnison': (2, 7, 'int'), 'advOscADetune': (0.05, 0.6), 'advOscASpread': (0.2, 1.0), 'advOscALevel': (-18, 0),
            'advOscBUnison': (1, 5, 'int'), 'advOscBOct': (-1, 1, 'int'), 'advOscBDetune': (0.0, 0.5), 'advOscBSpread': (0.0, 0.8), 'advOscBLevel': (-24, -3),
            'advCutA': (200, 4000, 'log'), 'advResA': (0, 0.6), 'advKeyA': (0, 1), 'advDrive': (0, 24),
            'advFxDrvModel': (0, 6, 'int'), 'advFxDrv': (0, 24), 'advMb': (0, 1), 'advMbTime': (5, 120, 'log'),
            'lowCutHz': (60, 200, 'log'), 'advWidth': (0.5, 2.0), 'advMidLevel': (-24, 0), 'gainDb': (-18, 0),
        },
    },
    'wobble': {
        'base': 'advEngine=1 mode=1 advSubOn=1 advSubOct=0 advOscAOn=1 advOscAWave=2 advOscBOn=1 advOscBWave=3 '
                'advAmpA=2 advAmpS=1 advAmpR=60 advFltAType=1 advMod1Src=1 advMod1Dst=14 advLfo1Shape=1 masterLimit=1',
        'ranges': {
            'advSubLevel': (-12, 0), 'advSubWarm': (0, 0.6),
            'advOscAUnison': (1, 5, 'int'), 'advOscADetune': (0.05, 0.5), 'advOscASpread': (0.2, 1.0), 'advOscALevel': (-12, 0),
            'advOscBLevel': (-24, -3), 'advOscBShape': (0, 1), 'advOscBOct': (-1, 1, 'int'),
            'advCutA': (150, 2000, 'log'), 'advResA': (0.1, 0.7), 'advDrive': (0, 24), 'advMod1Amt': (0.1, 0.8),
            'advLfo1Sync': (3, 5, 'int'),
            'advFxDrvModel': (0, 6, 'int'), 'advFxDrv': (0, 24), 'advMb': (0, 1), 'advMbTime': (5, 120, 'log'),
            'lowCutHz': (60, 200, 'log'), 'advWidth': (0.5, 2.0), 'advMidLevel': (-24, 0), 'gainDb': (-18, 0),
        },
    },
}


def sample(ranges, rng):
    out = {}
    for k, r in ranges.items():
        lo, hi = r[0], r[1]
        kind = r[2] if len(r) > 2 else 'lin'
        if kind == 'int':
            out[k] = rng.randint(int(lo), int(hi))
        elif kind == 'log':
            out[k] = round(math.exp(rng.uniform(math.log(lo), math.log(hi))), 2)
        else:
            out[k] = round(rng.uniform(lo, hi), 3)
    return out


def mutate(params, ranges, rng, amount):
    out = dict(params)
    for k, r in ranges.items():
        if rng.random() > 0.4:
            continue
        lo, hi = r[0], r[1]
        kind = r[2] if len(r) > 2 else 'lin'
        if kind == 'int':
            out[k] = max(int(lo), min(int(hi), out[k] + rng.choice([-1, 1])))
        elif kind == 'log':
            out[k] = round(max(lo, min(hi, out[k] * math.exp(rng.gauss(0, amount * math.log(hi / lo))))), 2)
        else:
            out[k] = round(max(lo, min(hi, out[k] + rng.gauss(0, amount * (hi - lo)))), 3)
    return out


def row(base, params):
    return base + ' ' + ' '.join(f'{k}={v}' for k, v in params.items())


def evaluate(exe, work, family, round_no, candidates, refs):
    spec = os.path.join(work, f'{family}_r{round_no}.txt')
    out = os.path.join(work, f'{family}_r{round_no}')
    with open(spec, 'w') as f:
        for i, c in enumerate(candidates):
            f.write(f'c{i:03d} | {row(FAMILIES[family]["base"], c)} | {PHRASE}\n')
    subprocess.run([exe, '--adv-render', spec, out], check=True, stdout=subprocess.DEVNULL)
    scores = []
    for i, c in enumerate(candidates):
        l, r, sr = bp.load(os.path.join(out, f'c{i:03d}.wav'))
        p = bp.profile(l, r, sr)
        d, parts = bp.distance(p, refs['mean'])
        # movement comes from the drums' sidechain in a track, not the patch: leave it out of the fit
        d -= 0.5 * parts['movement']
        scores.append((round(d, 2), parts, p))
    return scores


def main():
    a = sys.argv[1:]
    family, refs_path, exe, work = a[:4]
    rounds = int(a[a.index('--rounds') + 1]) if '--rounds' in a else 4
    pop = int(a[a.index('--pop') + 1]) if '--pop' in a else 24
    seed = int(a[a.index('--seed') + 1]) if '--seed' in a else 1
    rng = random.Random(seed)
    refs = json.load(open(refs_path))
    os.makedirs(work, exist_ok=True)
    ranges = FAMILIES[family]['ranges']
    best = []
    log = open(os.path.join(work, f'{family}_log.tsv'), 'w')
    log.write('round\tdist\tband\tside\tcentroid\tflatness\tparams\n')
    candidates = [sample(ranges, rng) for _ in range(pop)]
    for rnd in range(rounds):
        scores = evaluate(exe, work, family, rnd, candidates, refs)
        for c, (d, parts, p) in zip(candidates, scores):
            best.append((d, c, parts, p))
            log.write(f'{rnd}\t{d}\t{parts["band"]}\t{parts["side"]}\t{p["centroid"]}\t{p["flatness"]}\t{json.dumps(c)}\n')
        best.sort(key=lambda x: x[0])
        best = best[:8]
        print(f'round {rnd}: best {best[0][0]} ({best[0][2]}) centroid {best[0][3]["centroid"]} flat {best[0][3]["flatness"]} side {best[0][3]["side"]}', flush=True)
        amount = 0.25 * (0.6 ** rnd)
        candidates = [mutate(rng.choice(best[:4])[1], ranges, rng, amount) for _ in range(pop - 4)] + [sample(ranges, rng) for _ in range(4)]
    log.close()
    with open(os.path.join(work, f'{family}_best.txt'), 'w') as f:
        for d, c, parts, p in best[:3]:
            f.write(f'# dist {d} {json.dumps(parts)} seed {seed}\n{row(FAMILIES[family]["base"], c)}\n')
    print('best row:', row(FAMILIES[family]['base'], best[0][1]))


if __name__ == '__main__':
    main()
