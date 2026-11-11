// Unit tests for potcore (no framework; returns non-zero on failure).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Blend.h"
#include "Brush.h"
#include "Compositor.h"
#include "Document.h"
#include "Stabilizer.h"
#include "Undo.h"

using namespace pot;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            g_pass++;                                                            \
        } else {                                                                 \
            g_fail++;                                                            \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::abs(double(a) - double(b)) <= double(tol))

static PixelBGRA opaque(unsigned r, unsigned g, unsigned b) { return packBGRA(b, g, r, 255); }

// ---------------------------------------------------------------------------
static void testBlendIdentities()
{
    const PixelBGRA d = opaque(100, 150, 200);
    const PixelBGRA s = opaque(60, 120, 240);

    // Transparent source pixels are skipped by every kernel.
    for (int m = 0; m < kBlendModeCount; m++) CHECK(blendPixel(BlendMode(m), d, 0, 256) == d);

    // Normal: opaque source replaces the backdrop (within SAI's 15-bit rounding).
    PixelBGRA n = blendPixel(BlendMode::Normal, d, s, 256);
    CHECK_NEAR(chR(n), 60, 1); CHECK_NEAR(chG(n), 120, 1); CHECK_NEAR(chB(n), 240, 1);
    CHECK(chA(n) == 255);

    // Zero opacity leaves the backdrop alone.
    PixelBGRA z = blendPixel(BlendMode::Normal, d, s, 0);
    CHECK_NEAR(chR(z), 100, 1); CHECK_NEAR(chG(z), 150, 1); CHECK_NEAR(chB(z), 200, 1);

    // Multiply: d*s/255.
    PixelBGRA mu = blendPixel(BlendMode::Multiply, d, s, 256);
    CHECK_NEAR(chR(mu), 100 * 60 / 255.0, 2);
    CHECK_NEAR(chG(mu), 150 * 120 / 255.0, 2);
    CHECK_NEAR(chB(mu), 200 * 240 / 255.0, 2);

    // Screen: d + s - d*s.
    PixelBGRA sc = blendPixel(BlendMode::Screen, d, s, 256);
    CHECK_NEAR(chR(sc), 100 + 60 - 100 * 60 / 255.0, 2);
    CHECK_NEAR(chB(sc), 200 + 240 - 200 * 240 / 255.0, 2);

    // Overlay: d < 128 -> 2ds, d >= 128 -> 1 - 2(1-d)(1-s).
    PixelBGRA ov = blendPixel(BlendMode::Overlay, d, s, 256);
    CHECK_NEAR(chR(ov), 2 * 100 * 60 / 255.0, 2);
    CHECK_NEAR(chB(ov), 255 - 2 * (255 - 200) * (255 - 240) / 255.0, 2);
}

// The 'add ' mode (SAI calls it Luminosity) is the one the user asked about.
static void testLuminosity()
{
    const PixelBGRA d = opaque(100, 150, 200);
    const PixelBGRA s = opaque(60, 120, 240);

    // Opaque on opaque: a clamped add.
    PixelBGRA a = blendPixel(BlendMode::Luminosity, d, s, 256);
    CHECK_NEAR(chR(a), 160, 1);
    CHECK_NEAR(chG(a), 255, 0);
    CHECK_NEAR(chB(a), 255, 0);
    CHECK(chA(a) == 255);

    // Half opacity adds half the light.
    PixelBGRA h = blendPixel(BlendMode::Luminosity, d, s, 128);
    CHECK_NEAR(chR(h), 130, 1);
    CHECK_NEAR(chG(h), 210, 1);

    // Black adds nothing - and, unlike Normal, never darkens the backdrop.
    PixelBGRA blk = blendPixel(BlendMode::Luminosity, d, opaque(0, 0, 0), 256);
    CHECK_NEAR(chR(blk), 100, 1); CHECK_NEAR(chG(blk), 150, 1); CHECK_NEAR(chB(blk), 200, 1);

    // Over a fully transparent backdrop the layer shows as if Normal.
    PixelBGRA t = blendPixel(BlendMode::Luminosity, 0, s, 256);
    CHECK_NEAR(chR(t), 60, 1); CHECK_NEAR(chB(t), 240, 1); CHECK(chA(t) == 255);

    // Premultiplied semi-transparent source: adds S * Da.
    PixelBGRA semi = packBGRA(0, 0, 64, 128);  // red 64 premultiplied, alpha 128
    PixelBGRA r = blendPixel(BlendMode::Luminosity, opaque(10, 10, 10), semi, 256);
    CHECK_NEAR(chR(r), 10 + 64, 1);
    CHECK_NEAR(chG(r), 10, 1);

    // Luminosity result can never exceed the backdrop alpha (premultiplied valid).
    PixelBGRA da = packBGRA(50, 50, 50, 100);
    PixelBGRA q = blendPixel(BlendMode::Luminosity, da, opaque(255, 255, 255), 256);
    CHECK(chR(q) <= chA(q) && chG(q) <= chA(q) && chB(q) <= chA(q));
}

static void testShadeAndLumiShade()
{
    const PixelBGRA d = opaque(100, 150, 200);
    // Shade subtracts the inverse: white leaves the backdrop, black clears it.
    PixelBGRA w = blendPixel(BlendMode::Shade, d, opaque(255, 255, 255), 256);
    CHECK_NEAR(chR(w), 100, 1);
    PixelBGRA b = blendPixel(BlendMode::Shade, d, opaque(0, 0, 0), 256);
    CHECK_NEAR(chR(b), 0, 1); CHECK_NEAR(chB(b), 0, 1);
    PixelBGRA m = blendPixel(BlendMode::Shade, d, opaque(205, 205, 205), 256);
    CHECK_NEAR(chR(m), 50, 2);

    // Lumi & Shade: 50 % grey is neutral, lighter adds, darker subtracts.
    PixelBGRA g = blendPixel(BlendMode::LumiShade, d, opaque(128, 128, 128), 256);
    CHECK_NEAR(chR(g), 100, 2); CHECK_NEAR(chB(g), 200, 2);
    PixelBGRA l = blendPixel(BlendMode::LumiShade, d, opaque(178, 178, 178), 256);
    CHECK_NEAR(chR(l), 200, 3);
    PixelBGRA k = blendPixel(BlendMode::LumiShade, d, opaque(78, 78, 78), 256);
    CHECK_NEAR(chR(k), 0, 2);
}

static void testBinaryIgnoresOpacity()
{
    const PixelBGRA d = opaque(100, 150, 200), s = opaque(10, 20, 30);
    CHECK(blendPixel(BlendMode::BinaryColor, d, s, 0) == blendPixel(BlendMode::BinaryColor, d, s, 256));
}

static void testFourCC()
{
    CHECK(blendFromFourCC(SAI_BLEND_LUMINOSITY) == BlendMode::Luminosity);
    CHECK(fourCCFromBlend(BlendMode::Luminosity) == SAI_BLEND_LUMINOSITY);
    CHECK(blendFromFourCC(0x12345678) == BlendMode::Normal);
    for (int m = 0; m < kBlendModeCount; m++)
        CHECK(blendFromFourCC(fourCCFromBlend(BlendMode(m))) == BlendMode(m));
}

// ---------------------------------------------------------------------------
static void testStabilizerLevels()
{
    CHECK(Stabilizer::windowFor(0) == 1);
    CHECK(Stabilizer::windowFor(1) == 1);
    CHECK(Stabilizer::windowFor(5) == 5);
    CHECK(Stabilizer::windowFor(15) == 15);
    CHECK(Stabilizer::windowFor(Stabilizer::S(1)) == 2);
    CHECK(Stabilizer::windowFor(Stabilizer::S(7)) == 8);
    CHECK(Stabilizer::isValidLevel(0x87) && !Stabilizer::isValidLevel(0x88) && !Stabilizer::isValidLevel(16));
}

// A noisy straight line: higher levels must reduce jitter.
static double jitterAt(int level)
{
    Stabilizer st;
    st.setLevel(level);
    std::vector<PenSample> out;
    std::srand(1234);
    for (int i = 0; i < 400; i++) {
        double noise = (std::rand() % 2001 - 1000) / 250.0;  // +-4 px
        st.feed(PenSample{i * 2.0, 100.0 + noise, 0.8}, out);
    }
    double sum = 0;
    for (auto &p : out) sum += (p.y - 100.0) * (p.y - 100.0);
    return std::sqrt(sum / double(out.size()));
}

static void testStabilizerSmooths()
{
    double raw = jitterAt(0), j5 = jitterAt(5), j8 = jitterAt(8), j15 = jitterAt(15);
    double s1 = jitterAt(Stabilizer::S(1)), s7 = jitterAt(Stabilizer::S(7));
    std::printf("  jitter rms: off %.3f  5 %.3f  8 %.3f  15 %.3f  S-1 %.3f  S-7 %.3f\n", raw, j5,
                j8, j15, s1, s7);
    CHECK(j5 < raw * 0.6);
    CHECK(j15 < j5);
    // S-k uses the same window as level k+1 plus a second averaging stage.
    CHECK(s7 < j8);
    CHECK(s7 < s1);
}

static void testStabilizerOutputShape()
{
    Stabilizer st;
    st.setLevel(0);
    std::vector<PenSample> out;
    st.feed(PenSample{1, 2, 0.5}, out);
    CHECK(out.size() == 1 && out[0].x == 1 && out[0].y == 2);

    st.setLevel(3);
    out.clear();
    st.feed(PenSample{0, 0, 0.5}, out);
    CHECK(out.size() == 4);  // one Bezier segment per filtered packet

    // Constant input converges to the input.
    out.clear();
    for (int i = 0; i < 40; i++) st.feed(PenSample{50, 60, 1.0}, out);
    CHECK_NEAR(out.back().x, 50, 1e-9);
    CHECK_NEAR(out.back().y, 60, 1e-9);
}

static void testStabilizerSTail()
{
    Stabilizer st;
    st.setLevel(Stabilizer::S(5));
    std::vector<PenSample> out;
    for (int i = 0; i <= 30; i++) st.feed(PenSample{i * 10.0, 0, 1.0}, out);
    size_t before = out.size();
    st.feed(PenSample{300, 0, 0.0}, out);  // pen up
    CHECK(out.size() == before + 4 + 16);  // last segment + catch-up tail
    // The tail ends at the pen-up position with zero pressure.
    CHECK_NEAR(out.back().x, 300, 0.5);
    CHECK_NEAR(out.back().pressure, 0, 1e-9);
    // The lagging stroke is behind the pen before the tail.
    CHECK(out[before + 3].x < 290);
}

// ---------------------------------------------------------------------------
static void testBrush()
{
    const int W = 64, H = 64;
    std::vector<PixelBGRA> layer(W * H, 0);
    BrushSettings bs;
    bs.size = 10;
    bs.hardness = 1;
    bs.color = Color{255, 0, 0};
    StrokeEngine e;
    e.begin(layer, W, H, bs, false);
    e.addPoint(10, 32, 1);
    e.addPoint(50, 32, 1);
    StrokeResult r = e.end();
    CHECK(chA(layer[32 * W + 30]) == 255);
    CHECK(chR(layer[32 * W + 30]) == 255);
    CHECK(chA(layer[5 * W + 30]) == 0);
    CHECK(!r.rect.empty() && r.before.size() == r.after.size());

    // Overlapping dabs of one stroke do not exceed the stroke opacity.
    std::vector<PixelBGRA> l2(W * H, 0);
    bs.opacity = 50;
    e.begin(l2, W, H, bs, false);
    for (int i = 0; i < 20; i++) e.addPoint(32, 32, 1);
    e.end();
    CHECK_NEAR(chA(l2[32 * W + 32]), 128, 1);

    // Eraser
    bs.opacity = 100;
    bs.eraser = true;
    e.begin(layer, W, H, bs, false);
    e.addPoint(30, 32, 1);
    e.end();
    CHECK(chA(layer[32 * W + 30]) == 0);
}

// ---------------------------------------------------------------------------
static std::string tempPath(const char *name)
{
    const char *t = std::getenv("TEMP");
    if (!t) t = std::getenv("TMPDIR");
    std::string base = t ? t : ".";
    return base + "/" + name;
}

static void testDocumentRoundTrip()
{
    auto doc = Document::createNew(100, 80);
    CHECK(doc != nullptr);
    if (!doc) return;
    CHECK(doc->layerCount() == 1);
    uint32_t base = doc->layer(0).id;
    uint32_t top = doc->addRasterLayer(1, "Glow", 0);
    CHECK(doc->layerCount() == 2 && doc->layer(1).id == top);
    doc->setBlend(1, BlendMode::Luminosity);
    doc->layer(1).opacity = 70;

    // Paint: base dark blue, top an orange square.
    for (auto &p : doc->raster(base)->px) p = opaque(20, 30, 90);
    Raster *t = doc->raster(top);
    for (int y = 20; y < 40; y++)
        for (int x = 30; x < 60; x++) t->px[size_t(y) * 100 + x] = opaque(200, 100, 0);
    doc->raster(base)->dirty = t->dirty = true;

    std::vector<PixelBGRA> comp;
    Compositor::composite(*doc, Rect{0, 0, 100, 80}, comp);
    PixelBGRA expect = blendPixel(BlendMode::Luminosity, opaque(20, 30, 90), opaque(200, 100, 0),
                                  opacityTo256(70));
    CHECK(comp[30 * 100 + 40] == expect);
    CHECK(comp[0] == blendPixel(BlendMode::Normal, 0, opaque(20, 30, 90), 256));

    std::string path = tempPath("potationsail_test.sai"), err;
    doc->setThumbnail(comp);
    CHECK(doc->save(path, err));
    if (!err.empty()) std::printf("  save error: %s\n", err.c_str());

    auto back = Document::load(path, err);
    CHECK(back != nullptr);
    if (!back) return;
    CHECK(back->width() == 100 && back->height() == 80);
    CHECK(back->layerCount() == 2);
    CHECK(back->blend(1) == BlendMode::Luminosity);
    CHECK(back->layer(1).opacity == 70);
    CHECK(std::string(back->layer(1).name) == "Glow");
    const Raster *bt = back->raster(back->layer(1).id);
    CHECK(bt && bt->px[30 * 100 + 40] == opaque(200, 100, 0));
    CHECK(bt && chA(bt->px[0]) == 0);

    std::vector<PixelBGRA> comp2;
    Compositor::composite(*back, Rect{0, 0, 100, 80}, comp2);
    CHECK(comp2[30 * 100 + 40] == expect);
    std::remove(path.c_str());
}

static void testSetsAndClipping()
{
    auto doc = Document::createNew(8, 8);
    if (!doc) return;
    uint32_t base = doc->layer(0).id;
    for (auto &p : doc->raster(base)->px) p = opaque(100, 100, 100);
    // Clip layer above a half-covered base layer.
    uint32_t mid = doc->addRasterLayer(1, "shape", 0);
    Raster *m = doc->raster(mid);
    for (int i = 0; i < 32; i++) m->px[size_t(i)] = opaque(0, 0, 0);  // top half
    uint32_t clip = doc->addRasterLayer(2, "clip", 0);
    doc->layer(2).clipping = 1;
    for (auto &p : doc->raster(clip)->px) p = opaque(255, 0, 0);

    std::vector<PixelBGRA> c;
    Compositor::composite(*doc, Rect{0, 0, 8, 8}, c);
    CHECK_NEAR(chR(c[0]), 255, 1);       // clipped into the shape
    CHECK_NEAR(chR(c[63]), 100, 1);      // outside: base shows through

    // A set with Luminosity mode composites its content first.
    uint32_t set = doc->addLayerSet(3, "set", 0);
    doc->setBlend(3, BlendMode::Luminosity);
    uint32_t inner = doc->addRasterLayer(4, "inner", set);
    for (auto &p : doc->raster(inner)->px) p = opaque(50, 50, 50);
    Compositor::composite(*doc, Rect{0, 0, 8, 8}, c);
    CHECK_NEAR(chR(c[63]), 150, 1);
    CHECK(doc->depthOf(4) == 1);
}

static void testUndo()
{
    auto doc = Document::createNew(16, 16);
    if (!doc) return;
    UndoStack undo;
    uint32_t id = doc->layer(0).id;
    BrushSettings bs;
    bs.size = 4;
    StrokeEngine e;
    e.begin(doc->raster(id)->px, 16, 16, bs, false);
    e.addPoint(8, 8, 1);
    undo.push(std::make_unique<PixelCommand>(id, e.end()));
    CHECK(chA(doc->raster(id)->px[8 * 16 + 8]) == 255);
    undo.undo(*doc);
    CHECK(chA(doc->raster(id)->px[8 * 16 + 8]) == 0);
    undo.redo(*doc);
    CHECK(chA(doc->raster(id)->px[8 * 16 + 8]) == 255);

    // Delete and restore a layer, including its pixels.
    uint32_t extra = doc->addRasterLayer(1, "x", 0);
    undo.push(StructureCommand::inserted(1, extra));
    doc->raster(extra)->px[0] = opaque(1, 2, 3);
    undo.push(StructureCommand::removeNow(*doc, 1));
    CHECK(doc->layerCount() == 1);
    undo.undo(*doc);
    CHECK(doc->layerCount() == 2 && doc->raster(extra) && doc->raster(extra)->px[0] == opaque(1, 2, 3));
    undo.undo(*doc);
    CHECK(doc->layerCount() == 1);
    undo.redo(*doc);
    CHECK(doc->layerCount() == 2);

    // Reorder
    MoveCommand mv(1, 0, 0, 0);
    mv.redo(*doc);
    CHECK(doc->layer(0).id == extra);
    mv.undo(*doc);
    CHECK(doc->layer(1).id == extra);
}

// Load every .sai given on the command line, re-save and compare composites.
static void testSamples(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        std::string err;
        auto d = Document::load(argv[i], err);
        CHECK(d != nullptr);
        if (!d) {
            std::printf("  %s: %s\n", argv[i], err.c_str());
            continue;
        }
        std::vector<PixelBGRA> a, b;
        Compositor::composite(*d, Rect{0, 0, d->width(), d->height()}, a);
        std::string out = tempPath("potationsail_resave.sai");
        CHECK(d->save(out, err));
        auto r = Document::load(out, err);
        CHECK(r != nullptr);
        if (r) {
            Compositor::composite(*r, Rect{0, 0, r->width(), r->height()}, b);
            CHECK(a == b);
            std::printf("  sample %s: %dx%d, %zu layers ok\n", argv[i], d->width(), d->height(),
                        d->layerCount());
        }
        std::remove(out.c_str());
    }
}

// Writes a demo document: a gradient background, a Luminosity glow layer
// and a Multiply shadow inside a folder.  Used for manual app testing.
static int writeSample(const char *path)
{
    const int W = 640, H = 420;
    auto d = Document::createNew(W, H);
    if (!d) return 1;
    std::strncpy(d->layer(0).name, "Background", 256);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            d->raster(d->layer(0).id)->px[size_t(y) * W + x] =
                opaque(unsigned(20 + x * 60 / W), unsigned(30 + y * 50 / H), unsigned(90 + x * 80 / W));
    uint32_t set = d->addLayerSet(1, "Effects", 0);
    uint32_t shadow = d->addRasterLayer(2, "Shadow", set);
    d->setBlend(2, BlendMode::Multiply);
    uint32_t glow = d->addRasterLayer(3, "Glow", set);
    d->setBlend(3, BlendMode::Luminosity);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double dx = x - 380.0, dy = y - 200.0, r = std::sqrt(dx * dx + dy * dy);
            double k = std::max(0.0, 1.0 - r / 170.0);
            unsigned a = unsigned(k * k * 255);
            d->raster(glow)->px[size_t(y) * W + x] =
                packBGRA(a * 40 / 255, a * 150 / 255, a * 255 / 255, a);
            if (x > 60 && x < 260 && y > 120 && y < 320)
                d->raster(shadow)->px[size_t(y) * W + x] = opaque(120, 90, 160);
        }
    for (size_t i = 0; i < d->layerCount(); i++)
        if (Raster *r = d->raster(d->layer(i).id)) r->dirty = true;
    std::vector<PixelBGRA> c, white;
    Compositor::composite(*d, Rect{0, 0, W, H}, c);
    Compositor::flattenOver(c, Rect{0, 0, W, H}, W, packBGRA(255, 255, 255, 255), white);
    d->setThumbnail(white);
    std::string err;
    if (!d->save(path, err)) {
        std::printf("save failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--write-sample") return writeSample(argv[2]);
    testBlendIdentities();
    testLuminosity();
    testShadeAndLumiShade();
    testBinaryIgnoresOpacity();
    testFourCC();
    testStabilizerLevels();
    testStabilizerSmooths();
    testStabilizerOutputShape();
    testStabilizerSTail();
    testBrush();
    testDocumentRoundTrip();
    testSetsAndClipping();
    testUndo();
    testSamples(argc, argv);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
