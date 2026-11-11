#include "ToolPanel.h"

#include <commctrl.h>
#include <commdlg.h>

#include <cmath>
#include <string>

#include "MainWindow.h"
#include "Stabilizer.h"
#include "Text.h"

namespace app {

namespace {
enum : int { CTL_PEN = 1, CTL_ERASER, CTL_COLOR, CTL_SIZE, CTL_OPACITY, CTL_HARD, CTL_MIN,
             CTL_PRESSOP, CTL_STAB };

// Stabilizer combo entries in SAI's order: 0, 1..15, S-1..S-7.
int stabCodeAt(int index)
{
    if (index <= 15) return index;
    return 0x80 | (index - 15);
}
int stabIndexOf(int code)
{
    if (code & 0x80) return 15 + (code & 0x7F);
    return code;
}

// Brush size slider is logarithmic: 0..1000 -> 1..500 px.
int sizeToPos(double s) { return int(std::lround(std::log(s) / std::log(500.0) * 1000)); }
double posToSize(int p)
{
    double s = std::pow(500.0, p / 1000.0);
    return s < 10 ? std::round(s * 10) / 10 : std::round(s);
}

HWND makeLabel(HWND parent, const wchar_t *text)
{
    HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, WPARAM(uiFont()), TRUE);
    return h;
}
HWND makeSlider(HWND parent, int id, int lo, int hi)
{
    HWND h = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS,
                           0, 0, 10, 10, parent, HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, TBM_SETRANGEMIN, FALSE, lo);
    SendMessageW(h, TBM_SETRANGEMAX, TRUE, hi);
    return h;
}
HWND makeButton(HWND parent, int id, const wchar_t *text, DWORD style)
{
    HWND h = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 10, 10,
                           parent, HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, WPARAM(uiFont()), TRUE);
    return h;
}
}  // namespace

int ToolPanel::preferredWidth() { return scalePx(190); }

bool ToolPanel::create(HWND parent)
{
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"PotTools";
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, L"PotTools", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10,
                            10, parent, nullptr, inst, this);
    if (!hwnd_) return false;

    pen_ = makeButton(hwnd_, CTL_PEN, L"Pen  (N)", BS_AUTORADIOBUTTON | WS_GROUP);
    eraser_ = makeButton(hwnd_, CTL_ERASER, L"Eraser  (E)", BS_AUTORADIOBUTTON);
    color_ = makeButton(hwnd_, CTL_COLOR, L"", BS_OWNERDRAW);
    sizeLabel_ = makeLabel(hwnd_, L"");
    size_ = makeSlider(hwnd_, CTL_SIZE, 0, 1000);
    minLabel_ = makeLabel(hwnd_, L"");
    minSize_ = makeSlider(hwnd_, CTL_MIN, 0, 100);
    opLabel_ = makeLabel(hwnd_, L"");
    opacity_ = makeSlider(hwnd_, CTL_OPACITY, 0, 100);
    hardLabel_ = makeLabel(hwnd_, L"");
    hard_ = makeSlider(hwnd_, CTL_HARD, 0, 100);
    pressOp_ = makeButton(hwnd_, CTL_PRESSOP, L"Pressure → opacity", BS_AUTOCHECKBOX);
    stabLabel_ = makeLabel(hwnd_, L"Stabilizer");
    stab_ = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                          0, 0, 10, 300, hwnd_, HMENU(INT_PTR(CTL_STAB)), inst, nullptr);
    SendMessageW(stab_, WM_SETFONT, WPARAM(uiFont()), TRUE);
    for (int i = 0; i <= 15; i++) SendMessageW(stab_, CB_ADDSTRING, 0, LPARAM(std::to_wstring(i).c_str()));
    for (int k = 1; k <= 7; k++)
        SendMessageW(stab_, CB_ADDSTRING, 0, LPARAM((L"S-" + std::to_wstring(k)).c_str()));
    help_ = makeLabel(hwnd_,
                      L"Wheel: zoom\nSpace / middle drag: pan\n[ ]: brush size\nCtrl+Z / Ctrl+Y: undo / redo");
    layout();
    sync();
    return true;
}

LRESULT CALLBACK ToolPanel::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
    auto *self = reinterpret_cast<ToolPanel *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && !self->hwnd_) self->hwnd_ = h;
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

void ToolPanel::layout()
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int pad = scalePx(8), w = rc.right - 2 * pad, row = scalePx(22), sl = scalePx(26);
    int y = pad;
    MoveWindow(pen_, pad, y, w / 2, row, TRUE);
    MoveWindow(eraser_, pad + w / 2, y, w / 2, row, TRUE);
    y += row + pad;
    MoveWindow(color_, pad, y, w, scalePx(36), TRUE);
    y += scalePx(36) + pad;
    auto pair = [&](HWND label, HWND ctl) {
        MoveWindow(label, pad, y, w, row - scalePx(4), TRUE);
        y += row - scalePx(4);
        MoveWindow(ctl, pad - scalePx(4), y, w + scalePx(8), sl, TRUE);
        y += sl + scalePx(4);
    };
    pair(sizeLabel_, size_);
    pair(minLabel_, minSize_);
    pair(opLabel_, opacity_);
    pair(hardLabel_, hard_);
    MoveWindow(pressOp_, pad, y, w, row, TRUE);
    y += row + pad;
    MoveWindow(stabLabel_, pad, y, w, row - scalePx(4), TRUE);
    y += row - scalePx(4);
    MoveWindow(stab_, pad, y, w, scalePx(400), TRUE);
    y += scalePx(30) + pad * 2;
    MoveWindow(help_, pad, y, w, scalePx(80), TRUE);
}

void ToolPanel::updateLabels()
{
    const auto &b = app_.brush();
    wchar_t buf[64];
    swprintf(buf, 64, L"Size: %g px", b.size);
    SetWindowTextW(sizeLabel_, buf);
    swprintf(buf, 64, L"Min size: %d %%", int(std::lround(b.minSize * 100)));
    SetWindowTextW(minLabel_, buf);
    swprintf(buf, 64, L"Opacity: %d %%", b.opacity);
    SetWindowTextW(opLabel_, buf);
    swprintf(buf, 64, L"Hardness: %d %%", int(std::lround(b.hardness * 100)));
    SetWindowTextW(hardLabel_, buf);
}

void ToolPanel::sync()
{
    const auto &b = app_.brush();
    SendMessageW(pen_, BM_SETCHECK, app_.tool() == Tool::Pen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(eraser_, BM_SETCHECK, app_.tool() == Tool::Eraser ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(size_, TBM_SETPOS, TRUE, sizeToPos(b.size));
    SendMessageW(minSize_, TBM_SETPOS, TRUE, LPARAM(std::lround(b.minSize * 100)));
    SendMessageW(opacity_, TBM_SETPOS, TRUE, b.opacity);
    SendMessageW(hard_, TBM_SETPOS, TRUE, LPARAM(std::lround(b.hardness * 100)));
    SendMessageW(pressOp_, BM_SETCHECK, b.pressureOpacity ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(stab_, CB_SETCURSEL, stabIndexOf(app_.stabilizer()), 0);
    EnableWindow(color_, app_.tool() == Tool::Pen);
    InvalidateRect(color_, nullptr, TRUE);
    updateLabels();
}

void ToolPanel::onSlider(HWND s)
{
    auto &b = app_.brush();
    int pos = int(SendMessageW(s, TBM_GETPOS, 0, 0));
    if (s == size_) b.size = posToSize(pos);
    else if (s == minSize_) b.minSize = pos / 100.0;
    else if (s == opacity_) b.opacity = pos;
    else if (s == hard_) b.hardness = pos / 100.0;
    updateLabels();
}

void ToolPanel::pickColor()
{
    static COLORREF custom[16] = {};
    auto &c = app_.brush().color;
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = app_.hwnd();
    cc.rgbResult = RGB(c.r, c.g, c.b);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        c = pot::Color{GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult)};
        InvalidateRect(color_, nullptr, TRUE);
    }
}

LRESULT ToolPanel::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        layout();
        return 0;
    case WM_HSCROLL:
        if (lp) onSlider(HWND(lp));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case CTL_PEN: app_.setTool(Tool::Pen); return 0;
        case CTL_ERASER: app_.setTool(Tool::Eraser); return 0;
        case CTL_COLOR: pickColor(); return 0;
        case CTL_PRESSOP:
            app_.brush().pressureOpacity = SendMessageW(pressOp_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            return 0;
        case CTL_STAB:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int i = int(SendMessageW(stab_, CB_GETCURSEL, 0, 0));
                if (i >= 0) app_.setStabilizer(stabCodeAt(i));
                SetFocus(app_.hwnd());
            }
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        auto *di = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
        if (di->CtlID != CTL_COLOR) break;
        const auto &c = app_.brush().color;
        FillRect(di->hDC, &di->rcItem, GetSysColorBrush(COLOR_BTNFACE));
        RECT r = di->rcItem;
        InflateRect(&r, -2, -2);
        DrawEdge(di->hDC, &r, (di->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        InflateRect(&r, -4, -4);
        HBRUSH br = CreateSolidBrush(IsWindowEnabled(color_) ? RGB(c.r, c.g, c.b) : GetSysColor(COLOR_BTNSHADOW));
        FillRect(di->hDC, &r, br);
        DeleteObject(br);
        if (di->itemState & ODS_FOCUS) DrawFocusRect(di->hDC, &di->rcItem);
        return TRUE;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace app
