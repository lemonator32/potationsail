// A painting document backed by libsai.
//
// The loaded sai_document is kept alive for the whole session so everything
// potationsail does not understand (linework bodies, masks in /sublayers,
// unknown chunks, the work-time record, ...) is written back unchanged.
// Raster layers additionally get an 8-bit premultiplied BGRA working copy
// (the format SAI's compositor uses); only layers that were painted on are
// re-encoded on save, so untouched layers keep their original 16-bit data.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Blend.h"
#include "Pixel.h"
#include "sai.h"

namespace pot {

struct Raster {
    std::vector<PixelBGRA> px;  // canvas sized, premultiplied
    bool dirty = false;         // needs re-encoding on save
};

// A layer that has been removed from the document (for undo).  Owns the
// libsai record's heap blocks until it is attached again.
class DetachedLayer {
public:
    DetachedLayer() { meta_ = {}; }
    DetachedLayer(const sai_layer &meta, Raster &&r, bool hasRaster);
    DetachedLayer(DetachedLayer &&o) noexcept;
    DetachedLayer &operator=(DetachedLayer &&o) noexcept;
    DetachedLayer(const DetachedLayer &) = delete;
    DetachedLayer &operator=(const DetachedLayer &) = delete;
    ~DetachedLayer();

    bool valid() const { return valid_; }
    uint32_t id() const { return meta_.id; }
    size_t rasterBytes() const { return raster_.px.size() * sizeof(PixelBGRA); }

private:
    friend class Document;
    void release();
    sai_layer meta_;
    std::vector<sai_layer> subs_;  // masks (doc->sublayers) owned by this layer
    Raster raster_;
    bool hasRaster_ = false;
    bool valid_ = false;
};

class Document {
public:
    ~Document();
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    static std::unique_ptr<Document> createNew(int width, int height);
    static std::unique_ptr<Document> load(const std::string &utf8Path, std::string &error);
    bool save(const std::string &utf8Path, std::string &error);

    int width() const { return int(doc_->width); }
    int height() const { return int(doc_->height); }

    // Layers in drawing order (index 0 = bottom), layer sets included.
    size_t layerCount() const { return doc_->layer_count; }
    sai_layer &layer(size_t i) { return doc_->layers[i]; }
    const sai_layer &layer(size_t i) const { return doc_->layers[i]; }
    long indexOf(uint32_t id) const;  // -1 when absent
    uint32_t parentOf(size_t i) const;  // 0 = top level
    int depthOf(size_t i) const;

    Raster *raster(uint32_t id);
    const Raster *raster(uint32_t id) const;

    BlendMode blend(size_t i) const { return blendFromFourCC(doc_->layers[i].blend); }
    void setBlend(size_t i, BlendMode m) { doc_->layers[i].blend = fourCCFromBlend(m); }

    uint32_t selectedId() const { return doc_->selected_layer_id; }
    void setSelectedId(uint32_t id) { doc_->selected_layer_id = id; doc_->has_selected_layer = 1; }

    // Structure editing.  `index` is an array position (0 = bottom).
    uint32_t addRasterLayer(size_t index, const std::string &name, uint32_t parentSet);
    uint32_t addLayerSet(size_t index, const std::string &name, uint32_t parentSet);
    DetachedLayer detach(size_t index);
    void attach(size_t index, DetachedLayer &&layer);
    bool hasChildren(uint32_t setId) const;

    std::string nextLayerName() const;

    // Pre-multiplied composite of everything, over white, scaled to SAI's
    // thumbnail size; stored into the document before saving.
    void setThumbnail(const std::vector<PixelBGRA> &opaqueComposite);

    bool modified() const { return modified_; }
    void setModified(bool m = true) { modified_ = m; }

    sai_document *raw() { return doc_; }

private:
    Document() = default;
    void buildRasters();
    uint32_t moveLastTo(size_t index);  // libsai appends; move to `index`

    sai_document *doc_ = nullptr;
    std::unordered_map<uint32_t, Raster> rasters_;
    bool modified_ = false;
};

// Straight RGBA8 <-> premultiplied BGRA8 helpers (used by I/O and tests).
void rgbaToPremul(const uint8_t *rgba, PixelBGRA *out, size_t count);
void premulToRgba(const PixelBGRA *in, uint8_t *rgba, size_t count);

}  // namespace pot
