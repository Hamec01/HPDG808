# HPDG 808 upstream synchronization

Source: [Hamec01/HPDG](https://github.com/Hamec01/HPDG), revision
`bf6b37769fd26bc75271520f6460e1249dbd1cde` (HPDG 808 1.1.0).

All files in `Source/Bass808/`, except HPDG's lane adapter `Sub808Sound.*`, and
`Source/Audio/Synth808Voice.*` match that committed revision. This includes the preset bank,
OSC B, Draw Wave, reference resynthesis, preview transport, Sound Library and module presets.
The browser, oscillator and editor checks in `Tests/` come from the same revision.

Ahead of that revision (2026-11-25): the preset bank has the six `Memphis Trap - M808` presets instead of the eight
fitted Memphis ones (`BassRetiredPresets.inc`), the same as HPDG's working copy. `Tests/Bass808MemphisLab.cpp` checks
them (`--memphis-check`).

Ahead of that revision (2026-11-26, HPDG branch `claude/dnb-dancefloor-29b25a`): DnB Dancefloor - eleven
`Dancefloor - Sub / Mid / Stab` presets in place of the four `Modern - ` ones (retired), `BassSettings::lowCutHz` (the
"Low Cut" parameter), and Trio (MIDI channels 1 / 2 / 3: this sound, a Mid preset, a Stab preset; `--trio-check`).
All of `Source/Bass808/` again matches that HPDG branch.

This standalone repository keeps its own CMake project, installer and minimal drawing helpers.
HPDG's drum sequencer, lane adapter and experimental skins are outside this product's scope.
The standalone Dark controls use the existing `Source/UI/` helpers without adding HPDG's
unrelated image and font assets.

To repeat the validation:

```
cmake -S . -B build
cmake --build build --config Release --target HPDG808_VST3 HPDG808_Standalone HPDG_Bass808Snapshot --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

The installer's version is read from `HPDG808_VERSION` in `CMakeLists.txt`.
`dist/` holds the built installer and its SHA-256; `docs/hpdg808/` contains the matching guide,
release notes, screenshots and website handoff.

Ahead of that revision (2026-11-27, HPDG branch `claude/dnb-dancefloor-29b25a`): Bass Architecture 2.0 - the Advanced
engine (`BassAdvanced.*`, `BassAdvancedParams.*`, parameters version 7), the Advanced Dancefloor Mid / Stab presets,
`BassFactoryPresetsLater.inc` (sounds after the released programmes), the ADVANCED tab, the post-sum safety ceiling.
This repository is now where `Source/Bass808/` is developed; HPDG holds an identical copy.
