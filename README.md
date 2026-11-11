# potationsail

A small drawing program to test [libsai](libsai/). It opens and saves
PaintTool SAI (`.sai`) documents, composites layers with SAI's own
blend-mode arithmetic (including `add ` / Luminosity), and smooths strokes
with a SAI-style pen stabilizer (0–15, S‑1…S‑7). Both were recovered by
reverse engineering `sai.exe` 1.2.6.

```bat
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
build\potationsail.exe
```

Documentation: [docs/](docs/README.md) covers
[features](docs/features.md), [architecture](docs/architecture.md),
[blend modes](docs/blend-modes.md) and the [stabilizer](docs/stabilizer.md).
