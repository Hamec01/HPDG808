# HPDG 808

The HPDG bass engine as its own instrument: an 808 synth, a sample layer, a saturator, a bass mastering
section and 210 factory presets. VST3 and Standalone, Windows 10/11 x64. By HamloProd.

Download: https://hamloprod.org/en/vst/hpdg808, or `dist/HPDG808_Setup_1.1.0.exe` in this repository
(SHA-256 in `dist/HPDG808_Setup_1.1.0.exe.sha256`).

## Version 1.1.0

- Sound Library with search, genre/category/character filters, favorites and user presets.
- 35 section presets for Synth, Sample, Mix, Filter / FX and Master, with SAVE AS and RESET.
- OSC B harmonic layer and 12 additional Trap / Drill sounds.
- Draw Wave, LOAD REF / AUTO DRAW, full-reference resynthesis and editable wave frames.
- Synth timeline with PLAY, scrubbing, DAW tempo and Snap.

The engine and editor are synchronized with HPDG's `bf6b377` revision. See
[the synchronization notes](docs/UPSTREAM_SYNC.md) for the source scope and validation.

## Build

Needs CMake 3.22+ and Visual Studio 2022 (C++).

On Windows, double-click `build.bat` to build both Release VST3 and Standalone. It prints the output
paths and keeps the window open. Use `build.bat debug` for Debug, or `build.bat nopause` in a terminal.
An existing build folder keeps its generator; a fresh folder is configured for x64.

```
git clone https://github.com/Hamec01/HPDG808.git
cd HPDG808
cmake -S . -B build
cmake --build build --config Release --target HPDG808_VST3 HPDG808_Standalone
```

Installer (`Releases/HPDG808_Setup_<version>.exe` and its SHA-256; Inno Setup and the VC++ Runtime are
fetched into `build/`):

```
powershell -ExecutionPolicy Bypass -File Installer\Build-HPDG808.ps1
```

The version is set in one place: `set(HPDG808_VERSION ...)` in `CMakeLists.txt`.

## Checks

```
cmake --build build --config Release --target HPDG_Bass808Snapshot --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

The checks cover the preset browser and module isolation, drawn waves and state compatibility,
timeline playback, automatic drawing and OSC B. Reports, audio and UI screenshots are written under
`build/checks/`. Use `-DBUILD_TESTING=OFF` at configure time for a plugin-only build.

## Layout

- `Source/Bass808/` - processor, editor, engine, sample layer, saturator, MASTER, export, factory presets.
- `Source/Audio/Synth808Voice.*` - the 808 voice. `Source/UI/` - the knob and sketch drawing it uses.
- `Installer/` - build script and Inno Setup script. `docs/hpdg808/` - release notes, guide, site materials.

HPDG's Sub808 lane uses the same engine; its adapter (`Sub808Sound`) stays in the HPDG repository.

