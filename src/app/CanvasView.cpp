#include "CanvasView.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "MainWindow.h"

namespace app {

namespace {
constexpr double kMinZoom = 1.0 / 32, kMaxZoom = 32.0;

// A DIB section mirroring MainWindow::display(): StretchBlt from it has
// unambiguous top-left origin semantics for partial source rectangles.
struct Backing {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    uint32_t *bits = nullptr;
    int w = 0, h = 0;
    void release()
    {
        if (dc) {
            SelectObject(dc, old);
            DeleteObject(bmp);
            DeleteDC(dc);
        }
        dc = nullptr;
        bits = nullptr;
        w = h = 0;
    }
    bool ensure(int W, int H)
    {
        if (dc && w == W && h == H) return true;
        release();
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void *p = nullptr;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
        if (!bmp) return false;
        dc = CreateCompatibleDC(nullptr);
        old = HBITMAP(SelectObject(dc, bmp));
        bits = static_cast<uint32_t *>(p);
        w = W;
        h = H;
        return true;
    }
};
Backing g_backing;
}  // namespace

bool CanvasView::create(HWND parent)
{
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = L"PotCanvas";
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, L"PotCanvas", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100,
                            100, parent, nullptr, inst, this);
    return hwnd_ != nullptr;
}

LRESULT CALLBACK CanvasView::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
    auto *self = reinterpret_cast<CanvasView *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && self->hwnd_ == nullptr) self->hwnd_ = h;
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// View transform
// ---------------------------------------------------------------------------
void CanvasView::toCanvas(double sx, double sy, double &cx, double &cy) const
{
    cx = (sx - panX_) / zoom_;
    cy = (sy - panY_) / zoom_;
}

void CanvasView::clampPan()
{
    pot::Document *d = app_.doc();
    if (!d) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const double cw = d->width() * zoom_, ch = d->height() * zoom_;
    const double margin = 64;
    // Keep at least `margin` pixels of the canvas on screen.
    panX_ = std::min(std::max(panX_, margin - cw), double(rc.right) - margin);
    panY_ = std::min(std::max(panY_, margin - ch), double(rc.bottom) - margin);
}

void CanvasView::zoomBy(double factor, POINT a)
{
    double z = std::min(std::max(zoom_ * factor, kMinZoom), kMaxZoom);
    // Snap close to 100 % so pixel art stays crisp.
    if (std::abs(z - 1.0) < 0.02) z = 1.0;
    double cx, cy;
    toCanvas(a.x, a.y, cx, cy);
    zoom_ = z;
    panX_ = a.x - cx * zoom_;
    panY_ = a.y - cy * zoom_;
    clampPan();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CanvasView::zoomCentered(double factor)
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    zoomBy(factor, POINT{rc.right / 2, rc.bottom / 2});
}

void CanvasView::fit()
{
    pot::Document *d = app_.doc();
    if (!d) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    double avail_w = std::max(1L, rc.right - 32), avail_h = std::max(1L, rc.bottom - 32);
    zoom_ = std::min({avail_w / d->width(), avail_h / d->height(), 1.0});
    zoom_ = std::max(zoom_, kMinZoom);
    panX_ = std::floor((rc.right - d->width() * zoom_) / 2);
    panY_ = std::floor((rc.bottom - d->height() * zoom_) / 2);
    invalidateCanvas(pot::Rect{});
}

void CanvasView::actualSize()
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    zoomBy(1.0 / zoom_, POINT{rc.right / 2, rc.bottom / 2});
}

// Copy changed display pixels into the backing DIB and schedule a repaint.
void CanvasView::invalidateCanvas(const pot::Rect &req)
{
    pot::Document *d = app_.doc();
    if (!d) return;
    const int W = d->width(), H = d->height();
    if (!g_backing.ensure(W, H)) return;
    pot::Rect r = req.empty() ? pot::Rect{0, 0, W, H} : req.intersect(pot::Rect{0, 0, W, H});
    const auto &disp = app_.display();
    if (!r.empty() && disp.size() == size_t(W) * H) {
        GdiFlush();
        for (int y = r.y0; y < r.y1; y++)
            std::memcpy(g_backing.bits + size_t(y) * W + r.x0, disp.data() + size_t(y) * W + r.x0,
                        size_t(r.width()) * 4);
    }
    if (req.empty()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    RECT s{LONG(std::floor(panX_ + r.x0 * zoom_)) - 1, LONG(std::floor(panY_ + r.y0 * zoom_)) - 1,
           LONG(std::ceil(panX_ + r.x1 * zoom_)) + 1, LONG(std::ceil(panY_ + r.y1 * zoom_)) + 1};
    InvalidateRect(hwnd_, &s, FALSE);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void CanvasView::paint()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const RECT &u = ps.rcPaint;
    const int uw = u.right - u.left, uh = u.bottom - u.top;
    if (uw <= 0 || uh <= 0) {
        EndPaint(hwnd_, &ps);
        return;
    }
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, uw, uh);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    SetViewportOrgEx(mem, -u.left, -u.top, nullptr);

    HBRUSH bg = CreateSolidBrush(RGB(0x6b, 0x6b, 0x70));
    FillRect(mem, &u, bg);
    DeleteObject(bg);

    pot::Document *d = app_.doc();
    if (d && g_backing.dc) {
        const int W = d->width(), H = d->height();
        // Visible source rectangle (canvas pixels) for the update region.
        int sx0 = std::max(0, int(std::floor((u.left - panX_) / zoom_)));
        int sy0 = std::max(0, int(std::floor((u.top - panY_) / zoom_)));
        int sx1 = std::min(W, int(std::ceil((u.right - panX_) / zoom_)) + 1);
        int sy1 = std::min(H, int(std::ceil((u.bottom - panY_) / zoom_)) + 1);
        if (sx1 > sx0 && sy1 > sy0) {
            int dx0 = int(std::lround(panX_ + sx0 * zoom_)), dy0 = int(std::lround(panY_ + sy0 * zoom_));
            int dx1 = int(std::lround(panX_ + sx1 * zoom_)), dy1 = int(std::lround(panY_ + sy1 * zoom_));
            if (zoom_ < 1.0) {
                SetStretchBltMode(mem, HALFTONE);
                SetBrushOrgEx(mem, 0, 0, nullptr);
            } else {
                SetStretchBltMode(mem, COLORONCOLOR);
            }
            StretchBlt(mem, dx0, dy0, dx1 - dx0, dy1 - dy0, g_backing.dc, sx0, sy0, sx1 - sx0,
                       sy1 - sy0, SRCCOPY);
        }
        // Thin frame around the canvas.
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x30, 0x30, 0x34));
        HGDIOBJ op = SelectObject(mem, pen);
        HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, int(std::lround(panX_)) - 1, int(std::lround(panY_)) - 1,
                  int(std::lround(panX_ + W * zoom_)) + 1, int(std::lround(panY_ + H * zoom_)) + 1);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(pen);
    }

    SetViewportOrgEx(mem, 0, 0, nullptr);
    BitBlt(dc, u.left, u.top, uw, uh, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void CanvasView::inputSample(double x, double y, double pressure, bool /*penEraser*/)
{
    stab_.setLevel(app_.stabilizer());
    stab_.feed(pot::PenSample{x, y, pressure}, out_);
    flushStabilized();
}

void CanvasView::flushStabilized()
{
    for (const auto &s : out_) {
        if (!strokeOpen_ || s.pressure <= 0) continue;
        double cx, cy;
        toCanvas(s.x, s.y, cx, cy);
        app_.strokePoint(cx, cy, s.pressure);
    }
    out_.clear();
}

bool CanvasView::penToClient(const POINTER_PEN_INFO &pi, double &x, double &y)
{
    POINT org{0, 0};
    ClientToScreen(hwnd_, &org);
    const POINT px = pi.pointerInfo.ptPixelLocation;
    x = px.x - org.x;
    y = px.y - org.y;
    // Prefer the sub-pixel HIMETRIC position when it maps consistently.
    RECT dev{}, disp{};
    if (GetPointerDeviceRects(pi.pointerInfo.sourceDevice, &dev, &disp) && dev.right > dev.left &&
        dev.bottom > dev.top) {
        const POINT hm = pi.pointerInfo.ptHimetricLocation;
        double sx = disp.left + double(hm.x - dev.left) * (disp.right - disp.left) / (dev.right - dev.left);
        double sy = disp.top + double(hm.y - dev.top) * (disp.bottom - disp.top) / (dev.bottom - dev.top);
        if (std::abs(sx - px.x) < 2.0 && std::abs(sy - px.y) < 2.0) {
            x = sx - org.x;
            y = sy - org.y;
        }
    }
    return true;
}

// Returns false when the pointer is not a pen (let Windows promote it).
bool CanvasView::pointerUpdate(WPARAM wp, bool /*down*/, bool /*up*/)
{
    const UINT32 id = GET_POINTERID_WPARAM(wp);
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (!GetPointerType(id, &type) || type != PT_PEN) return false;

    POINTER_PEN_INFO cur{};
    if (!GetPointerPenInfo(id, &cur)) return false;
    UINT32 count = std::max<UINT32>(1, cur.pointerInfo.historyCount);
    std::vector<POINTER_PEN_INFO> hist(count);
    if (count == 1 || !GetPointerPenInfoHistory(id, &count, hist.data())) {
        hist[0] = cur;
        count = 1;
    }
    // History is newest first; replay oldest first.
    for (UINT32 i = count; i-- > 0;) {
        const POINTER_PEN_INFO &pi = hist[i];
        const bool inContact = (pi.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
        const bool eraser = (pi.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0;
        double x, y;
        penToClient(pi, x, y);
        if (inContact && !contact_) {
            contact_ = true;
            penEraser_ = eraser;
            SetFocus(hwnd_);
            stab_.reset();  // no streak from where the previous stroke ended
            strokeOpen_ = app_.beginStroke(eraser);
        }
        if (inContact) {
            double p = (pi.penMask & PEN_MASK_PRESSURE) ? std::max<UINT32>(pi.pressure, 1) / 1024.0 : 1.0;
            inputSample(x, y, std::min(p, 1.0), penEraser_);
        } else if (contact_) {
            inputSample(x, y, 0.0, penEraser_);  // pen-up packet: lets S-levels finish
            contact_ = false;
            if (strokeOpen_) app_.endStroke();
            strokeOpen_ = false;
        } else {
            inputSample(x, y, 0.0, eraser);  // hover, keeps numeric levels primed
        }
    }
    return true;
}

LRESULT CanvasView::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (app_.doc()) {
            clampPan();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;

    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
        if (!panning_ && pointerUpdate(wp, msg == WM_POINTERDOWN, msg == WM_POINTERUP)) return 0;
        break;
    case WM_POINTERCAPTURECHANGED:
        if (contact_) {
            contact_ = false;
            if (strokeOpen_) app_.endStroke();
            strokeOpen_ = false;
            stab_.reset();
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        SetFocus(hwnd_);
        SetCapture(hwnd_);
        const double x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (msg == WM_MBUTTONDOWN || spaceDown_) {
            panning_ = true;
            panStart_ = POINT{LONG(x), LONG(y)};
            panStartX_ = panX_;
            panStartY_ = panY_;
            SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
            return 0;
        }
        contact_ = true;
        stab_.reset();
        strokeOpen_ = app_.beginStroke(false);
        inputSample(x, y, 1.0, false);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const double x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (panning_) {
            panX_ = panStartX_ + (x - panStart_.x);
            panY_ = panStartY_ + (y - panStart_.y);
            clampPan();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (contact_ && (wp & MK_LBUTTON)) {
            inputSample(x, y, 1.0, false);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
        if (panning_) {
            panning_ = false;
        } else if (contact_) {
            inputSample(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0.0, false);
            contact_ = false;
            if (strokeOpen_) app_.endStroke();
            strokeOpen_ = false;
        }
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        panning_ = false;
        if (contact_) {
            contact_ = false;
            if (strokeOpen_) app_.endStroke();
            strokeOpen_ = false;
            stab_.reset();
        }
        return 0;

    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &p);
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        zoomBy(std::pow(1.25, delta / double(WHEEL_DELTA)), p);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_SPACE) {
            spaceDown_ = true;
            if (!contact_) SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
            return 0;
        }
        break;
    case WM_KEYUP:
        if (wp == VK_SPACE) {
            spaceDown_ = false;
            SetCursor(LoadCursor(nullptr, IDC_CROSS));
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, (spaceDown_ || panning_) ? IDC_SIZEALL : IDC_CROSS));
            return TRUE;
        }
        break;
    case WM_DESTROY:
        g_backing.release();
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace app
