// Left panel: tool, colour, brush size / opacity / hardness, stabilizer.
#pragma once
#include <windows.h>

namespace app {

class MainWindow;

class ToolPanel {
public:
    explicit ToolPanel(MainWindow &app) : app_(app) {}
    bool create(HWND parent);
    HWND hwnd() const { return hwnd_; }
    void sync();  // re-read every value from MainWindow
    static int preferredWidth();

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void layout();
    void onSlider(HWND s);
    void pickColor();
    void updateLabels();

    MainWindow &app_;
    HWND hwnd_ = nullptr;
    HWND pen_ = nullptr, eraser_ = nullptr, color_ = nullptr;
    HWND sizeLabel_ = nullptr, size_ = nullptr;
    HWND opLabel_ = nullptr, opacity_ = nullptr;
    HWND hardLabel_ = nullptr, hard_ = nullptr;
    HWND minLabel_ = nullptr, minSize_ = nullptr;
    HWND pressOp_ = nullptr;
    HWND stabLabel_ = nullptr, stab_ = nullptr;
    HWND help_ = nullptr;
    HBRUSH swatch_ = nullptr;
};

}  // namespace app
