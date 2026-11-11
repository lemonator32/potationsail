// Scalar emulation of PaintTool SAI's MMX blend kernels.
//
// Every SAI kernel has the same frame (see FUN_0049d690 in sai.exe):
//
//   for each pixel whose source alpha byte is non-zero:
//     S[i] = paddsw(psrlw(s[i]*op, 1), psrlw(s[i]*op, 9))   source, 15-bit, x opacity
//     D[i] = paddsw(d[i] << 7, d[i])                         destination, 15-bit
//     ... mode specific lane arithmetic ...
//     out[i] = packuswb(psrlw(R[i], 7))                      back to 8 bit
//
// Lanes are B, G, R, A (lane 3 = alpha).  "mul15(a, b)" below is SAI's
// pmulhw followed by psllw 1, i.e. (a*b) >> 15 with the low bit cleared.
// 1.0 is 0x7FFF.  All values are premultiplied.
#include "Blend.h"

#include <cstring>

namespace pot {
namespace {

constexpr int16_t ONE = 0x7FFF;

inline int16_t sat16(int v)
{
    return v > 32767 ? int16_t(32767) : v < -32768 ? int16_t(-32768) : int16_t(v);
}
inline int16_t adds(int16_t a, int16_t b) { return sat16(int(a) + int(b)); }       // paddsw
inline int16_t addw(int16_t a, int16_t b) { return int16_t(uint16_t(a) + uint16_t(b)); }  // paddw
inline int16_t subw(int16_t a, int16_t b) { return int16_t(uint16_t(a) - uint16_t(b)); }  // psubw
inline int16_t subus(int16_t a, int16_t b)  // psubusw
{
    int r = int(uint16_t(a)) - int(uint16_t(b));
    return int16_t(r < 0 ? 0 : r);
}
inline int16_t mul15(int16_t a, int16_t b)  // pmulhw + psllw 1
{
    int16_t hi = int16_t((int32_t(a) * int32_t(b)) >> 16);
    return int16_t(uint16_t(hi) << 1);
}
inline uint8_t pack(int16_t v)  // psrlw 7 + packuswb
{
    unsigned u = unsigned(uint16_t(v)) >> 7;
    return uint8_t(u > 255 ? 255 : u);
}

struct Lanes {
    int16_t v[4];
};

inline Lanes loadSrc(PixelBGRA s, int op)
{
    Lanes L;
    for (int i = 0; i < 4; i++) {
        uint16_t m = uint16_t(((s >> (8 * i)) & 0xFF) * unsigned(op));  // pmullw
        L.v[i] = adds(int16_t(m >> 1), int16_t(m >> 9));
    }
    return L;
}
inline Lanes loadDst(PixelBGRA d)
{
    Lanes L;
    for (int i = 0; i < 4; i++) {
        int16_t b = int16_t((d >> (8 * i)) & 0xFF);
        L.v[i] = adds(int16_t(b << 7), b);
    }
    return L;
}
inline PixelBGRA store(const Lanes &R)
{
    return packBGRA(pack(R.v[0]), pack(R.v[1]), pack(R.v[2]), pack(R.v[3]));
}

// out = D * (1 - Sa) : the "backdrop survives" term shared by several modes.
inline Lanes keepDst(const Lanes &D, int16_t Sa)
{
    Lanes T;
    for (int i = 0; i < 4; i++) T.v[i] = mul15(D.v[i], subw(ONE, Sa));
    return T;
}

// --- 0x49d690 Normal / Pass Through -------------------------------------
PixelBGRA kNormal(const Lanes &D, const Lanes &S, int16_t)
{
    Lanes T = keepDst(D, S.v[3]);
    for (int i = 0; i < 4; i++) T.v[i] = adds(T.v[i], S.v[i]);
    return store(T);
}

// --- 0x49d760 Multiply ----------------------------------------------------
//   c = D(1-Sa) + S(D + 1 - Da)      a = Da(1-Sa) + Sa
PixelBGRA kMultiply(const Lanes &D, const Lanes &S, int16_t flag)
{
    Lanes T = keepDst(D, S.v[3]);
    int16_t A = int16_t(D.v[3] | flag);
    int16_t inv = subw(ONE, A);
    Lanes R;
    for (int i = 0; i < 3; i++) R.v[i] = adds(T.v[i], mul15(S.v[i], addw(D.v[i], inv)));
    R.v[3] = adds(T.v[3], mul15(S.v[3], addw(A, inv)));
    return store(R);
}

// --- 0x49d850 Screen ------------------------------------------------------
//   c = D(1-Sa) + Sa - (Sa-S)(1-D)  (= S + D - S*D for opaque pixels)
PixelBGRA kScreen(const Lanes &D, const Lanes &S, int16_t)
{
    Lanes T1 = keepDst(D, S.v[3]);
    Lanes T;
    for (int i = 0; i < 3; i++) T.v[i] = mul15(subw(S.v[3], S.v[i]), subw(ONE, D.v[i]));
    T.v[3] = mul15(S.v[3], ONE);
    Lanes R;
    for (int i = 0; i < 3; i++) R.v[i] = adds(T1.v[i], subw(T.v[3], T.v[i]));
    R.v[3] = adds(T1.v[3], T.v[3]);
    return store(R);
}

// --- 0x49d950 Overlay -----------------------------------------------------
//   D > Da/2 : c = D(1-Sa) + Sa*Da - 2(Sa-S)(Da-D)
//   else     : c = D(1-Sa) + 2*S*D
//   then     : c += S(1 - a'), where a' = Da(1-Sa) + Sa*Da
PixelBGRA kOverlay(const Lanes &D, const Lanes &S, int16_t flag)
{
    Lanes T1 = keepDst(D, S.v[3]);
    int16_t A = int16_t(D.v[3] | flag);
    int16_t half = int16_t(uint16_t(A) >> 1);
    bool mask[4];
    Lanes first, second;
    for (int i = 0; i < 3; i++) {
        mask[i] = half < D.v[i];  // pcmpgtw (signed)
        first.v[i] = mask[i] ? subw(S.v[3], S.v[i]) : S.v[i];
        second.v[i] = mask[i] ? int16_t(uint16_t(subw(A, D.v[i])) << 1) : addw(D.v[i], D.v[i]);
    }
    mask[3] = false;
    first.v[3] = S.v[3];
    second.v[3] = A;
    Lanes T;
    for (int i = 0; i < 4; i++) T.v[i] = mul15(first.v[i], second.v[i]);
    Lanes U;
    for (int i = 0; i < 4; i++) {
        int16_t r = mask[i] ? subw(T.v[3], T.v[i]) : T.v[i];
        U.v[i] = adds(T1.v[i], r);
    }
    int16_t A2 = int16_t(U.v[3] | flag);
    Lanes R;
    for (int i = 0; i < 4; i++) R.v[i] = adds(U.v[i], mul15(subw(ONE, A2), S.v[i]));
    return store(R);
}

// Shared tail of Luminosity / Shade / Lumi&Shade: add the part of the source
// that lies over transparent backdrop, S * (1 - Da).
inline PixelBGRA addUncovered(Lanes C, const Lanes &S, int16_t flag)
{
    int16_t A2 = int16_t(C.v[3] | flag);
    for (int i = 0; i < 4; i++) C.v[i] = adds(C.v[i], mul15(subw(ONE, A2), S.v[i]));
    return store(C);
}

// --- 0x49dab0 Luminosity ('add ') ----------------------------------------
//   c = min(D + S*Da, Da) + S(1-Da)        a = Da + Sa(1-Da)
// Note: there is no D(1-Sa) term (the kernel computes it and discards it):
// adding light never darkens the backdrop, and over transparent pixels the
// layer looks exactly like Normal.
PixelBGRA kLuminosity(const Lanes &D, const Lanes &S, int16_t flag)
{
    int16_t A = int16_t(D.v[3] | flag);
    Lanes C;
    for (int i = 0; i < 4; i++) {
        int16_t dv = i == 3 ? A : D.v[i];
        int16_t w = adds(dv, mul15(S.v[i], A));
        C.v[i] = w < A ? w : A;  // pcmpgtw select
    }
    return addUncovered(C, S, flag);
}

// --- 0x49dbd0 Shade ('sub ') ---------------------------------------------
//   c = max(D - (Sa - S)*Da, 0) + S(1-Da)  a = Da + Sa(1-Da)
PixelBGRA kShade(const Lanes &D, const Lanes &S, int16_t flag)
{
    int16_t A = int16_t(D.v[3] | flag);
    Lanes SA;
    for (int i = 0; i < 4; i++) SA.v[i] = mul15(S.v[i], A);
    Lanes C;
    for (int i = 0; i < 3; i++) C.v[i] = subus(D.v[i], subw(SA.v[3], SA.v[i]));
    C.v[3] = A;
    return addUncovered(C, S, flag);
}

// --- 0x49dcf0 Lumi & Shade ('adsb') --------------------------------------
//   c = clamp(D + Da*(2S - Sa), 0, Da) + S(1-Da)
PixelBGRA kLumiShade(const Lanes &D, const Lanes &S, int16_t flag)
{
    int16_t A = int16_t(D.v[3] | flag);
    Lanes SA;
    for (int i = 0; i < 4; i++) SA.v[i] = mul15(S.v[i], A);
    int16_t h = int16_t(uint16_t(SA.v[3]) >> 1);
    Lanes C;
    for (int i = 0; i < 4; i++) {
        int16_t dv = i == 3 ? A : D.v[i];
        int16_t w = adds(dv, int16_t(uint16_t(subw(SA.v[i], h)) << 1));
        w = w < A ? w : A;
        C.v[i] = w > 0 ? w : int16_t(0);
    }
    return addUncovered(C, S, flag);
}

// --- 0x49de30 Binary Color ('cbin') --------------------------------------
// Same as Normal but the source is taken at full strength: the kernel never
// reads the opacity argument (S = s << 7).
PixelBGRA kBinary(const Lanes &D, PixelBGRA s)
{
    Lanes S;
    for (int i = 0; i < 4; i++) S.v[i] = int16_t(((s >> (8 * i)) & 0xFF) << 7);
    Lanes T = keepDst(D, S.v[3]);
    for (int i = 0; i < 4; i++) T.v[i] = adds(T.v[i], S.v[i]);
    return store(T);
}

}  // namespace

PixelBGRA blendPixel(BlendMode mode, PixelBGRA dst, PixelBGRA src, int op, bool destOpaque)
{
    if ((src & 0xFF000000u) == 0) return dst;  // every SAI kernel skips these
    if (op < 0) op = 0;
    if (op > 256) op = 256;
    int16_t flag = destOpaque ? ONE : int16_t(0);
    Lanes D = loadDst(dst);
    if (mode == BlendMode::BinaryColor) return kBinary(D, src);
    Lanes S = loadSrc(src, op);
    switch (mode) {
    case BlendMode::Multiply: return kMultiply(D, S, flag);
    case BlendMode::Screen: return kScreen(D, S, flag);
    case BlendMode::Overlay: return kOverlay(D, S, flag);
    case BlendMode::Luminosity: return kLuminosity(D, S, flag);
    case BlendMode::Shade: return kShade(D, S, flag);
    case BlendMode::LumiShade: return kLumiShade(D, S, flag);
    case BlendMode::PassThrough:
    case BlendMode::Normal:
    default: return kNormal(D, S, flag);
    }
}

void blendSpan(BlendMode mode, PixelBGRA *dst, const PixelBGRA *src, int count, int op,
               bool destOpaque)
{
    for (int i = 0; i < count; i++) dst[i] = blendPixel(mode, dst[i], src[i], op, destOpaque);
}

int opacityTo256(int percent)
{
    if (percent <= 0) return 0;
    if (percent >= 100) return 256;
    return (percent * 256 + 50) / 100;
}

namespace {
struct ModeInfo {
    BlendMode mode;
    uint32_t fourcc;
    const char *name;
};
constexpr uint32_t fcc(char a, char b, char c, char d)
{
    return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
           (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d));
}
constexpr ModeInfo kModes[kBlendModeCount] = {
    {BlendMode::PassThrough, fcc('p', 'a', 's', 's'), "Pass Through"},
    {BlendMode::Normal, fcc('n', 'o', 'r', 'm'), "Normal"},
    {BlendMode::Multiply, fcc('m', 'u', 'l', ' '), "Multiply"},
    {BlendMode::Screen, fcc('s', 'c', 'r', 'n'), "Screen"},
    {BlendMode::Overlay, fcc('o', 'v', 'e', 'r'), "Overlay"},
    {BlendMode::Luminosity, fcc('a', 'd', 'd', ' '), "Luminosity"},
    {BlendMode::Shade, fcc('s', 'u', 'b', ' '), "Shade"},
    {BlendMode::LumiShade, fcc('a', 'd', 's', 'b'), "Lumi & Shade"},
    {BlendMode::BinaryColor, fcc('c', 'b', 'i', 'n'), "Binary Color"},
};
}  // namespace

BlendMode blendFromFourCC(uint32_t f)
{
    for (const auto &m : kModes)
        if (m.fourcc == f) return m.mode;
    return BlendMode::Normal;
}
uint32_t fourCCFromBlend(BlendMode mode) { return kModes[int(mode) % kBlendModeCount].fourcc; }
const char *blendName(BlendMode mode) { return kModes[int(mode) % kBlendModeCount].name; }

}  // namespace pot
