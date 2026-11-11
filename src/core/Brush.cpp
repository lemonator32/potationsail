#include "Brush.h"

#include <cmath>
#include <cstring>

namespace pot {

namespace {
inline unsigned div255(unsigned v) { return (v + 128 + ((v + 128) >> 8)) >> 8; }
}

void StrokeEngine::begin(std::vector<PixelBGRA> &layer, int width, int height,
                         const BrushSettings &s, bool preserveOpacity)
{
    layer_ = &layer;
    w_ = width;
    h_ = height;
    s_ = s;
    preserve_ = preserveOpacity;
    base_ = layer;
    cov_.assign(size_t(width) * height, 0);
    hasLast_ = false;
    carry_ = 0;
    total_ = Rect{};
}

Rect StrokeEngine::addPoint(double x, double y, double p)
{
    if (!layer_) return Rect{};
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    Rect dirty;
    if (!hasLast_) {
        hasLast_ = true;
        lx_ = x; ly_ = y; lp_ = p;
        dirty.unite(dab(x, y, p));
    } else {
        double dx = x - lx_, dy = y - ly_;
        double len = std::sqrt(dx * dx + dy * dy);
        double t = 0;
        // Walk the segment placing dabs every `spacing * diameter` pixels.
        while (true) {
            double pr = lp_ + (p - lp_) * (len > 0 ? t / len : 1);
            double diam = s_.size * (s_.minSize + (1 - s_.minSize) * pr);
            double step = std::max(0.5, diam * s_.spacing);
            double next = t + step - carry_;
            if (next > len) {
                carry_ += len - t;
                break;
            }
            t = next;
            carry_ = 0;
            double f = len > 0 ? t / len : 1;
            dirty.unite(dab(lx_ + dx * f, ly_ + dy * f, lp_ + (p - lp_) * f));
        }
        lx_ = x; ly_ = y; lp_ = p;
    }
    if (!dirty.empty()) {
        apply(dirty);
        total_.unite(dirty);
    }
    return dirty;
}

// Stamp one dab into the coverage mask (max blending).
Rect StrokeEngine::dab(double cx, double cy, double p)
{
    double diam = s_.size * (s_.minSize + (1 - s_.minSize) * p);
    if (diam <= 0.05 || p <= 0) return Rect{};
    double r = diam * 0.5;
    double strength = s_.pressureOpacity ? p : 1.0;
    // Very small dabs: keep the radius at 0.5 px and fade them instead.
    if (r < 0.5) {
        strength *= r * 2;
        r = 0.5;
    }
    double hard = std::min(std::max(s_.hardness, 0.0), 1.0);
    double inner = r * hard;
    Rect box{int(std::floor(cx - r - 1)), int(std::floor(cy - r - 1)), int(std::ceil(cx + r + 1)),
             int(std::ceil(cy + r + 1))};
    box = box.intersect(Rect{0, 0, w_, h_});
    if (box.empty()) return box;
    const double peak = strength * 65535.0;
    for (int y = box.y0; y < box.y1; y++) {
        double py = y + 0.5 - cy;
        for (int x = box.x0; x < box.x1; x++) {
            double px = x + 0.5 - cx;
            double d = std::sqrt(px * px + py * py);
            double edge = r + 0.5 - d;  // 1 px anti-aliased rim
            if (edge <= 0) continue;
            double c = edge >= 1 ? 1 : edge;
            if (d > inner && r > inner) {
                double t = (d - inner) / (r - inner);
                t = t > 1 ? 1 : t;
                c *= 1 - t * t * (3 - 2 * t);  // smoothstep falloff
            }
            uint16_t v = uint16_t(c * peak + 0.5);
            uint16_t &m = cov_[size_t(y) * w_ + x];
            if (v > m) m = v;
        }
    }
    return box;
}

// Recompute layer pixels inside `r` from the pre-stroke copy and the mask.
void StrokeEngine::apply(const Rect &r)
{
    const unsigned op = unsigned(std::min(std::max(s_.opacity, 0), 100) * 65535 / 100);
    const unsigned cr = s_.color.r, cg = s_.color.g, cb = s_.color.b;
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) {
            size_t k = size_t(y) * w_ + x;
            unsigned a = unsigned((uint64_t(cov_[k]) * op / 65535 * 255 + 32767) / 65535);
            PixelBGRA b = base_[k];
            if (a == 0) {
                (*layer_)[k] = b;
                continue;
            }
            unsigned inv = 255 - a;
            PixelBGRA out;
            if (s_.eraser) {
                if (preserve_) {
                    out = b;  // a layer with protected opacity cannot be erased
                } else {
                    out = packBGRA(div255(chB(b) * inv), div255(chG(b) * inv),
                                   div255(chR(b) * inv), div255(chA(b) * inv));
                }
            } else if (preserve_) {
                unsigned ba = chA(b);
                auto mix = [&](unsigned bc, unsigned c) {
                    unsigned target = div255(c * ba);
                    return unsigned(int(bc) + (int(target) - int(bc)) * int(a) / 255);
                };
                out = packBGRA(mix(chB(b), cb), mix(chG(b), cg), mix(chR(b), cr), ba);
            } else {
                out = packBGRA(div255(cb * a) + div255(chB(b) * inv),
                               div255(cg * a) + div255(chG(b) * inv),
                               div255(cr * a) + div255(chR(b) * inv), a + div255(chA(b) * inv));
            }
            (*layer_)[k] = out;
        }
}

StrokeResult StrokeEngine::end()
{
    StrokeResult res;
    if (!layer_) return res;
    res.rect = total_;
    if (!total_.empty()) {
        int rw = total_.width();
        res.before.resize(size_t(rw) * total_.height());
        res.after.resize(res.before.size());
        for (int y = total_.y0; y < total_.y1; y++) {
            size_t src = size_t(y) * w_ + total_.x0, dst = size_t(y - total_.y0) * rw;
            std::memcpy(&res.before[dst], &base_[src], size_t(rw) * sizeof(PixelBGRA));
            std::memcpy(&res.after[dst], &(*layer_)[src], size_t(rw) * sizeof(PixelBGRA));
        }
    }
    layer_ = nullptr;
    base_.clear();
    base_.shrink_to_fit();
    cov_.clear();
    cov_.shrink_to_fit();
    return res;
}

}  // namespace pot
