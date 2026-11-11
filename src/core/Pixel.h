// Pixel formats and small geometry helpers shared by the core.
#pragma once
#include <algorithm>
#include <cstdint>

namespace pot {

// Every in-memory raster in potationsail is premultiplied BGRA, 8 bits per
// channel, packed little-endian into a uint32_t (B in the low byte, A in the
// high byte).  That is exactly the format PaintTool SAI's display compositor
// works on (see docs/blend-modes.md), so the blend kernels can be reproduced
// bit for bit.
using PixelBGRA = uint32_t;

inline uint8_t chB(PixelBGRA p) { return uint8_t(p); }
inline uint8_t chG(PixelBGRA p) { return uint8_t(p >> 8); }
inline uint8_t chR(PixelBGRA p) { return uint8_t(p >> 16); }
inline uint8_t chA(PixelBGRA p) { return uint8_t(p >> 24); }
inline PixelBGRA packBGRA(unsigned b, unsigned g, unsigned r, unsigned a)
{
    return PixelBGRA(b) | (PixelBGRA(g) << 8) | (PixelBGRA(r) << 16) | (PixelBGRA(a) << 24);
}

// Straight-alpha RGB colour used by the UI (0..255 per channel).
struct Color {
    uint8_t r = 0, g = 0, b = 0;
};

struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // half-open [x0,x1) x [y0,y1)

    bool empty() const { return x1 <= x0 || y1 <= y0; }
    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
    void unite(const Rect &o)
    {
        if (o.empty()) return;
        if (empty()) { *this = o; return; }
        x0 = std::min(x0, o.x0); y0 = std::min(y0, o.y0);
        x1 = std::max(x1, o.x1); y1 = std::max(y1, o.y1);
    }
    Rect intersect(const Rect &o) const
    {
        Rect r{std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
        if (r.empty()) return Rect{};
        return r;
    }
};

}  // namespace pot
