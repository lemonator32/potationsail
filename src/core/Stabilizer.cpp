#include "Stabilizer.h"

#include <cmath>

namespace pot {

namespace {
constexpr double kPi = 3.14159265358979323846;

// FUN_00437cf0: wrap an angle difference into (-pi, pi].
double angleDelta(double from, double to)
{
    double d = to - from;
    if (d > kPi) return d - 2 * kPi;
    if (d < -kPi) d += 2 * kPi;
    return d;
}
}  // namespace

bool Stabilizer::isValidLevel(int code)
{
    return (code >= 0 && code <= 15) || (code >= 0x81 && code <= 0x87);
}

// FUN_00438c60: n = (code & 0x7f) + (S ? 1 : 0), clamped to 1..16.
int Stabilizer::windowFor(int code)
{
    int n = (code & 0x7F) + ((code & 0x80) ? 1 : 0);
    if (n < 1) n = 1;
    if (n > 16) n = 16;
    return n;
}

void Stabilizer::setLevel(int code)
{
    if (!isValidLevel(code)) code = 0;
    if (code != code_) {
        code_ = code;
        reset();  // SAI clears every history when the level changes
    }
}

void Stabilizer::reset()
{
    px_.clear(); py_.clear(); pp_.clear();
    sx_.clear(); sy_.clear(); sp_.clear();
    spline_.clear();
    wasDown_ = false;
    havePrev_ = false;
}

// FUN_00437d30: average the new value with up to n-1 previous entries of a
// 16 entry ring buffer.  Mode 1 stores the result (recursive), mode 0 the input.
double Stabilizer::Average::apply(double v, int n, bool recursive)
{
    double sum = v;
    int used = 1;
    for (int i = 0; used < n && i < count; i++, used++) sum += hist[(head - 1 - i) & 15];
    double avg = sum / used;
    hist[head] = recursive ? avg : v;
    head = (head + 1) & 15;
    if (++count > 16) count = 16;
    return avg;
}

// FUN_00438010: keep the last three packets, emit the cubic segment from p0 to
// p1 at t = 1/4, 1/2, 3/4, 1.  Control points come from FUN_00437e60, which
// places them on the p0->p2 chord, scaled by the two adjacent segment lengths.
void Stabilizer::Spline::push(const PenSample &s, std::vector<PenSample> &out)
{
    if (!started) {
        started = true;
        p0 = p1 = p2 = s;
        outX = s.x;
        outY = s.y;
    }
    // Outgoing control point of the point that becomes p0.
    double c0x = outX, c0y = outY;
    p0 = p1;
    p1 = p2;
    p2 = s;

    double d0 = std::hypot(p0.x - p1.x, p0.y - p1.y);
    double d1 = std::hypot(p2.x - p1.x, p2.y - p1.y);
    double inX = p1.x, inY = p1.y;
    outX = p1.x;
    outY = p1.y;
    if (d0 != 0.0 && d1 != 0.0) {
        // Inferred constant (the ftol'd expressions are not recoverable):
        // 1/3 of the length-weighted chord, which reduces to Catmull-Rom for
        // evenly spaced packets.
        double tx = p2.x - p0.x, ty = p2.y - p0.y, sum = d0 + d1;
        inX = p1.x - tx * (d0 / sum) / 3.0;
        inY = p1.y - ty * (d0 / sum) / 3.0;
        outX = p1.x + tx * (d1 / sum) / 3.0;
        outY = p1.y + ty * (d1 / sum) / 3.0;
    }
    for (int k = 1; k <= 4; k++) {
        double t = k * 0.25, u = 1.0 - t;
        double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
        PenSample q;
        q.x = b0 * p0.x + b1 * c0x + b2 * inX + b3 * p1.x;
        q.y = b0 * p0.y + b1 * c0y + b2 * inY + b3 * p1.y;
        q.pressure = p0.pressure + (p1.pressure - p0.pressure) * t;  // linear, as SAI
        out.push_back(q);
    }
}

// Pen-up tail of the S levels: 16 steps that walk from the lagging filtered
// position to where the pen actually left, turning gradually from the stroke
// direction toward the target while the pressure fades to zero.
void Stabilizer::emitTail(const PenSample &raw, const PenSample &f, std::vector<PenSample> &out)
{
    double dist = std::hypot(raw.x - f.x, raw.y - f.y);
    if (dist <= 0.0) return;
    double heading = havePrev_ ? std::atan2(f.y - prevFy_, f.x - prevFx_) : 0.0;
    if (!havePrev_ || (f.x == prevFx_ && f.y == prevFy_))
        heading = std::atan2(raw.y - f.y, raw.x - f.x);
    double target = std::atan2(raw.y - f.y, raw.x - f.x);
    // SAI only steers toward the pen when it is more than 4 px away (1024/256).
    double turn = dist > 4.0 ? angleDelta(heading, target) / 16.0 : 0.0;
    double step = dist / 16.0;
    double x = f.x, y = f.y, p = f.pressure, dp = f.pressure / 16.0;
    double a = heading;
    for (int i = 0; i < 16; i++) {
        a += turn;
        x += std::cos(a) * step;
        y += std::sin(a) * step;
        p -= dp;
        out.push_back(PenSample{x, y, p > 0 ? p : 0});
    }
}

void Stabilizer::feed(const PenSample &raw, std::vector<PenSample> &out)
{
    const int n = windowFor(code_);
    const bool sMode = (code_ & 0x80) != 0;
    const bool down = raw.pressure > 0.0;

    // Pen state: 0 hover, 1 just pressed, 2 dragging, 3 just released.
    int state = 2;
    if (sMode) state = down ? (wasDown_ ? 2 : 1) : (wasDown_ ? 3 : 0);
    wasDown_ = down;

    PenSample f = raw;
    f.pressure = pp_.apply(raw.pressure, n, false);
    if (sMode) f.pressure = sp_.apply(f.pressure, n, true);

    if (state == 1) {  // pen-down: forget the position history
        px_.clear(); py_.clear(); sx_.clear(); sy_.clear();
        spline_.clear();
        havePrev_ = false;
    }
    if (state != 0) {
        if (!sMode) {
            if (code_ != 1) {
                f.x = px_.apply(raw.x, n, false);
                f.y = py_.apply(raw.y, n, false);
            }
        } else {
            f.x = sx_.apply(px_.apply(raw.x, n, false), n, true);
            f.y = sy_.apply(py_.apply(raw.y, n, false), n, true);
        }
    }

    if (n < 2 || state == 0) {
        out.push_back(f);
    } else {
        spline_.push(f, out);
        if (state == 3) emitTail(raw, f, out);
    }

    if (state == 3) {  // pen-up: the pressure history starts over next stroke
        pp_.clear();
        sp_.clear();
    }
    prevFx_ = f.x;
    prevFy_ = f.y;
    havePrev_ = true;
}

}  // namespace pot
