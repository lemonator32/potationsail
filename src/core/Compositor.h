// Flattens a Document's layer stack with SAI's blend kernels.
#pragma once
#include <vector>

#include "Document.h"
#include "Pixel.h"

namespace pot {

class Compositor {
public:
    // Composite rectangle `r` (canvas coordinates) of all visible layers into
    // `out`, a canvas-sized premultiplied BGRA buffer.  Pixels outside `r` are
    // not touched.  The result is the transparent-backdrop composite.
    static void composite(const Document &doc, Rect r, std::vector<PixelBGRA> &out);

    // `src` (premultiplied) over an opaque colour, for display / thumbnails.
    static void flattenOver(const std::vector<PixelBGRA> &src, Rect r, int width,
                            PixelBGRA background, std::vector<PixelBGRA> &out);
};

}  // namespace pot
