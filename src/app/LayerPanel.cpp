#include "LayerPanel.h"

#include <commctrl.h>

#include <string>

#include "Blend.h"
#include "MainWindow.h"
#include "Text.h"

namespace app {

namespace {
enum : int { CTL_BLEND = 1, CTL_OPACITY, CTL_PRESERVE, CTL_CLIP, CTL_LIST, CTL_NEW, CTL_SET, CTL_DEL,
             CTL_UP, CTL_DOWN };

// Modes offered for raster layers and for sets (sets add Pass Through).
const pot::BlendMode kModes[] = {
    pot::BlendMode::PassThrough, pot::BlendMode::Normal,     pot::BlendMode::Multiply,
    pot::BlendMode::Screen,      pot::BlendMode::Overlay,    pot::BlendMode::Luminosity,
    pot::BlendMode::Shade,       pot::BlendMode::LumiShade,  pot::BlendMode::BinaryColor,
};

// The list box eats its own clicks; a subclass forwards eye-box hits.
LRESULT CALLBACK listSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    if (msg == WM_LBUTTONDOWN) {
        const int x = short(LOWORD(lp)), y = short(HIWORD(lp));
        LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
        if (!HIWORD(hit) && x <= scalePx(26)) {
            SendMessageW(HWND(ref), WM_APP, WPARAM(LOWORD(hit)), LPARAM(x));
            return 0;
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

HWND child(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    HWND h = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                           HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, WPARAM(uiFont()), TRUE);
    return h;
}
}  // namespace

int LayerPanel::preferredWidth() { return scalePx(240); }

bool LayerPanel::create(HWND parent)
{
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"PotLayers";
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, L"PotLayers", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10,
                            10, parent, nullptr, inst, this);
    if (!hwnd_) return false;

    blend_ = child(hwnd_, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, CTL_BLEND);
    opLabel_ = child(hwnd_, L"STATIC", L"Opacity", 0, 0);
    opacity_ = child(hwnd_, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, CTL_OPACITY);
    SendMessageW(opacity_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    preserve_ = child(hwnd_, L"BUTTON", L"Protect opacity", WS_TABSTOP | BS_AUTOCHECKBOX, CTL_PRESERVE);
    clip_ = child(hwnd_, L"BUTTON", L"Clipping group", WS_TABSTOP | BS_AUTOCHECKBOX, CTL_CLIP);
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY |
                                LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                            0, 0, 10, 10, hwnd_, HMENU(INT_PTR(CTL_LIST)), inst, nullptr);
    SendMessageW(list_, WM_SETFONT, WPARAM(uiFont()), TRUE);
    SetWindowSubclass(list_, listSubclass, 1, DWORD_PTR(hwnd_));
    btnNew_ = child(hwnd_, L"BUTTON", L"New", WS_TABSTOP, CTL_NEW);
    btnSet_ = child(hwnd_, L"BUTTON", L"Folder", WS_TABSTOP, CTL_SET);
    btnDel_ = child(hwnd_, L"BUTTON", L"Delete", WS_TABSTOP, CTL_DEL);
    btnUp_ = child(hwnd_, L"BUTTON", L"▲", WS_TABSTOP, CTL_UP);
    btnDown_ = child(hwnd_, L"BUTTON", L"▼", WS_TABSTOP, CTL_DOWN);
    layout();
    return true;
}

LRESULT CALLBACK LayerPanel::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
    auto *self = reinterpret_cast<LayerPanel *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && !self->hwnd_) self->hwnd_ = h;
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

void LayerPanel::layout()
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int pad = scalePx(8), w = rc.right - 2 * pad, row = scalePx(22);
    int y = pad;
    MoveWindow(blend_, pad, y, w, scalePx(300), TRUE);
    y += scalePx(28);
    MoveWindow(opLabel_, pad, y, w, row - scalePx(4), TRUE);
    y += row - scalePx(4);
    MoveWindow(opacity_, pad - scalePx(4), y, w + scalePx(8), scalePx(26), TRUE);
    y += scalePx(28);
    MoveWindow(preserve_, pad, y, w / 2 + scalePx(10), row, TRUE);
    MoveWindow(clip_, pad + w / 2 + scalePx(10), y, w / 2 - scalePx(10), row, TRUE);
    y += row + scalePx(4);
    const int btnH = scalePx(26);
    const int listBottom = rc.bottom - pad - btnH - scalePx(4);
    MoveWindow(list_, pad, y, w, listBottom - y > 0 ? listBottom - y : 0, TRUE);
    SendMessageW(list_, LB_SETITEMHEIGHT, 0, scalePx(38));
    int by = rc.bottom - pad - btnH, small = scalePx(28), big = (w - 2 * small - 4 * scalePx(3)) / 3;
    int x = pad;
    for (HWND b : {btnNew_, btnSet_, btnDel_}) {
        MoveWindow(b, x, by, big, btnH, TRUE);
        x += big + scalePx(3);
    }
    MoveWindow(btnUp_, x, by, small, btnH, TRUE);
    MoveWindow(btnDown_, x + small + scalePx(3), by, small, btnH, TRUE);
}

long LayerPanel::selectedIndex() const
{
    pot::Document *d = app_.doc();
    return d ? d->indexOf(d->selectedId()) : -1;
}

void LayerPanel::rebuild()
{
    pot::Document *d = app_.doc();
    syncing_ = true;
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    rows_.clear();
    if (d) {
        // Display order: depth-first, children directly under their folder,
        // top of the stack first (SAI's layer panel order).
        std::vector<size_t> order;
        auto visit = [&](auto &&self, uint32_t parent, int depth) -> void {
            if (depth > 32) return;
            for (size_t i = d->layerCount(); i-- > 0;) {
                if (d->parentOf(i) != parent) continue;
                order.push_back(i);
                const sai_layer &l = d->layer(i);
                if (l.type == SAI_LAYER_SET) self(self, l.id, depth + 1);
            }
        };
        visit(visit, 0, 0);
        // Layers whose parent set is missing would be invisible; list them too.
        for (size_t i = d->layerCount(); i-- > 0;) {
            bool listed = false;
            for (size_t k : order) listed |= k == i;
            if (!listed) order.push_back(i);
        }
        long sel = selectedIndex();
        for (size_t idx : order) {
            int row = int(SendMessageW(list_, LB_ADDSTRING, 0, LPARAM(L"")));
            rows_.push_back(idx);
            if (long(idx) == sel) SendMessageW(list_, LB_SETCURSEL, row, 0);
        }
    }
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);
    syncing_ = false;
    syncProps();
}

void LayerPanel::syncProps()
{
    pot::Document *d = app_.doc();
    long i = selectedIndex();
    syncing_ = true;
    const bool has = d && i >= 0;
    for (HWND h : {blend_, opacity_, preserve_, clip_, btnDel_, btnUp_, btnDown_}) EnableWindow(h, has);
    SendMessageW(blend_, CB_RESETCONTENT, 0, 0);
    if (has) {
        const sai_layer &l = d->layer(size_t(i));
        const bool isSet = l.type == SAI_LAYER_SET;
        pot::BlendMode cur = d->blend(size_t(i));
        for (pot::BlendMode m : kModes) {
            if (m == pot::BlendMode::PassThrough && !isSet) continue;
            int k = int(SendMessageW(blend_, CB_ADDSTRING, 0, LPARAM(widen(pot::blendName(m)).c_str())));
            SendMessageW(blend_, CB_SETITEMDATA, k, LPARAM(m));
            if (m == cur) SendMessageW(blend_, CB_SETCURSEL, k, 0);
        }
        SendMessageW(opacity_, TBM_SETPOS, TRUE, l.opacity);
        wchar_t buf[32];
        swprintf(buf, 32, L"Opacity: %u %%", unsigned(l.opacity));
        SetWindowTextW(opLabel_, buf);
        SendMessageW(preserve_, BM_SETCHECK, l.preserve_opacity ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(clip_, BM_SETCHECK, l.clipping ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(preserve_, !isSet);
        EnableWindow(clip_, !isSet);
        EnableWindow(btnDel_, d->layerCount() > 1);
    }
    syncing_ = false;
}

void LayerPanel::commitProps(const pot::LayerProps &after, bool merge)
{
    pot::Document *d = app_.doc();
    long i = selectedIndex();
    if (!d || i < 0) return;
    sai_layer &l = d->layer(size_t(i));
    pot::LayerProps before = merge ? opBefore_ : pot::LayerProps::of(l);
    after.applyTo(l);
    app_.pushCommand(std::make_unique<pot::PropsCommand>(l.id, before, after));
    d->setModified();
    app_.refresh();
    InvalidateRect(list_, nullptr, FALSE);
}

void LayerPanel::drawItem(const DRAWITEMSTRUCT &di)
{
    pot::Document *d = app_.doc();
    if (!d || di.itemID == UINT(-1) || di.itemID >= rows_.size()) return;
    const size_t idx = rows_[di.itemID];
    if (idx >= d->layerCount()) return;
    const sai_layer &l = d->layer(idx);
    HDC dc = di.hDC;
    RECT r = di.rcItem;
    const bool sel = (di.itemState & ODS_SELECTED) != 0;
    FillRect(dc, &r, GetSysColorBrush(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    HGDIOBJ oldFont = SelectObject(dc, uiFont());

    // Eye box
    const int box = scalePx(16), pad = scalePx(6);
    RECT eye{r.left + pad, r.top + (r.bottom - r.top - box) / 2, r.left + pad + box,
             r.top + (r.bottom - r.top + box) / 2};
    FrameRect(dc, &eye, GetSysColorBrush(COLOR_BTNSHADOW));
    if (l.visible) {
        RECT in = eye;
        InflateRect(&in, -scalePx(4), -scalePx(4));
        FillRect(dc, &in, GetSysColorBrush(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    }
    int x = eye.right + pad + d->depthOf(idx) * scalePx(14);
    if (l.clipping) {
        RECT c{x, r.top + scalePx(6), x + scalePx(3), r.bottom - scalePx(6)};
        FillRect(dc, &c, GetSysColorBrush(COLOR_HOTLIGHT));
        x += scalePx(8);
    }
    std::wstring name = fromAnsi(l.name);
    if (l.type == SAI_LAYER_SET) name = (l.set_open || !l.has_set_open ? L"▾ " : L"▸ ") + name;
    else if (l.type != SAI_LAYER_RASTER) name += L"  (not editable)";
    std::wstring info = widen(pot::blendName(d->blend(idx)));
    info += L"  " + std::to_wstring(l.opacity) + L"%";
    RECT tr{x, r.top + scalePx(2), r.right - pad, r.top + (r.bottom - r.top) / 2 + scalePx(2)};
    DrawTextW(dc, name.c_str(), -1, &tr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | DT_VCENTER);
    RECT ir{x, tr.bottom - scalePx(2), r.right - pad, r.bottom - scalePx(2)};
    SetTextColor(dc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT));
    DrawTextW(dc, info.c_str(), -1, &ir, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | DT_VCENTER);
    SelectObject(dc, oldFont);
    if (di.itemState & ODS_FOCUS) DrawFocusRect(dc, &r);
}

// Click inside a list row: the eye box toggles visibility.
void LayerPanel::clicked(int row, int x)
{
    pot::Document *d = app_.doc();
    if (!d || row < 0 || size_t(row) >= rows_.size()) return;
    const int eyeRight = scalePx(6) + scalePx(16) + scalePx(4);
    if (x > eyeRight) return;
    sai_layer &l = d->layer(rows_[size_t(row)]);
    pot::LayerProps before = pot::LayerProps::of(l), after = before;
    after.visible = l.visible ? 0 : 1;
    after.applyTo(l);
    app_.pushCommand(std::make_unique<pot::PropsCommand>(l.id, before, after));
    d->setModified();
    app_.refresh();
    InvalidateRect(list_, nullptr, FALSE);
}

LRESULT LayerPanel::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    pot::Document *d = app_.doc();
    switch (msg) {
    case WM_SIZE:
        layout();
        return 0;
    case WM_MEASUREITEM:
        reinterpret_cast<MEASUREITEMSTRUCT *>(lp)->itemHeight = UINT(scalePx(38));
        return TRUE;
    case WM_DRAWITEM:
        drawItem(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
        return TRUE;
    case WM_HSCROLL:
        if (HWND(lp) == opacity_ && d && !syncing_) {
            long i = selectedIndex();
            if (i < 0) return 0;
            sai_layer &l = d->layer(size_t(i));
            if (!opDrag_ || opLayer_ != l.id) {
                opDrag_ = true;
                opLayer_ = l.id;
                opBefore_ = pot::LayerProps::of(l);
            }
            int pos = int(SendMessageW(opacity_, TBM_GETPOS, 0, 0));
            wchar_t buf[32];
            swprintf(buf, 32, L"Opacity: %d %%", pos);
            SetWindowTextW(opLabel_, buf);
            const int code = LOWORD(wp);
            if (code == TB_ENDTRACK || code == TB_THUMBPOSITION) {
                pot::LayerProps after = pot::LayerProps::of(l);
                after.opacity = uint8_t(pos);
                if (after.opacity != opBefore_.opacity) commitProps(after, true);
                opDrag_ = false;
            } else {
                l.opacity = uint8_t(pos);  // live preview while dragging
                app_.refresh();
                InvalidateRect(list_, nullptr, FALSE);
            }
        }
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (syncing_) return 0;
        if (id == CTL_LIST && code == LBN_SELCHANGE && d) {
            int row = int(SendMessageW(list_, LB_GETCURSEL, 0, 0));
            if (row >= 0 && size_t(row) < rows_.size()) {
                d->setSelectedId(d->layer(rows_[size_t(row)]).id);
                syncProps();
            }
            return 0;
        }
        if (id == CTL_LIST && code == LBN_DBLCLK) {
            app_.command(ID_LAYER_RENAME);
            return 0;
        }
        if (id == CTL_BLEND && code == CBN_SELCHANGE && d) {
            int k = int(SendMessageW(blend_, CB_GETCURSEL, 0, 0));
            long i = selectedIndex();
            if (k >= 0 && i >= 0) {
                auto m = pot::BlendMode(SendMessageW(blend_, CB_GETITEMDATA, k, 0));
                pot::LayerProps after = pot::LayerProps::of(d->layer(size_t(i)));
                after.blend = pot::fourCCFromBlend(m);
                commitProps(after, false);
            }
            SetFocus(app_.hwnd());
            return 0;
        }
        if ((id == CTL_PRESERVE || id == CTL_CLIP) && d) {
            long i = selectedIndex();
            if (i < 0) return 0;
            pot::LayerProps after = pot::LayerProps::of(d->layer(size_t(i)));
            HWND b = id == CTL_PRESERVE ? preserve_ : clip_;
            uint8_t v = SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED;
            (id == CTL_PRESERVE ? after.preserve : after.clipping) = v;
            commitProps(after, false);
            return 0;
        }
        switch (id) {
        case CTL_NEW: app_.command(ID_LAYER_NEW); return 0;
        case CTL_SET: app_.command(ID_LAYER_NEWSET); return 0;
        case CTL_DEL: app_.command(ID_LAYER_DELETE); return 0;
        case CTL_UP: app_.command(ID_LAYER_UP); return 0;
        case CTL_DOWN: app_.command(ID_LAYER_DOWN); return 0;
        }
        break;
    }
    case WM_APP:  // eye box clicked (from the list subclass)
        clicked(int(wp), int(lp));
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace app
