# Pen stabilizer

The stabilizer is modelled on PaintTool SAI 1.2.6's tablet input path. The
code is in [`src/core/Stabilizer.cpp`](../src/core/Stabilizer.cpp).

## Where it lives in SAI

| Function (sai.exe VA) | Role |
|---|---|
| `FUN_0041f5a0` | Builds the quick bar. Its stabilizer combo lists **0, 1–15, S‑1…S‑7** with item data `0..0x10` and `0x81..0x87`. Bit 7 marks the "S" family. |
| `FUN_0041b230` | Combo handler: stores the selected code at `panel+0x410`. |
| `FUN_004b4ae0` (`WM_MOUSEMOVE`) → `FUN_00436900` | Copies the code into globals `0x5414d0` / `0x5414ec`. |
| `FUN_004376a0` | On `WM_MOUSEMOVE` over a canvas, copies those globals into the shared input-state block (`DAT_00894bc4 + 9/+10`). |
| `FUN_00438c60` | The WinTab `WT_PACKET` handler (`"wintab.c"`). **This is the stabilizer.** It runs on raw packets, in screen coordinates, before any tool sees them. |
| `FUN_00437d30` | 16-entry ring-buffer moving average. |
| `FUN_00438010`, `FUN_00437e60`, `FUN_00437dc0` | Cubic Bézier subdivision of the filtered points. |
| `FUN_00437cf0` | Wraps an angle difference into (−π, π]. |

Because SAI smooths in screen space, the same level feels the same at any
zoom. potationsail does the same: the canvas view feeds client-area
coordinates to the stabilizer and converts the output to canvas coordinates.

## Algorithm

### 1. Window size

From `FUN_00438c60`:

```
n = (code & 0x7F) + (code & 0x80 ? 1 : 0)      // S-k averages over k+1 packets
n = clamp(n, 1, 16)
```

| Setting | Window `n` | Stages |
|---|---|---|
| 0, 1 | 1 | none (1 skips position averaging) |
| 2–15 | 2–15 | moving average + Bézier |
| S‑1…S‑7 | 2–8 | moving average + recursive average + Bézier + pen-up tail |

Changing the level, or a new tablet context, clears every history.

### 2. Moving average (`FUN_00437d30`)

Each smoothed quantity has a 16-slot ring buffer. A new value is averaged
with up to `n−1` previous entries. Mode 0 stores the **raw** input (a
finite moving average). Mode 1 stores the **averaged** output (a recursive,
exponential-like filter):

```cpp
double Average::apply(double v, int n, bool recursive) {
    sum = v; used = 1;
    for (i = 0; used < n && i < count; ++i, ++used) sum += hist[(head-1-i) & 15];
    avg = sum / used;
    hist[head] = recursive ? avg : v;
    head = (head + 1) & 15; count = min(count + 1, 16);
    return avg;
}
```

The **numeric levels** pass x and y through one mode-0 filter. The
**S levels** chain a mode-0 filter and a mode-1 filter. That is why S‑k is
smoother than level k+1 with the same window (measured: S‑7 jitter
0.81 px RMS, level 8 0.90 px, on ±4 px noise).

SAI tracks the pen state for the S levels as hover (0), just pressed (1),
dragging (2) and just released (3). On pen-down the position histories are
reset, so a stroke never starts with a streak from where the pen last was.

### 3. Bézier subdivision (`FUN_00438010`)

When `n ≥ 2`, every filtered packet is turned into **four** output points.
SAI keeps the last three filtered packets `p0 p1 p2` and evaluates the
cubic from `p0` to `p1` at `t = ¼, ½, ¾, 1` (`FUN_00437dc0` evaluates the
Bernstein form). The tangents come from `FUN_00437e60`: if both adjacent
segments have non-zero length, the control points lie along the chord
`p0→p2`, scaled by the lengths `|p0p1|` and `|p1p2|`. Pressure is
interpolated linearly. The stroke therefore lags the pen by one packet.

### 4. Pen-up tail (S levels)

On pen-up with an S level, SAI emits **16** extra points that walk from the
lagging filtered position to where the pen actually left. The heading starts
in the stroke's direction and turns toward the target in 16 equal steps
(the turn is applied only when the gap is more than 4 px, `1024/256` in
SAI's 24.8 fixed point), while the pressure fades to zero. This produces
SAI's characteristic tapered end on fast flicks.

## What is observed and what is inferred

| Part | Status |
|---|---|
| Level encoding, window formula, clamp to 16 | Observed in the decompiled code |
| Ring-buffer average and its two modes | Observed (`FUN_00437d30`) |
| One averaging stage for numeric levels, two for S levels | Observed (call pattern in `FUN_00438c60`) |
| History reset on level change and on pen-down | Observed |
| Four points per packet, `t = k/4`, linear pressure | Observed (`FUN_00438010`) |
| Control-point **scale factor** (potationsail uses ⅓ of the length-weighted chord, i.e. Catmull-Rom for even spacing) | **Inferred.** The exact expression is computed through `ftol` calls the decompiler did not recover. |
| Pen-up tail: 16 steps, turning heading, 4 px threshold | Observed structure. Step length and pressure fade are **inferred**. |
| Pressure smoothing | **Inferred.** SAI averages the pressure channel through the same helper, but which stage applies it is not fully resolved. potationsail averages pressure with the same window. |

Nothing here was checked by recording SAI's runtime output. A debugger
trace of `FUN_00438c60` would settle the inferred constants.

## Input sources in potationsail

* **Pen** (`WM_POINTER`, `PT_PEN`): every coalesced history packet is fed
  oldest first. Sub-pixel position comes from the HIMETRIC location, and
  pressure is 0..1024. Contact loss sends a pressure-0 packet so the
  S levels can produce their tail.
* **Mouse**: full pressure while the button is held, with a pressure-0
  packet on release.
