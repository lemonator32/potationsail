# System architecture

```
┌──────────────────────── potationsail.exe (Win32 + GDI) ────────────────────────┐
│  main.cpp ── message loop, accelerators, DPI / pointer setup                    │
│  MainWindow ── owns Document, UndoStack, brush state, composite buffers         │
│    ├── ToolPanel   (left)   tool, colour, size, opacity, hardness, stabilizer   │
│    ├── CanvasView  (centre) zoom / pan, pen + mouse input ─► Stabilizer         │
│    └── LayerPanel  (right)  layer list, blend mode, opacity, protect / clip     │
└───────────────────────────────┬─────────────────────────────────────────────────┘
                                │  (no Win32 below this line)
┌──────────────────────── potcore.lib (C++17) ────────────────────────────────────┐
│  Stabilizer   SAI's packet filter: moving averages + Bézier + S-level tail      │
│  StrokeEngine round dabs, coverage mask, pen / eraser / protect opacity         │
│  Compositor   layer tree ─► premultiplied BGRA, using Blend kernels             │
│  Blend        SAI's 9 blend kernels in 15-bit fixed point                       │
│  Document     sai_document + 8-bit working rasters, structure editing           │
│  Undo         pixel, property, structure and move commands                      │
└───────────────────────────────┬─────────────────────────────────────────────────┘
┌──────────────────────── libsai (C99, git submodule) ────────────────────────────┐
│  sai_document_load / save, VFS encryption, tile RLE, thumbnail, chunks          │
└──────────────────────────────────────────────────────────────────────────────────┘
```

Build targets (`CMakeLists.txt`):

| Target | Contents |
|---|---|
| `sai` | libsai, from the `libsai/` submodule; its tools and tests are switched off |
| `potcore` | `src/core/*`: portable, no Windows headers |
| `potationsail` | `src/app/*`: the Win32 application (`POT_BUILD_APP`, on by default) |
| `potcore_tests` | `tests/test_core.cpp`, registered with CTest |

## Data model

### Document (`src/core/Document.*`)

* The loaded **`sai_document` stays alive for the whole session.** All
  edits go into it, and saving calls `sai_document_save` on it. Everything
  potationsail does not understand is therefore saved back unchanged:
  linework bodies, masks in `/sublayers`, unknown chunks, texture and effect
  settings, the work-time record.
* Each raster layer (`SAI_LAYER_RASTER`) also gets a **working raster**:
  canvas-sized, 8-bit **premultiplied BGRA** (`uint32_t`), keyed by layer
  id. This is the format SAI's display kernels use, so blending needs no
  conversion.
* Each raster has a `dirty` flag. On save, only dirty rasters are converted
  back with `sai_layer_set_rgba8`. Untouched layers keep their original
  16-bit tile data byte for byte.
* Layer order is libsai's flat array, bottom first. Folders are layers of
  type `SAI_LAYER_SET`, and children point to them through `pfid`
  (`parent_set_id`). Order matters only among siblings.
* Structure edits go through `addRasterLayer`, `addLayerSet`, `detach` and
  `attach`. A `DetachedLayer` owns the libsai record's heap blocks, its
  working raster, and any masks whose `plid` points to it. That makes
  delete/undo lossless.

### Pixel formats

| Where | Format |
|---|---|
| `.sai` file | 32×32 tiles, 16-bit BGRA premultiplied (14 significant bits), +8 px border |
| libsai API used here | straight RGBA8, canvas sized (`sai_layer_get/set_rgba8`) |
| potcore working rasters | premultiplied BGRA8 packed in `uint32_t` |
| Composite | premultiplied BGRA8 over a transparent backdrop |
| Display | composite over a checkerboard, opaque BGRA8 in a DIB section |
| Thumbnail | composite over white, downscaled with SAI's size rule |

The working rasters are 8-bit, so a layer that is **painted on** loses the
low 6 bits of SAI's 14-bit precision when saved. Layers that are not
touched keep full precision.

## Painting pipeline

```
WM_POINTER (pen)  ─┐                                 client px
WM_MOUSE* (mouse) ─┴─► CanvasView::inputSample ─► Stabilizer::feed ─► smoothed samples
                                                                          │ toCanvas()
                                                                          ▼
                     MainWindow::strokePoint ─► StrokeEngine::addPoint ─► dirty rect
                                                                          │
                     MainWindow::refresh(rect) ─► Compositor::composite(rect)
                                              ─► checkerboard ─► CanvasView::invalidateCanvas
```

* **Stroke engine.** At pen-down it snapshots the layer and allocates a
  16-bit coverage mask. Dabs are placed every `spacing × diameter` pixels
  and combined into the mask with **max**. The layer is recomputed from the
  snapshot wherever the mask changed. A stroke therefore never builds up
  past its opacity, the way a SAI pen does, and "protect opacity" and the
  eraser are simple per-pixel rules.
* **Incremental compositing.** Only the dirty rectangle is recomposited and
  copied to the backing DIB. `InvalidateRect` covers just that area at the
  current zoom.
* **End of stroke.** `StrokeEngine::end` returns the before/after pixels of
  the bounding box. These become a `PixelCommand` on the undo stack.

## Undo (`src/core/Undo.*`)

| Command | Stores |
|---|---|
| `PixelCommand` | layer id, rectangle, pixels before and after |
| `PropsCommand` | name, opacity, visibility, protect, clipping, blend before/after |
| `StructureCommand` | an insertion, or a removed `DetachedLayer` |
| `MoveCommand` | from/to index and parent |

The stack keeps a memory budget (512 MB by default). The oldest commands
are dropped first.

## UI layer (`src/app/*`)

* Plain Win32 and GDI, with Common Controls v6 through a manifest pragma.
  No third-party toolkit. Per-monitor-v2 DPI awareness. Layout sizes go
  through `scalePx`.
* `EnableMouseInPointer(FALSE)`: the mouse stays on `WM_MOUSE*`, and pens
  arrive as `WM_POINTER` with full history, pressure, and the eraser
  flag. The eraser end of the stylus switches to the eraser settings for
  that stroke only.
* Layer names are converted between UTF-16 and the **ANSI code page**,
  which is how SAI stores them (CP932 on Japanese Windows).
* Display: a top-down 32-bit DIB section mirrors the display buffer.
  `StretchBlt` draws it, using HALFTONE when zoomed out and COLORONCOLOR
  (nearest) when zoomed in, through an off-screen buffer to avoid flicker.

## Testing

`potcore_tests` covers the blend identities and the Luminosity cases, the
stabilizer (window sizes, jitter reduction per level, output shape, S tail),
the brush, a full save/load round trip through libsai, folders and clipping,
and undo for pixels, structure and moves. Pass `.sai` paths on the command
line to also load, composite, resave, reload and compare real files.
`potcore_tests --write-sample out.sai` writes the demo document.
