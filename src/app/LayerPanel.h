// Right panel: blend mode / opacity / flags of the selected layer, and the
// layer list (top layer first, indented by folder depth).
#pragma once
#include <windows.h>

#include <vector>

#include "Undo.h"

namespace app {

class MainWindow;

class LayerPanel {
public:
    explicit LayerPanel(MainWindow &app) : app_(app) {}
    bool create(HWND parent);
    HWND hwnd() const { return hwnd_; }
    void rebuild();  // list contents + property controls
    static int preferredWidth();

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void layout();
    void syncProps();
    long selectedIndex() const;  // document index of the selection
    void commitProps(const pot::LayerProps &after, bool merge);
    void drawItem(const DRAWITEMSTRUCT &di);
    void clicked(int row, int x);

    MainWindow &app_;
    HWND hwnd_ = nullptr;
    HWND blend_ = nullptr, opLabel_ = nullptr, opacity_ = nullptr;
    HWND preserve_ = nullptr, clip_ = nullptr, list_ = nullptr;
    HWND btnNew_ = nullptr, btnSet_ = nullptr, btnDel_ = nullptr, btnUp_ = nullptr, btnDown_ = nullptr;
    std::vector<size_t> rows_;  // list row -> document index
    bool syncing_ = false;

    // Opacity drags are merged into one undo step.
    bool opDrag_ = false;
    pot::LayerProps opBefore_;
    uint32_t opLayer_ = 0;
};

}  // namespace app
