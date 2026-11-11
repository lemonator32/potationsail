# Blend modes

potationsail composites layers with a scalar transcription of PaintTool
SAI's own blend kernels, recovered from `sai.exe` 1.2.6‑Beta.3 (SHA‑256
`68ecc23e…a00090a`). The code is in [`src/core/Blend.cpp`](../src/core/Blend.cpp).

## How the kernels were found

| Step | Evidence (sai.exe VA) |
|---|---|
| The FourCC values (`pass`, `norm`, `mul `, `scrn`, `over`, `add `, `sub `, `adsb`, `cbin`) appear as a table at `0x53a974` and as immediates in the layer loader. | `0x53a974`, `0x463ddb` |
| The layer loader `FUN_00463c50` maps each FourCC to a mode byte stored at layer struct offset `+0x50`: pass 0, norm 1 (also unknown values), mul 2, scrn 3, over 4, **add 5**, sub 6, adsb 7, cbin 8. | `0x463c50` |
| `FUN_0049fd10` builds the per-layer render pipeline. It indexes four 16-entry function tables with that byte: `0x53c880`, `0x53c8c0`, `0x53c900`, `0x53c940`. | `0x49fdaa` |
| Table `0x53c880` (8-bit BGRA, the display path) holds the kernels `0x49d690` (pass/norm), `0x49d760` (mul), `0x49d850` (scrn), `0x49d950` (over), `0x49dab0` (add), `0x49dbd0` (sub), `0x49dcf0` (adsb), `0x49de30` (cbin). | `0x53c880` |
| Every kernel loads its constant `0x7FFF7FFF7FFF7FFF` (fixed-point 1.0) from `0x53c9e8`–`0x53ca58`. | |

The other three tables are variants of the same kernels for other pipeline
stages (for example 16-bit or masked sources). potationsail uses only the
display-path table, because the app's working format is the same 8-bit
premultiplied BGRA.

## Common frame

Every kernel is an MMX loop with this shape (`d` = destination byte,
`s` = source byte, `op` = layer opacity 0..256, lanes B, G, R, A):

```
skip the pixel when the source alpha byte is 0
S = paddsw(psrlw(s*op, 1), psrlw(s*op, 9))   // source in 15-bit, times opacity
D = paddsw(d << 7, d)                         // destination in 15-bit
... mode arithmetic ...
out = packuswb(R >> 7)                        // back to 8 bit, saturating
```

`mul15(a, b)` below means `pmulhw` followed by `psllw 1`, i.e.
`(a*b) >> 15` with the low bit cleared. 1.0 is `0x7FFF`. All values are
**premultiplied**. `Sa` and `Da` are the alpha lanes. `flag` is the kernel's
first argument: when it is set, the destination alpha is treated as opaque
(`Da | 0x7FFF`).

## Formulas

| Mode | FourCC | Colour channel (premultiplied) | Alpha |
|---|---|---|---|
| Normal / Pass Through | `norm` / `pass` | `D(1−Sa) + S` | `Da(1−Sa) + Sa` |
| Multiply | `mul ` | `D(1−Sa) + S(D + 1 − Da)` | `Da(1−Sa) + Sa` |
| Screen | `scrn` | `D(1−Sa) + Sa − (Sa−S)(1−D)` | `Da(1−Sa) + Sa` |
| Overlay | `over` | if `D > Da/2`: `D(1−Sa) + Sa·Da − 2(Sa−S)(Da−D)`, else `D(1−Sa) + 2SD`; then `+ S(1−a′)` | see code |
| **Luminosity** | **`add `** | **`min(D + S·Da, Da) + S(1−Da)`** | **`Da + Sa(1−Da)`** |
| Shade | `sub ` | `max(D − (Sa−S)·Da, 0) + S(1−Da)` | `Da + Sa(1−Da)` |
| Lumi & Shade | `adsb` | `clamp(D + Da(2S − Sa), 0, Da) + S(1−Da)` | `Da + Sa(1−Da)` |
| Binary Color | `cbin` | as Normal, but the layer opacity is ignored | as Normal |

### `add ` (Luminosity) in detail

Kernel `FUN_0049dab0`:

```
T  = mul15(D, 1 - Sa)              ; computed, then discarded (no use of T)
A  = Da | flag
SA = mul15(S, A)                   ; source scaled by backdrop coverage
W  = paddsw(D, SA)                 ; lanes B,G,R; alpha lane uses A
C  = W < A ? W : A                 ; pcmpgtw select: clamp to Da
C += mul15(1 - (C.alpha | flag), S); the part of S over transparent backdrop
```

Some consequences:

* **No darkening.** Unlike Normal, the backdrop is never multiplied by
  `(1−Sa)`, so black or dark colours on an `add ` layer leave the image
  unchanged. The kernel computes `D(1−Sa)` and then drops it; it looks like
  leftover code from the shared template.
* **Clamped to backdrop alpha.** The added light cannot push a colour past
  `Da`, so the result is always a valid premultiplied pixel. On an opaque
  backdrop this is a plain clamped add, `min(d + s·opacity, 255)`.
* **Over transparency it behaves like Normal.** Where `Da = 0` only the
  `S(1−Da)` term remains, so a Luminosity layer over empty canvas shows its
  own colours.
* **Premultiplied source.** A half-transparent red pixel adds half as much
  red. A soft brush therefore makes a soft glow, as in SAI.

Shade and Lumi & Shade share the same tail and differ only in the clamped
term. Lumi & Shade treats 50 % grey as neutral (`2S − Sa = 0`).

## Use in the compositor

[`Compositor.cpp`](../src/core/Compositor.cpp) walks the layer tree bottom
to top:

* **Raster layers** are blended onto the group's buffer with the kernel for
  their mode and `opacityTo256(opacity)`.
* **Folders** whose mode is not Pass Through are composited into a
  transparent temporary buffer, which is then blended with the folder's
  mode and opacity. Pass Through folders composite straight into the parent;
  below 100 % opacity the result is mixed with the unmodified parent.
* **Clipping groups**: the source is multiplied by the alpha of the nearest
  non-clipped layer below it in the same folder before blending. A hidden
  base hides its whole group.

## What is not verified

* The kernels were transcribed from Ghidra's decompiled MMX code. They have
  **not** been compared pixel-for-pixel against images rendered by SAI.
  The tests (`tests/test_core.cpp`) check the expected arithmetic
  identities: Normal replaces, Multiply is `d·s`, a clamped add for
  Luminosity, and grey is neutral for Lumi & Shade.
* How SAI handles a non-Pass-Through folder at reduced opacity, and clipping
  into a folder, was inferred from the pipeline builder rather than traced
  through every stage.
* Paper textures, layer effects (`peff`) and masks are preserved in files
  but not rendered.
