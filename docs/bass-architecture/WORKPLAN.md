# Bass Architecture 2.0 — working plan (agent notes)

Source: owner's `C:\Users\Ham_h\Downloads\HPDG808_Development_Plan_2.0.html` (27.11.2026, outside git), stages P1–P5.
Goal behind it: DnB Dancefloor bass sounds good (owner: "унылый"; Mid B "16-bit pluck"; Trio unclear), and it lifts
HPDG + HPDG808 as products. Learning material: 20 Dancefloor tutorials (transcripts: scratchpad `dnb_videos/vNN/`).

## Hard rules (check before every commit)

- Bass only — no lead/pad/poly workstation. A module must help a bass, sound measurably good, cost sane CPU, be usable by HPDG.
- Classic path bit-identical: `HPDG808_ClassicGolden` (Tests/golden/classic_v1.tsv) passes. New features **off by default**.
- APVTS ids, ranges, meaning: never change. New params = new `ParameterID` version (**7** for 2.0).
- Old presets / retired / user: never silently re-voiced. Changed sound → new revision; old kept in `BassRetiredPresets.inc`.
- Keep Draw Wave, Sample osc, HamloMag saturator (`BassSaturator`), BassMaster, Pitch Punch, Glide/Legato, BassFitLab.
- Audio thread: no allocation, no file I/O, no locks. Smooth params, DC block, denormals, safe note on/off, preset change.
- Clean mono sub survives any stereo / mid processing (sub bypasses mid FX by default).
- Oversampling only local and measured; report latency.
- Subjective sound claims only after level-matched listening (owner decides). Numbers, not impressions (docs/RULES.md in HPDG).
- One stage at a time, closed by tests. Don't present old tests as new.
- HPDG: RG, Mutate, Track Lock, MIDI/WAV export, Sample Analysis, Preview keep working; other genres note-identical.

## Where code lives / sync

- Develop `Source/Bass808/*` in **HPDG808** (branch `bass-v2`), copy to HPDG (`Source/Bass808/`, + HPDG-only
  `Sub808Sound.*`). Both must hold identical copies → manifest check (sha256 list) — TODO `tools/bass808_sync.py` in HPDG.
- Build HPDG808: `cmake --build build/dancefloor --config Release --target HPDG_Bass808Snapshot --parallel 8`,
  tests: `ctest --test-dir build/dancefloor -C Release`.
- HPDG builds with `HPDG_BASS808_EMBEDDED=1`; Trio button only when not embedded.

## Current engine (facts from code, 14228f2 / 36aed69)

- `BassSettings` flat struct; `BassEngine` mono: 2 `BassVoice` (3 ms choke), held stack, one sine LFO → cutoff/vowel,
  then bus: `BassSaturator` → `processLowCut` → `processBusFilter` → gain glide. `render(float* mono, n)`.
- Voice: classic osc (Sine+FM / Tri / Saw / Square / Reese=2 saws), sub sine, OSC B = additive harmonics 2–8 (no own pitch),
  click, sample layer (+handoff), amp env (attack/hold/decay -6dB/sustain/release), TPT SVF LP12/LP24/HP12/BP/Vowel,
  phantom harmonics. Nonlinearity (808 drive/tanh) already **before** voice filter.
- Plugin: `Bass808Processor` — param tables `floatParams()/choiceParams()/boolParams()` with `float BassSettings::*`
  pointers (→ new fields must be **flat members** of BassSettings); `currentSettings()` builds settings per block;
  `BassMaster` mono→stereo (sub filter, dynamics, band width, harmonics, limiter); Trio = 2 extra engine+master on ch 2/3,
  summed **without** post-sum limiter.
- IO: `BassSettingsIO.cpp` tables `kFloats/kInts/kBools` (id → member). Write new fields only when ≠ default
  (like `lowCutHz`) so old trees still match factory presets.

## Design decisions for 2.0 (mine, to keep code consistent)

- `int engine = 0` in BassSettings: 0 Classic, 1 Advanced. Advanced = new code path inside `BassVoice`/`BassEngine`
  (reuse note lifecycle, glide, choke fade, held stack); Classic code untouched.
- Stereo: `BassEngine::render(float* mono, int n, float* side = nullptr)`. Advanced writes mono = sub + (L+R)/2 and adds
  side = (L−R)/2. Classic never touches side. Processor: master on mono, then L += side, R −= side. HPDG lanes: mono (later stereo).
- Advanced voice renders 3 buses: `sub` (mono, clean), `midL`, `midR`. Engine runs mid FX rack on midL/R, then sums.
- New file pair `BassAdvanced.h/.cpp` for oscillators / unison / LFO / matrix / FX DSP helpers (keep BassEngine.cpp readable).
- Field prefix: `adv...` (e.g. `advOscAWave`, `advOscAUnison`), params version 7, ids `aOscAWave`… (short, stable).

## Stages / checklist

### P1 Foundation (now)
- [x] 01 source freeze: HPDG 36aed69, HPDG808 14228f2 → branch `bass-v2`; shared files = all `Source/Bass808/*` except Sub808Sound.*
- [x] 04 preset parity: HPDG's 8 new Mid/Stab rows synced into 808; round-3 rows kept as retired (both repos)
- [ ] 02 build baseline: Release build + ctest 808, CoreTests HPDG — record results
- [x] 03 golden fixtures (667 renders, check 12 s): `--golden-write` → Tests/golden/classic_v1.tsv (engine notes 36/48 ×all presets, plugin phrase ×all programs)
- [ ] 05 research inventory: BassFitLab (HPDG), Bass808Lab, OscBLab, Draw Wave resynth, `--bass-cpu`, `--dnb-bass-envelope` — reusable vs missing
- [ ] 06 lab MVP: offline analysis of 4 families (808, Reese, Deep Sub, Boom Bap): pitch, env, band energy, centroid, M/S, CSV/JSON + WAV
- [ ] 07 RT audit: vector resize in processBlock (exists for hosts sending more), Trio note-off on switch, sample/DrawWave ownership
- [ ] 08 core contract doc (this file's design section → P1_CORE_CONTRACT.md if needed)
- [ ] 09 parity standalone vs embedded dry DSP (same settings → same mono render; master/ducking documented)
- [ ] 10 P1 report

### P2 Oscillators (Advanced engine)
- [x] generic osc ×3 (A/B/C): wave Sine/Tri/Saw/Square/Pulse(+Draw later); octave, semi, fine; level, pan; phase offset,
      random phase, retrigger/free; unison 1–7 (detune, spread, level compensation); shape/PW; on/off; modulation-only
- [x] PolyBLEP saw/pulse (exists: reuse `polyBlep`), triangle via integrated / BLAMP-lite
- [x] Sub: sine/tri, octave −2..0, level, mono, direct out (no mid FX), clean/warm
- [x] Noise (attack/texture): level (colour: via filters)
- [x] Reese = template (2+ detuned saws) not a new wave
- [ ] WT position over Draw Wave frames (later; reuse BassDrawWave band-limited tables)
- [ ] gate: pitch, alias, L/R, mono loss renders; C off ≈ 0 CPU; demo patches (Clean Sub, Warm Sub, Soft/Wide Reese, Analog Round, Boom Bap, Hybrid 808)

### P3 Modulation & FX
- [x] envelopes: AMP ADSR, FILTER ENV, MOD ENV (keep 808 pitch punch)
- [x] 4 LFO: sine/tri/saw/square/S&H/random; free / tempo sync; retrigger; one-shot
- [x] mod matrix (8 slots, 16-sample control rate): src (LFO1-4, ENV2, ENV3, velocity, note) → dst (osc pitch/level/shape/PM, cutoff, reso,
      drive, unison detune, width, noise…), depth bipolar; pitch in st, cutoff in oct
- [x] PM: C→A/B, B→A (acyclic), ratio
- [x] filter A (+ optional B series/parallel): existing SVF types, drive pre, key, env
- [x] FX rack on mid (fixed order: saturator → low cut → multiband → chorus; width in the voice): pre drive → filter → post drive (HamloMag) → low cut → multiband (own 3-band up/down, OTT-like)
      → chorus → width. Bypass/reorder click-free.
- [ ] gate: Donk, Growl, Wobble, Acid, Reese sound like targets (A/B); no clicks; no-alloc + CPU suite

### P4 Reference fitting & presets
- [ ] 3 ways per reference: sample / Draw-WT resynth / DSP fit; loss = pitch+env+spectral+harmonic+stereo+λ·cost
- [ ] preset passport (stableId, revision, engineVersion, role, articulation, gate range, fundamental range, subPolicy, metrics)
- [ ] first collection: 808, Dancefloor, Liquid/Deep, Boom Bap — distinct sounds, level-matched A/B with owner

### P5 Multi-Bass & integration
- [ ] Trio → Single / Layer / Multi (+ Legacy Trio mapping); slots Bass 1–3 fully editable; note-off by owner ledger
- [x] gain staging + post-sum safety ceiling (Advanced + LIMIT, or Trio) (3 in-phase layers = +9.54 dB peak)
- [ ] HPDG: Dancefloor Sub = foundation, Mid A = character, Mid B = answer (not mandatory); stems HPDG vs 808 consistent
- [ ] FL Studio check: VST3, MIDI drag ch1–3, automation, reopen, offline render

## Measured (tools/bass_profile.py; quality-lab/bass-v2/dancefloor_reference_profile.json)

- 8 reference drops (16 drops, SCNet bass stems): 94 % of the bass energy under 100 Hz, peak band 40-50 Hz; mids
  25-35 dB under the sub; side - mid: < 120 Hz -41 dB (mono), 120-500 -19, 500-2k -8, > 2k -6 dB (wide tops);
  flatness 300 Hz-3 kHz 0.45 (gritty); the 150-1500 Hz level moves 7.3 dB (quarter / 8th: sidechain, gates).
  References to their own mean: 8.7-16.4 (median 12.4).
- HPDG round 4-5 three-bass sum (6 seeds): distance 36-48; mono (side -60 dB everywhere), flatness 0.003-0.13,
  sub share 0.35-0.82: "уныло" in numbers.
- Advanced fits (tools/bass_fit.py, sub at the note's octave): reese 6.4, wobble 5.3 (more average than any
  reference: character from the recipes, the fit only puts it in the right region).

## Dancefloor-specific (from the videos)

- Sidechain bus: kick AND snare duck the basses (Kickstart 2 on kick + snare groups) -> HPDG mid lanes: snare duck.
- Sub: a sine + a sine with the 3rd harmonic drawn (~71 %), EQ above; sub at ~-3 dB; the sub sits under each mid bass.
- Mid basses: Serum presets + OTT, saturation, Pro-MB, EQ cutting the lows, G-Clip; "basic saw, -2 oct, 4 unison,
  detune 0.2, blend 75, a filter, FX"; pitch-slide overlaps ("vroom").
- Kanine: chug bass, brass stabs (Mid B = brass, not a pluck), Reese, a dotted-quarter bass, frequency shifter.
- Mix: kick loudest (~0 dB), snare a little under, hats under that; the bass bus -2.8 dB under the drums; the main bass
  ~0 dB into the bus, the support bass ~-6 dB; at most 4 bass elements.
- Movement built into the patch (evolving while the note holds), 8th rhythm, call and response phrases.
