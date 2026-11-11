// The painting surface: shows the composite, handles zoom / pan and feeds
// pen or mouse input through the stabilizer into the stroke engine.
#pragma once
#include <windows.h>

#include <vector>

#include "Pixel.h"
#include "Stabilizer.h"

namespace app {

class MainWindow;

class CanvasView {
public:
    explicit CanvasView(MainWindow &app) : app_(app) {}
    bool create(HWND parent);
    HWND hwnd() const { return hwnd_; }

    void invalidateCanvas(const pot::Rect &r);  // canvas coordinates; empty = all
    void zoomBy(double factor, POINT anchorClient);
    void zoomCentered(double factor);
    void fit();
    void actualSize();
    void documentChanged() { fit(); }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void paint();
    void clampPan();
    void toCanvas(double sx, double sy, double &cx, double &cy) const;

    // Input -> stabilizer -> stroke.
    void inputSample(double clientX, double clientY, double pressure, bool penEraser);
    void flushStabilized();
    bool pointerUpdate(WPARAM wp, bool down, bool up);
    bool penToClient(const POINTER_PEN_INFO &pi, double &x, double &y);

    MainWindow &app_;
    HWND hwnd_ = nullptr;
    double zoom_ = 1.0;
    double panX_ = 0, panY_ = 0;  // client position of canvas pixel (0,0)

    pot::Stabilizer stab_;
    std::vector<pot::PenSample> out_;
    bool contact_ = false;     // pen / button is down on the canvas
    bool strokeOpen_ = false;  // a stroke has been started in the document
    bool penEraser_ = false;
    bool panning_ = false;
    POINT panStart_{};
    double panStartX_ = 0, panStartY_ = 0;
    bool spaceDown_ = false;
};

}  // namespace app
