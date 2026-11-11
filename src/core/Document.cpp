#include "Document.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pot {

// ---------------------------------------------------------------------------
// Pixel conversion
// ---------------------------------------------------------------------------
void rgbaToPremul(const uint8_t *s, PixelBGRA *out, size_t count)
{
    for (size_t i = 0; i < count; i++, s += 4) {
        unsigned a = s[3];
        auto pm = [a](unsigned c) { return (c * a + 127) / 255; };
        out[i] = packBGRA(pm(s[2]), pm(s[1]), pm(s[0]), a);
    }
}

void premulToRgba(const PixelBGRA *in, uint8_t *o, size_t count)
{
    for (size_t i = 0; i < count; i++, o += 4) {
        PixelBGRA p = in[i];
        unsigned a = chA(p);
        if (!a) {
            o[0] = o[1] = o[2] = o[3] = 0;
            continue;
        }
        auto un = [a](unsigned c) {
            unsigned v = (c * 255 + a / 2) / a;
            return uint8_t(v > 255 ? 255 : v);
        };
        o[0] = un(chR(p));
        o[1] = un(chG(p));
        o[2] = un(chB(p));
        o[3] = uint8_t(a);
    }
}

// ---------------------------------------------------------------------------
// DetachedLayer
// ---------------------------------------------------------------------------
namespace {
void freeLayerHeap(sai_layer &l)
{
    for (size_t i = 0; i < l.extra_chunk_count; i++) free(l.extra_chunks[i].data);
    free(l.extra_chunks);
    free(l.pixels);
    free(l.tile_map);
    free(l.body);
    std::memset(&l, 0, sizeof(l));
}
}  // namespace

DetachedLayer::DetachedLayer(const sai_layer &meta, Raster &&r, bool hasRaster)
    : meta_(meta), raster_(std::move(r)), hasRaster_(hasRaster), valid_(true)
{
}

DetachedLayer::DetachedLayer(DetachedLayer &&o) noexcept
    : meta_(o.meta_), subs_(std::move(o.subs_)), raster_(std::move(o.raster_)),
      hasRaster_(o.hasRaster_), valid_(o.valid_)
{
    o.valid_ = false;
    o.subs_.clear();
    std::memset(&o.meta_, 0, sizeof(o.meta_));
}

DetachedLayer &DetachedLayer::operator=(DetachedLayer &&o) noexcept
{
    if (this != &o) {
        release();
        meta_ = o.meta_;
        subs_ = std::move(o.subs_);
        o.subs_.clear();
        raster_ = std::move(o.raster_);
        hasRaster_ = o.hasRaster_;
        valid_ = o.valid_;
        o.valid_ = false;
        std::memset(&o.meta_, 0, sizeof(o.meta_));
    }
    return *this;
}

DetachedLayer::~DetachedLayer() { release(); }

void DetachedLayer::release()
{
    if (valid_) {
        freeLayerHeap(meta_);
        for (auto &s : subs_) freeLayerHeap(s);
    }
    subs_.clear();
    valid_ = false;
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------
Document::~Document() { sai_document_free(doc_); }

std::unique_ptr<Document> Document::createNew(int width, int height)
{
    if (width < 1 || height < 1 || width > 10000 || height > 10000) return nullptr;
    std::unique_ptr<Document> d(new Document());
    d->doc_ = sai_document_new(uint32_t(width), uint32_t(height));
    if (!d->doc_) return nullptr;
    sai_layer *l = sai_document_add_layer(d->doc_, "Layer1");
    if (!l) return nullptr;
    d->setSelectedId(l->id);
    d->buildRasters();
    for (auto &kv : d->rasters_) kv.second.dirty = true;
    return d;
}

std::unique_ptr<Document> Document::load(const std::string &path, std::string &error)
{
    sai_document *raw = nullptr;
    int e = sai_document_load(path.c_str(), &raw);
    if (e != SAI_OK) {
        error = sai_strerror(e);
        return nullptr;
    }
    std::unique_ptr<Document> d(new Document());
    d->doc_ = raw;
    d->buildRasters();
    if (d->indexOf(d->selectedId()) < 0 && d->layerCount())
        d->setSelectedId(d->layer(d->layerCount() - 1).id);
    return d;
}

void Document::buildRasters()
{
    const size_t n = size_t(width()) * size_t(height());
    std::vector<uint8_t> rgba(n * 4);
    for (size_t i = 0; i < doc_->layer_count; i++) {
        sai_layer &l = doc_->layers[i];
        if (l.type != SAI_LAYER_RASTER) continue;
        Raster r;
        r.px.assign(n, 0);
        if (l.pixels && sai_layer_get_rgba8(&l, doc_->width, doc_->height, rgba.data()) == SAI_OK)
            rgbaToPremul(rgba.data(), r.px.data(), n);
        rasters_[l.id] = std::move(r);
    }
}

bool Document::save(const std::string &path, std::string &error)
{
    const size_t n = size_t(width()) * size_t(height());
    std::vector<uint8_t> rgba;
    for (size_t i = 0; i < doc_->layer_count; i++) {
        sai_layer &l = doc_->layers[i];
        Raster *r = raster(l.id);
        if (!r || !r->dirty) continue;
        rgba.resize(n * 4);
        premulToRgba(r->px.data(), rgba.data(), n);
        int e = sai_layer_set_rgba8(&l, doc_->width, doc_->height, rgba.data());
        if (e != SAI_OK) {
            error = sai_strerror(e);
            return false;
        }
        r->dirty = false;
    }
    int e = sai_document_save(doc_, path.c_str());
    if (e != SAI_OK) {
        error = sai_strerror(e);
        return false;
    }
    modified_ = false;
    return true;
}

long Document::indexOf(uint32_t id) const
{
    for (size_t i = 0; i < doc_->layer_count; i++)
        if (doc_->layers[i].id == id) return long(i);
    return -1;
}

uint32_t Document::parentOf(size_t i) const
{
    const sai_layer &l = doc_->layers[i];
    return l.has_parent_set ? l.parent_set_id : 0;
}

int Document::depthOf(size_t i) const
{
    int depth = 0;
    uint32_t p = parentOf(i);
    while (p && depth < 32) {
        long j = indexOf(p);
        if (j < 0) break;
        depth++;
        p = parentOf(size_t(j));
    }
    return depth;
}

Raster *Document::raster(uint32_t id)
{
    auto it = rasters_.find(id);
    return it == rasters_.end() ? nullptr : &it->second;
}
const Raster *Document::raster(uint32_t id) const
{
    auto it = rasters_.find(id);
    return it == rasters_.end() ? nullptr : &it->second;
}

bool Document::hasChildren(uint32_t setId) const
{
    for (size_t i = 0; i < doc_->layer_count; i++)
        if (parentOf(i) == setId) return true;
    return false;
}

uint32_t Document::moveLastTo(size_t index)
{
    size_t last = doc_->layer_count - 1;
    if (index > last) index = last;
    sai_layer tmp = doc_->layers[last];
    std::memmove(&doc_->layers[index + 1], &doc_->layers[index], (last - index) * sizeof(sai_layer));
    doc_->layers[index] = tmp;
    return tmp.id;
}

uint32_t Document::addRasterLayer(size_t index, const std::string &name, uint32_t parentSet)
{
    sai_layer *l = sai_document_add_layer(doc_, name.c_str());
    if (!l) return 0;
    if (parentSet) {
        l->has_parent_set = 1;
        l->parent_set_id = parentSet;
    }
    Raster r;
    r.px.assign(size_t(width()) * size_t(height()), 0);
    r.dirty = true;
    rasters_[l->id] = std::move(r);
    modified_ = true;
    return moveLastTo(index);
}

uint32_t Document::addLayerSet(size_t index, const std::string &name, uint32_t parentSet)
{
    sai_layer *l = sai_document_add_set(doc_, name.c_str());
    if (!l) return 0;
    if (parentSet) {
        l->has_parent_set = 1;
        l->parent_set_id = parentSet;
    }
    l->has_set_open = 1;
    l->set_open = 1;
    modified_ = true;
    return moveLastTo(index);
}

DetachedLayer Document::detach(size_t index)
{
    if (index >= doc_->layer_count) return DetachedLayer();
    sai_layer meta = doc_->layers[index];
    Raster r;
    bool hasRaster = false;
    auto it = rasters_.find(meta.id);
    if (it != rasters_.end()) {
        r = std::move(it->second);
        rasters_.erase(it);
        hasRaster = true;
    }
    size_t tail = doc_->layer_count - index - 1;
    std::memmove(&doc_->layers[index], &doc_->layers[index + 1], tail * sizeof(sai_layer));
    doc_->layer_count--;

    DetachedLayer out(meta, std::move(r), hasRaster);
    // Masks that belong to this layer travel with it.
    size_t keep = 0;
    for (size_t i = 0; i < doc_->sublayer_count; i++) {
        sai_layer &s = doc_->sublayers[i];
        if (s.has_parent_layer && s.parent_layer_id == meta.id)
            out.subs_.push_back(s);
        else
            doc_->sublayers[keep++] = s;
    }
    doc_->sublayer_count = keep;
    modified_ = true;
    return out;
}

void Document::attach(size_t index, DetachedLayer &&dl)
{
    if (!dl.valid_) return;
    size_t n = doc_->layer_count;
    if (index > n) index = n;
    auto *grown = static_cast<sai_layer *>(realloc(doc_->layers, (n + 1) * sizeof(sai_layer)));
    if (!grown) return;  // keep the detached layer alive; nothing changed
    doc_->layers = grown;
    std::memmove(&grown[index + 1], &grown[index], (n - index) * sizeof(sai_layer));
    grown[index] = dl.meta_;
    doc_->layer_count = n + 1;
    if (!dl.subs_.empty()) {
        size_t m = doc_->sublayer_count, k = dl.subs_.size();
        auto *subs = static_cast<sai_layer *>(realloc(doc_->sublayers, (m + k) * sizeof(sai_layer)));
        if (subs) {
            std::memcpy(subs + m, dl.subs_.data(), k * sizeof(sai_layer));
            doc_->sublayers = subs;
            doc_->sublayer_count = m + k;
        } else {
            for (auto &s : dl.subs_) freeLayerHeap(s);  // cannot keep the masks
        }
        dl.subs_.clear();
    }
    if (dl.hasRaster_) rasters_[dl.meta_.id] = std::move(dl.raster_);
    dl.valid_ = false;  // ownership moved into the document
    std::memset(&dl.meta_, 0, sizeof(dl.meta_));
    modified_ = true;
}

std::string Document::nextLayerName() const
{
    int best = 0;
    for (size_t i = 0; i < doc_->layer_count; i++) {
        int k = 0;
        if (std::sscanf(doc_->layers[i].name, "Layer%d", &k) == 1 && k > best) best = k;
    }
    return "Layer" + std::to_string(best + 1);
}

// Thumbnail size rule from FUN_00462350: halve until the longer side / f < 97.
void Document::setThumbnail(const std::vector<PixelBGRA> &img)
{
    const int w = width(), h = height(), m = w > h ? w : h;
    int f = 1;
    for (int cand : {16, 8, 4, 2})
        if (m / cand >= 97) {
            f = cand;
            break;
        }
    int tw = w / f, th = h / f;
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    auto *t = static_cast<uint8_t *>(malloc(size_t(tw) * th * 4));
    if (!t) return;
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            unsigned sum[3] = {0, 0, 0}, cnt = 0;
            for (int dy = 0; dy < f; dy++)
                for (int dx = 0; dx < f; dx++) {
                    int sx = x * f + dx, sy = y * f + dy;
                    if (sx >= w || sy >= h) continue;
                    PixelBGRA p = img[size_t(sy) * w + sx];
                    sum[0] += chB(p); sum[1] += chG(p); sum[2] += chR(p);
                    cnt++;
                }
            uint8_t *o = t + (size_t(y) * tw + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = uint8_t(cnt ? (sum[c] + cnt / 2) / cnt : 255);
            o[3] = 255;
        }
    free(doc_->thumb_bgra);
    doc_->thumb_bgra = t;
    doc_->thumb_width = uint32_t(tw);
    doc_->thumb_height = uint32_t(th);
}

}  // namespace pot
