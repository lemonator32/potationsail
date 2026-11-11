// Round-tip brush and eraser.
//
// A stroke accumulates a coverage mask with "maximum" combination (dabs of
// one stroke never build up past the stroke opacity, like SAI's pen), and the
// layer is recomputed from its pre-stroke copy wherever the mask changed.
#pragma once
#include <cstdint>
#include <vector>

#include "Pixel.h"

namespace pot {

struct BrushSettings {
    double size = 12.0;        // diameter in canvas pixels at full pressure
    double minSize = 0.0;      // fraction of `size` at zero pressure (0..1)
    double hardness = 0.85;    // 0 = soft airbrush edge, 1 = hard edge
    double spacing = 0.10;     // dab spacing as a fraction of the diameter
    int opacity = 100;         // 0..100, the stroke's maximum opacity
    bool pressureOpacity = false;
    bool eraser = false;
    Color color{0, 0, 0};
};

struct StrokeResult {
    Rect rect;                       // bounding box of everything touched
    std::vector<PixelBGRA> before;   // pixels of `rect` before the stroke
    std::vector<PixelBGRA> after;    // ... and after
};

class StrokeEngine {
public:
    void begin(std::vector<PixelBGRA> &layer, int width, int height, const BrushSettings &s,
               bool preserveOpacity);
    // Add a point (canvas coordinates).  Returns the area that changed.
    Rect addPoint(double x, double y, double pressure);
    StrokeResult end();
    bool active() const { return layer_ != nullptr; }

private:
    Rect dab(double cx, double cy, double pressure);
    void apply(const Rect &r);

    std::vector<PixelBGRA> *layer_ = nullptr;
    std::vector<PixelBGRA> base_;
    std::vector<uint16_t> cov_;
    int w_ = 0, h_ = 0;
    BrushSettings s_;
    bool preserve_ = false;
    bool hasLast_ = false;
    double lx_ = 0, ly_ = 0, lp_ = 0, carry_ = 0;
    Rect total_;
};

}  // namespace pot
