# potationsail documentation

| Document | Contents |
|---|---|
| [features.md](features.md) | What the app does, shortcuts, limitations |
| [architecture.md](architecture.md) | Modules, data model, pixel formats, painting and undo pipelines |
| [blend-modes.md](blend-modes.md) | SAI's blend kernels as recovered from `sai.exe`, especially `add ` (Luminosity) |
| [stabilizer.md](stabilizer.md) | SAI's pen stabilizer as recovered from `sai.exe`, with what is observed vs inferred |

## Building

Requirements: Windows 10 or later, Visual Studio 2022 or later with the C++
workload (CMake and Ninja come with it).

```bat
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build            :: or build\potcore_tests.exe [files.sai...]
build\potationsail.exe [file.sai]
```

Run these from a "Developer Command Prompt" (or after `vcvars64.bat`).
The core library and its tests also build without the UI:
`-DPOT_BUILD_APP=OFF`.

## Reverse-engineering source

Both analyses used PaintTool SAI Ver.1 **1.2.6-Beta.3** (`sai.exe`, PE32
x86, SHA-256 `68ecc23e03d8c1843b4b0460dae8597612c3054daa138d6e8a98a13e5a00090a`).
The binary was decompiled with REA (Ghidra 12.1.4 backend), and its tables
were read with a small PE scanner. Function names are Ghidra's defaults
(`FUN_<address>`), since no symbols ship. libsai's own notes for the file
format are in [`libsai/docs/`](../libsai/docs/).

PaintTool SAI is a product of SYSTEMAX. potationsail is not affiliated with
SYSTEMAX.
