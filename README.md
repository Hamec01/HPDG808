# HPDG 808

The HPDG bass engine as its own instrument: an 808 synth, a sample layer, a saturator, a bass mastering
section and 200 factory presets. VST3 and Standalone, Windows 10/11 x64. By HamloProd.

Download: https://hamloprod.org/en/vst/hpdg808, or `dist/HPDG808_Setup_1.0.2.exe` in this repository
(SHA-256 in `dist/HPDG808_Setup_1.0.2.exe.sha256`).

## Build

Needs CMake 3.22+ and Visual Studio 2022 (C++).

```
git clone https://github.com/Hamec01/HPDG808.git
cmake -S . -B build
cmake --build build --config Release --target HPDG808_VST3 HPDG808_Standalone
```

Installer (`Releases/HPDG808_Setup_<version>.exe` and its SHA-256; Inno Setup and the VC++ Runtime are
fetched into `build/`):

```
powershell -ExecutionPolicy Bypass -File Installer\Build-HPDG808.ps1
```

The version is set in one place: `set(HPDG808_VERSION ...)` in `CMakeLists.txt`.

## Layout

- `Source/Bass808/` - processor, editor, engine, sample layer, saturator, MASTER, export, factory presets.
- `Source/Audio/Synth808Voice.*` - the 808 voice. `Source/UI/` - the knob and sketch drawing it uses.
- `Installer/` - build script and Inno Setup script. `docs/hpdg808/` - release notes, guide, site materials.

HPDG's Sub808 lane uses the same engine; its adapter (`Sub808Sound`) stays in the HPDG repository.

