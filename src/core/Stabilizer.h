// Pen stabilizer modelled on PaintTool SAI 1.2.6 (tablet packet handler
// FUN_00438c60 and helpers FUN_00437d30 / FUN_00438010 in sai.exe).
//
// SAI smooths raw pen packets in *screen* space before the brush sees them:
//
//   level code   0..15      "numeric" levels: moving average over n = level
//                           packets (FIR), applied even while hovering
//                0x81..0x87 S-1..S-7: n = k + 1, a moving average followed by
//                           a second, recursive average (IIR), reset at every
//                           pen-down, plus a curved "catch-up" tail at pen-up
//
// When n >= 2 every filtered packet is turned into four points on a cubic
// Bezier through the last three packets (one packet of latency).
// docs/stabilizer.md lists what was read from the binary and what is inferred.
#pragma once
#include <cstdint>
#include <vector>

namespace pot {

struct PenSample {
    double x = 0, y = 0;   // screen pixels (sub-pixel)
    double pressure = 0;   // 0..1, 0 = not touching
};

class Stabilizer {
public:
    // Codes as stored by SAI: 0..15, or 0x80|k for S-k (k = 1..7).
    static constexpr int S(int k) { return 0x80 | k; }
    static bool isValidLevel(int code);
    static int windowFor(int code);  // n, 1..16

    void setLevel(int code);
    int level() const { return code_; }
    void reset();

    // Feed one raw packet (contact or hover).  Smoothed points are appended
    // to `out`; it may receive 0, 1, 4 or 21 points.
    void feed(const PenSample &raw, std::vector<PenSample> &out);

private:
    // SAI's ring buffer (16 entries) used for every smoothed quantity.
    struct Average {
        int head = 0, count = 0;
        double hist[16] = {};
        void clear() { head = count = 0; }
        // recursive = true stores the averaged value (IIR) instead of the input.
        double apply(double v, int n, bool recursive);
    };
    struct Spline {
        bool started = false;
        PenSample p0, p1, p2;
        double outX = 0, outY = 0;  // outgoing control point of p1
        void clear() { started = false; }
        void push(const PenSample &s, std::vector<PenSample> &out);
    };

    void emitTail(const PenSample &raw, const PenSample &filtered, std::vector<PenSample> &out);

    int code_ = 0;
    bool wasDown_ = false;
    Average px_, py_, pp_;     // first stage (FIR)
    Average sx_, sy_, sp_;     // second stage (IIR, S levels only)
    Spline spline_;
    double prevFx_ = 0, prevFy_ = 0;  // previous filtered position
    bool havePrev_ = false;
};

}  // namespace pot
