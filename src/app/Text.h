// String conversion and the two small modal prompts the app needs.
#pragma once
#include <windows.h>

#include <string>

namespace app {

std::wstring widen(const std::string &utf8);
std::string narrow(const std::wstring &w);
// SAI stores layer names in the system ANSI code page (CP932 on Japanese
// Windows).  These convert between that and UTF-16.
std::wstring fromAnsi(const char *s);
std::string toAnsi(const std::wstring &w);

HFONT uiFont();
int scalePx(int px);  // 96-dpi pixels -> device pixels

// Modal single line text prompt.  Returns false when cancelled.
bool promptText(HWND owner, const wchar_t *title, const wchar_t *label, std::wstring &value);
// Modal width x height prompt.
bool promptSize(HWND owner, const wchar_t *title, int &width, int &height);

}  // namespace app
