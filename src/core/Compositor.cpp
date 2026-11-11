#include "Compositor.h"

#include <cstring>

namespace pot {
namespace {

struct Region {
    const Document &doc;
    Rect r;
    int rw, rh, W;
};

inline unsigned div255(unsigned v) { return (v + 128 + ((v + 128) >> 8)) >> 8; }

// Scale a premultiplied pixel by a coverage 0..255 (clipping masks).
inline PixelBGRA scale(PixelBGRA p, unsigned k)
{
    return packBGRA(div255(chB(p) * k), div255(chG(p) * k), div255(chR(p) * k), div255(chA(p) * k));
}

// Lerp between two premultiplied pixels with weight 0..256 (pass-through sets
// with reduced opacity).
inline PixelBGRA lerp(PixelBGRA a, PixelBGRA b, int w)
{
    PixelBGRA out = 0;
    for (int s = 0; s < 32; s += 8) {
        int ca = int((a >> s) & 0xFF), cb = int((b >> s) & 0xFF);
        out |= PixelBGRA(ca + (((cb - ca) * w) >> 8)) << s;
    }
    return out;
}

// Nearest non-clipped layer below `i` within the same group (SAI's clipping
// base).  -1 when `i` is the lowest layer of its group.
long clipBase(const Document &doc, size_t i)
{
    uint32_t parent = doc.parentOf(i);
    for (long j = long(i) - 1; j >= 0; j--) {
        if (doc.parentOf(size_t(j)) != parent) continue;
        if (!doc.layer(size_t(j)).clipping) return j;
    }
    return -1;
}

void compositeGroup(const Region &g, uint32_t parent, std::vector<PixelBGRA> &dst, int depth)
{
    if (depth > 32) return;  // malformed parent chain
    const Document &doc = g.doc;
    std::vector<PixelBGRA> row(size_t(g.rw));
    for (size_t i = 0; i < doc.layerCount(); i++) {
        if (doc.parentOf(i) != parent) continue;
        const sai_layer &l = doc.layer(i);
        if (!l.visible) continue;
        const int op = opacityTo256(l.opacity);
        const BlendMode mode = doc.blend(i);

        if (l.type == SAI_LAYER_SET) {
            if (mode == BlendMode::PassThrough) {
                if (op >= 256) {
                    compositeGroup(g, l.id, dst, depth + 1);
                } else {
                    std::vector<PixelBGRA> copy = dst;
                    compositeGroup(g, l.id, copy, depth + 1);
                    for (size_t k = 0; k < dst.size(); k++) dst[k] = lerp(dst[k], copy[k], op);
                }
            } else {
                std::vector<PixelBGRA> tmp(dst.size(), 0);
                compositeGroup(g, l.id, tmp, depth + 1);
                blendSpan(mode, dst.data(), tmp.data(), int(dst.size()), op);
            }
            continue;
        }
        if (l.type != SAI_LAYER_RASTER) continue;  // linework etc.: not rendered
        const Raster *ras = doc.raster(l.id);
        if (!ras) continue;

        const Raster *base = nullptr;
        if (l.clipping) {
            long b = clipBase(doc, i);
            if (b >= 0) {
                const sai_layer &bl = doc.layer(size_t(b));
                if (!bl.visible) continue;  // hidden base hides its clipping group
                if (bl.type == SAI_LAYER_RASTER) base = doc.raster(bl.id);
            }
        }
        for (int y = 0; y < g.rh; y++) {
            const PixelBGRA *src = ras->px.data() + size_t(g.r.y0 + y) * g.W + g.r.x0;
            PixelBGRA *out = dst.data() + size_t(y) * g.rw;
            if (base) {
                const PixelBGRA *bp = base->px.data() + size_t(g.r.y0 + y) * g.W + g.r.x0;
                for (int x = 0; x < g.rw; x++) row[size_t(x)] = scale(src[x], chA(bp[x]));
                src = row.data();
            }
            blendSpan(mode, out, src, g.rw, op);
        }
    }
}

}  // namespace

void Compositor::composite(const Document &doc, Rect r, std::vector<PixelBGRA> &out)
{
    const int W = doc.width(), H = doc.height();
    r = r.intersect(Rect{0, 0, W, H});
    if (r.empty()) return;
    out.resize(size_t(W) * H);
    Region g{doc, r, r.width(), r.height(), W};
    std::vector<PixelBGRA> buf(size_t(g.rw) * g.rh, 0);
    compositeGroup(g, 0, buf, 0);
    for (int y = 0; y < g.rh; y++)
        std::memcpy(out.data() + size_t(r.y0 + y) * W + r.x0, buf.data() + size_t(y) * g.rw,
                    size_t(g.rw) * sizeof(PixelBGRA));
}

void Compositor::flattenOver(const std::vector<PixelBGRA> &src, Rect r, int W, PixelBGRA bg,
                             std::vector<PixelBGRA> &out)
{
    if (out.size() < src.size()) out.resize(src.size());
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) {
            size_t k = size_t(y) * W + x;
            PixelBGRA p = src[k];
            unsigned inv = 255 - chA(p);
            auto ch = [inv](unsigned c, unsigned b) {
                unsigned v = c + div255(b * inv);
                return v > 255 ? 255u : v;
            };
            out[k] = packBGRA(ch(chB(p), chB(bg)), ch(chG(p), chG(bg)), ch(chR(p), chR(bg)), 255);
        }
}

}  // namespace pot
