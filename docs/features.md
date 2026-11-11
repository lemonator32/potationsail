# Features

## Files

| Action | How |
|---|---|
| New canvas | File ▸ New (Ctrl+N), 1–10000 px each side, 72 dpi, one empty layer |
| Open `.sai` | File ▸ Open (Ctrl+O), drag and drop onto the window, or pass the path on the command line |
| Save / Save As | Ctrl+S / Ctrl+Shift+S. Writes a SAI 1.x document with libsai, including a thumbnail |

Supported: `.sai` from PaintTool SAI 1.1 and later (`/canvas` version
0x10). Legacy 1.0 canvases are rejected by libsai with "unsupported".

Things that are kept but not edited or rendered: linework layers, masks,
paper textures, layer effects, unknown chunks, and the work-time record.
They are written back unchanged.

## Painting

* **Pen** (N) and **Eraser** (E). Each tool remembers its own size,
  opacity and hardness. The eraser end of a stylus erases with the eraser
  settings.
* **Size** 1–500 px (logarithmic slider, `[` and `]` keys), **minimum size**
  at zero pressure, **opacity** (the stroke's ceiling, as in SAI),
  **hardness** (soft to hard edge), and **pressure → opacity**.
* Pressure from Windows Ink pens (`WM_POINTER`). The mouse paints at full
  pressure.
* **Protect opacity** on a layer: the pen only recolours existing pixels,
  and the eraser does nothing.
* Layer ▸ Clear (Del) empties the selected layer.

## Stabilizer

The combo box offers SAI's settings: **0**, **1–15**, **S‑1…S‑7**. See
[stabilizer.md](stabilizer.md).

* 0–1: raw input.
* 2–15: moving average over that many pen packets, then Bézier smoothing.
* S‑1…S‑7: a stronger two-stage average. On pen-up the line catches up to
  where the pen left with a tapered tail.

## Layers

* Raster layers and **folders**, shown top first and indented by depth.
* **Blend modes**, using SAI's arithmetic ([blend-modes.md](blend-modes.md)):
  Normal, Multiply, Screen, Overlay, **Luminosity (`add `)**, Shade,
  Lumi & Shade, Binary Color, and Pass Through for folders.
* Opacity (one undo step per slider drag), visibility (click the eye box),
  protect opacity, **clipping group**.
* New layer (Ctrl+Shift+N) goes above the selection, inside the selected
  folder. Also: new folder, rename (double-click), delete (folders must be
  empty first), move up/down within the folder.
* Non-raster layers are listed as "(not editable)". They cannot be painted
  on and are not rendered.

## View

* Mouse wheel zooms around the cursor (1/32× to 32×). Ctrl+= / Ctrl+-.
* Ctrl+0 fits the canvas to the window, Ctrl+1 shows actual pixels.
* Pan with the middle mouse button, or hold Space and drag.
* Checkerboard under transparent areas.

## Edit

* Undo / redo (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z) for strokes, clears, layer
  properties, adding, deleting and moving layers.

## Known limitations

* Painted layers are stored back at 8-bit precision (see
  [architecture.md](architecture.md#pixel-formats)).
* No selection tools, transforms, colour picker from canvas, masks, text, or
  brush textures.
* Layers cannot be dragged into or out of folders. Create them inside the
  selected folder instead.
* Windows only. The core library is portable; the UI is Win32.
