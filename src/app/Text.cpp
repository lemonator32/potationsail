#include "Text.h"

#include <cwchar>
#include <vector>

namespace app {

static std::wstring toWide(UINT cp, const char *s, int len)
{
    if (!s || !len) return {};
    int n = MultiByteToWideChar(cp, 0, s, len, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(cp, 0, s, len, w.data(), n);
    if (len < 0 && !w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static std::string fromWide(UINT cp, const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(cp, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(cp, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string &u) { return toWide(CP_UTF8, u.c_str(), int(u.size())); }
std::string narrow(const std::wstring &w) { return fromWide(CP_UTF8, w); }
std::wstring fromAnsi(const char *s) { return toWide(CP_ACP, s, -1); }
std::string toAnsi(const std::wstring &w) { return fromWide(CP_ACP, w); }

HFONT uiFont()
{
    static HFONT font = [] {
        NONCLIENTMETRICSW m{};
        m.cbSize = sizeof(m);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(m), &m, 0);
        return CreateFontIndirectW(&m.lfMessageFont);
    }();
    return font;
}

int scalePx(int px)
{
    static int dpi = [] {
        HDC dc = GetDC(nullptr);
        int d = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        return d ? d : 96;
    }();
    return MulDiv(px, dpi, 96);
}

// ---------------------------------------------------------------------------
// A tiny modal prompt built from plain controls (no resource script needed).
// ---------------------------------------------------------------------------
namespace {

struct Field {
    const wchar_t *label;
    std::wstring value;
    bool numeric;
    HWND edit = nullptr;
};

struct PromptState {
    std::vector<Field> *fields;
    bool done = false, ok = false;
};

constexpr int kIdOk = 1, kIdCancel = 2, kIdEditBase = 100;

LRESULT CALLBACK promptProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    auto *st = reinterpret_cast<PromptState *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == kIdOk || LOWORD(wp) == kIdCancel) {
            st->ok = LOWORD(wp) == kIdOk;
            if (st->ok)
                for (auto &f : *st->fields) {
                    wchar_t buf[512];
                    GetWindowTextW(f.edit, buf, 512);
                    f.value = buf;
                }
            st->done = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        st->done = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool runPrompt(HWND owner, const wchar_t *title, std::vector<Field> &fields)
{
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = promptProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = L"PotPrompt";
        RegisterClassW(&wc);
        registered = true;
    }
    PromptState st{&fields};
    const int pad = scalePx(10), rowH = scalePx(24), labelW = scalePx(90), editW = scalePx(220);
    const int btnW = scalePx(80), btnH = scalePx(26);
    RECT rc{0, 0, pad * 3 + labelW + editW, pad * 3 + int(fields.size()) * (rowH + pad) + btnH};
    AdjustWindowRect(&rc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE);
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PotPrompt", title,
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               (orc.left + orc.right - w) / 2, (orc.top + orc.bottom - hgt) / 2, w,
                               hgt, owner, nullptr, inst, &st);
    if (!dlg) return false;
    HFONT font = uiFont();
    int y = pad;
    for (size_t i = 0; i < fields.size(); i++) {
        HWND lab = CreateWindowW(L"STATIC", fields[i].label, WS_CHILD | WS_VISIBLE, pad,
                                 y + scalePx(4), labelW, rowH, dlg, nullptr, inst, nullptr);
        SendMessageW(lab, WM_SETFONT, WPARAM(font), TRUE);
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
        if (fields[i].numeric) style |= ES_NUMBER;
        fields[i].edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", fields[i].value.c_str(), style,
                                         pad * 2 + labelW, y, editW, rowH, dlg,
                                         HMENU(INT_PTR(kIdEditBase + i)), inst, nullptr);
        SendMessageW(fields[i].edit, WM_SETFONT, WPARAM(font), TRUE);
        y += rowH + pad;
    }
    int bx = pad * 3 + labelW + editW - pad - 2 * btnW - pad;
    HWND ok = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            bx, y + pad, btnW, btnH, dlg, HMENU(INT_PTR(kIdOk)), inst, nullptr);
    HWND cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, bx + btnW + pad,
                                y + pad, btnW, btnH, dlg, HMENU(INT_PTR(kIdCancel)), inst, nullptr);
    SendMessageW(ok, WM_SETFONT, WPARAM(font), TRUE);
    SendMessageW(cancel, WM_SETFONT, WPARAM(font), TRUE);

    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(fields[0].edit);
    SendMessageW(fields[0].edit, EM_SETSEL, 0, -1);
    MSG m{};
    while (!st.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
            st.done = true;
            break;
        }
        if (!IsDialogMessageW(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (m.message == WM_QUIT) PostQuitMessage(int(m.wParam));
    EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    SetActiveWindow(owner);
    return st.ok;
}

}  // namespace

bool promptText(HWND owner, const wchar_t *title, const wchar_t *label, std::wstring &value)
{
    std::vector<Field> f{{label, value, false}};
    if (!runPrompt(owner, title, f)) return false;
    value = f[0].value;
    return true;
}

bool promptSize(HWND owner, const wchar_t *title, int &width, int &height)
{
    std::vector<Field> f{{L"Width (px)", std::to_wstring(width), true},
                         {L"Height (px)", std::to_wstring(height), true}};
    if (!runPrompt(owner, title, f)) return false;
    width = _wtoi(f[0].value.c_str());
    height = _wtoi(f[1].value.c_str());
    return true;
}

}  // namespace app
