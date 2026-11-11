// PaintTool SAI layer blend modes, reproduced from sai.exe 1.2.6-Beta.3.
//
// The kernels in Blend.cpp are a scalar transcription of SAI's MMX routines
// (table at VA 0x53c880, functions 0x49d690..0x49de30).  They reproduce the
// same 15-bit fixed point, saturating adds and rounding instruction by
// instruction.  They have not been compared pixel-for-pixel against SAI's
// rendered output; see docs/blend-modes.md for the derivation and caveats.
#pragma once
#include <cstdint>

#include "Pixel.h"

namespace pot {

// Internal mode numbers are SAI's own (the switch in FUN_00463c50 that maps
// the stored FourCC to the layer struct byte at +0x50).
enum class BlendMode : uint8_t {
    PassThrough = 0,  // 'pass'  layer sets only
    Normal = 1,       // 'norm'
    Multiply = 2,     // 'mul '
    Screen = 3,       // 'scrn'
    Overlay = 4,      // 'over'
    Luminosity = 5,   // 'add '  (shown as "Luminosity" in SAI's UI)
    Shade = 6,        // 'sub '
    LumiShade = 7,    // 'adsb'  ("Lumi & Shade")
    BinaryColor = 8,  // 'cbin'
};
constexpr int kBlendModeCount = 9;

BlendMode blendFromFourCC(uint32_t fourcc);  // unknown values -> Normal (as SAI)
uint32_t fourCCFromBlend(BlendMode mode);
const char *blendName(BlendMode mode);

// Layer opacity (0..100 %) to the 0..256 multiplier SAI's kernels take.
int opacityTo256(int percent);

// Blend `count` source pixels onto `dst` in place.
//   opacity256  0..256, applied to the source (ignored by BinaryColor, as in SAI)
//   destOpaque  SAI's first kernel argument: when set, the destination alpha is
//               treated as fully opaque (it is OR-ed with 0x7FFF).
void blendSpan(BlendMode mode, PixelBGRA *dst, const PixelBGRA *src, int count,
               int opacity256, bool destOpaque = false);

PixelBGRA blendPixel(BlendMode mode, PixelBGRA dst, PixelBGRA src, int opacity256,
                     bool destOpaque = false);

}  // namespace pot
