# HPDG 808 upstream synchronization

Source: [Hamec01/HPDG](https://github.com/Hamec01/HPDG), revision
`bf6b37769fd26bc75271520f6460e1249dbd1cde` (HPDG 808 1.1.0).

All files in `Source/Bass808/`, except HPDG's lane adapter `Sub808Sound.*`, and
`Source/Audio/Synth808Voice.*` match that committed revision. This includes the preset bank,
OSC B, Draw Wave, reference resynthesis, preview transport, Sound Library and module presets.
The browser, oscillator and editor checks in `Tests/` come from the same revision.

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
