#include "MainWindow.h"

#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "CanvasView.h"
#include "Compositor.h"
#include "LayerPanel.h"
#include "Text.h"
#include "ToolPanel.h"

namespace app {

namespace {
constexpr wchar_t kAppName[] = L"potationsail";

// Light/dark checkerboard shown under transparent pixels (SAI style).
inline pot::PixelBGRA checker(int x, int y)
{
    return ((x >> 3) ^ (y >> 3)) & 1 ? pot::packBGRA(0xCC, 0xCC, 0xCC, 255)
                                     : pot::packBGRA(0xFF, 0xFF, 0xFF, 255);
}
}  // namespace

MainWindow::MainWindow()
{
    brush_.size = 6;
    brush_.minSize = 0.15;
    brush_.hardness = 0.9;
    eraserSettings_ = brush_;
    eraserSettings_.size = 20;
    eraserSettings_.eraser = true;
}

MainWindow::~MainWindow() = default;

bool MainWindow::create(HINSTANCE inst, int show)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"PotMain";
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, L"PotMain", kAppName,
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                            scalePx(1280), scalePx(820), nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    buildMenu();

    canvas_ = std::make_unique<CanvasView>(*this);
    layers_ = std::make_unique<LayerPanel>(*this);
    tools_ = std::make_unique<ToolPanel>(*this);
    if (!canvas_->create(hwnd_) || !layers_->create(hwnd_) || !tools_->create(hwnd_)) return false;

    ACCEL acc[] = {
        {FCONTROL | FVIRTKEY, 'N', ID_FILE_NEW},
        {FCONTROL | FVIRTKEY, 'O', ID_FILE_OPEN},
        {FCONTROL | FVIRTKEY, 'S', ID_FILE_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_FILE_SAVEAS},
        {FCONTROL | FVIRTKEY, 'Z', ID_EDIT_UNDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_EDIT_REDO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'Z', ID_EDIT_REDO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'N', ID_LAYER_NEW},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_VIEW_ZOOMIN},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_VIEW_ZOOMOUT},
        {FCONTROL | FVIRTKEY, '0', ID_VIEW_FIT},
        {FCONTROL | FVIRTKEY, '1', ID_VIEW_ACTUAL},
        {FVIRTKEY, 'N', ID_TOOL_PEN},
        {FVIRTKEY, 'E', ID_TOOL_ERASER},
        {FVIRTKEY, VK_OEM_6, ID_TOOL_BIGGER},   // ]
        {FVIRTKEY, VK_OEM_4, ID_TOOL_SMALLER},  // [
        {FVIRTKEY, VK_DELETE, ID_LAYER_CLEAR},
    };
    accel_ = CreateAcceleratorTableW(acc, int(sizeof(acc) / sizeof(acc[0])));

    setDocument(pot::Document::createNew(1280, 960), L"");
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);
    canvas_->fit();
    return true;
}

void MainWindow::buildMenu()
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(), layer = CreatePopupMenu();
    HMENU view = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_NEW, L"&New...\tCtrl+N");
    AppendMenuW(file, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_FILE_SAVEAS, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"E&xit");
    AppendMenuW(edit, MF_STRING, ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, ID_EDIT_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(layer, MF_STRING, ID_LAYER_NEW, L"&New Layer\tCtrl+Shift+N");
    AppendMenuW(layer, MF_STRING, ID_LAYER_NEWSET, L"New Layer &Folder");
    AppendMenuW(layer, MF_STRING, ID_LAYER_RENAME, L"&Rename...");
    AppendMenuW(layer, MF_STRING, ID_LAYER_CLEAR, L"&Clear\tDel");
    AppendMenuW(layer, MF_STRING, ID_LAYER_DELETE, L"&Delete");
    AppendMenuW(layer, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(layer, MF_STRING, ID_LAYER_UP, L"Move &Up");
    AppendMenuW(layer, MF_STRING, ID_LAYER_DOWN, L"Move Do&wn");
    AppendMenuW(view, MF_STRING, ID_VIEW_ZOOMIN, L"Zoom &In\tCtrl++");
    AppendMenuW(view, MF_STRING, ID_VIEW_ZOOMOUT, L"Zoom &Out\tCtrl+-");
    AppendMenuW(view, MF_STRING, ID_VIEW_FIT, L"&Fit to Window\tCtrl+0");
    AppendMenuW(view, MF_STRING, ID_VIEW_ACTUAL, L"&Actual Pixels\tCtrl+1");
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&About potationsail");
    AppendMenuW(bar, MF_POPUP, UINT_PTR(file), L"&File");
    AppendMenuW(bar, MF_POPUP, UINT_PTR(edit), L"&Edit");
    AppendMenuW(bar, MF_POPUP, UINT_PTR(layer), L"&Layer");
    AppendMenuW(bar, MF_POPUP, UINT_PTR(view), L"&View");
    AppendMenuW(bar, MF_POPUP, UINT_PTR(help), L"&Help");
    SetMenu(hwnd_, bar);
}

void MainWindow::layout()
{
    if (!canvas_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int tw = ToolPanel::preferredWidth(), lw = LayerPanel::preferredWidth();
    MoveWindow(tools_->hwnd(), 0, 0, tw, rc.bottom, TRUE);
    MoveWindow(layers_->hwnd(), rc.right - lw, 0, lw, rc.bottom, TRUE);
    MoveWindow(canvas_->hwnd(), tw, 0, std::max(1L, rc.right - tw - lw), rc.bottom, TRUE);
}

LRESULT CALLBACK MainWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
    auto *self = reinterpret_cast<MainWindow *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && !self->hwnd_) self->hwnd_ = h;
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto *mm = reinterpret_cast<MINMAXINFO *>(lp);
        mm->ptMinTrackSize = POINT{scalePx(720), scalePx(560)};
        return 0;
    }
    case WM_COMMAND:
        command(LOWORD(wp));
        return 0;
    case WM_INITMENUPOPUP: {
        HMENU m = HMENU(wp);
        EnableMenuItem(m, ID_EDIT_UNDO, undo_.canUndo() ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, ID_EDIT_REDO, undo_.canRedo() ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, ID_LAYER_DELETE, doc_ && doc_->layerCount() > 1 ? MF_ENABLED : MF_GRAYED);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = HDROP(wp);
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(drop, 0, path, MAX_PATH)) openFile(path);
        DragFinish(drop);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && canvas_) SetFocus(canvas_->hwnd());
        return 0;
    case WM_CLOSE:
        if (confirmDiscard()) DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------
void MainWindow::setDocument(std::unique_ptr<pot::Document> d, const std::wstring &path)
{
    if (!d) return;
    if (stroke_.active()) stroke_.end();
    doc_ = std::move(d);
    path_ = path;
    undo_.clear();
    composite_.assign(size_t(doc_->width()) * doc_->height(), 0);
    display_.assign(composite_.size(), 0);
    refresh();
    layers_->rebuild();
    canvas_->documentChanged();
    updateTitle();
}

void MainWindow::updateTitle()
{
    std::wstring t;
    if (doc_ && doc_->modified()) t += L"* ";
    std::wstring name = path_.empty() ? L"Untitled" : path_.substr(path_.find_last_of(L"\\/") + 1);
    t += name;
    if (doc_) t += L"  (" + std::to_wstring(doc_->width()) + L" × " + std::to_wstring(doc_->height()) + L")";
    t += L" - ";
    t += kAppName;
    SetWindowTextW(hwnd_, t.c_str());
}

bool MainWindow::confirmDiscard()
{
    if (!doc_ || !doc_->modified()) return true;
    int r = MessageBoxW(hwnd_, L"Save changes to the current document?", kAppName,
                        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return fileSave(false);
    return true;
}

void MainWindow::fileNew()
{
    if (!confirmDiscard()) return;
    int w = doc_ ? doc_->width() : 1280, h = doc_ ? doc_->height() : 960;
    if (!promptSize(hwnd_, L"New Canvas", w, h)) return;
    auto d = pot::Document::createNew(w, h);
    if (!d) {
        MessageBoxW(hwnd_, L"Width and height must be between 1 and 10000 pixels.", kAppName,
                    MB_ICONWARNING);
        return;
    }
    setDocument(std::move(d), L"");
}

void MainWindow::openFile(const std::wstring &path)
{
    if (!confirmDiscard()) return;
    std::string err;
    auto d = pot::Document::load(narrow(path), err);
    if (!d) {
        std::wstring msg = L"Could not open\n" + path + L"\n\n" + widen(err);
        MessageBoxW(hwnd_, msg.c_str(), kAppName, MB_ICONERROR);
        return;
    }
    setDocument(std::move(d), path);
}

void MainWindow::fileOpen()
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = hwnd_;
    of.lpstrFilter = L"PaintTool SAI document (*.sai)\0*.sai\0All files\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&of)) openFile(file);
}

bool MainWindow::fileSave(bool saveAs)
{
    if (!doc_) return false;
    std::wstring path = path_;
    if (saveAs || path.empty()) {
        wchar_t file[MAX_PATH] = L"";
        if (!path_.empty()) wcsncpy(file, path_.c_str(), MAX_PATH - 1);
        else wcscpy(file, L"Untitled.sai");
        OPENFILENAMEW of{};
        of.lStructSize = sizeof(of);
        of.hwndOwner = hwnd_;
        of.lpstrFilter = L"PaintTool SAI document (*.sai)\0*.sai\0";
        of.lpstrFile = file;
        of.nMaxFile = MAX_PATH;
        of.lpstrDefExt = L"sai";
        of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&of)) return false;
        path = file;
    }
    // Thumbnail = full composite over white, as SAI writes it.
    std::vector<pot::PixelBGRA> white;
    const pot::Rect all{0, 0, doc_->width(), doc_->height()};
    pot::Compositor::flattenOver(composite_, all, doc_->width(), pot::packBGRA(255, 255, 255, 255), white);
    doc_->setThumbnail(white);
    std::string err;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    bool ok = doc_->save(narrow(path), err);
    SetCursor(old);
    if (!ok) {
        std::wstring msg = L"Could not save\n" + path + L"\n\n" + widen(err);
        MessageBoxW(hwnd_, msg.c_str(), kAppName, MB_ICONERROR);
        return false;
    }
    path_ = path;
    updateTitle();
    return true;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void MainWindow::refresh(pot::Rect r)
{
    if (!doc_) return;
    const int W = doc_->width(), H = doc_->height();
    const pot::Rect all{0, 0, W, H};
    pot::Rect area = r.empty() ? all : r.intersect(all);
    if (area.empty()) return;
    pot::Compositor::composite(*doc_, area, composite_);
    for (int y = area.y0; y < area.y1; y++)
        for (int x = area.x0; x < area.x1; x++) {
            size_t k = size_t(y) * W + x;
            pot::PixelBGRA p = composite_[k], bg = checker(x, y);
            unsigned inv = 255 - pot::chA(p);
            auto ch = [inv](unsigned c, unsigned b) {
                unsigned v = c + (b * inv + 127) / 255;
                return v > 255 ? 255u : v;
            };
            display_[k] = pot::packBGRA(ch(pot::chB(p), pot::chB(bg)), ch(pot::chG(p), pot::chG(bg)),
                                        ch(pot::chR(p), pot::chR(bg)), 255);
        }
    canvas_->invalidateCanvas(r.empty() ? pot::Rect{} : area);
    updateTitle();
}

void MainWindow::layersChanged()
{
    layers_->rebuild();
    refresh();
}

void MainWindow::pushCommand(std::unique_ptr<pot::Command> c)
{
    undo_.push(std::move(c));
    updateTitle();
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void MainWindow::setTool(Tool t)
{
    if (t == tool_) return;
    std::swap(brush_, eraserSettings_);  // each tool remembers its own settings
    brush_.color = eraserSettings_.color;
    tool_ = t;
    brush_.eraser = t == Tool::Eraser;
    tools_->sync();
}

bool MainWindow::beginStroke(bool penEraser)
{
    if (!doc_) return false;
    long i = doc_->indexOf(doc_->selectedId());
    if (i < 0) return false;
    const sai_layer &l = doc_->layer(size_t(i));
    pot::Raster *r = doc_->raster(l.id);
    if (!r || l.type != SAI_LAYER_RASTER) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    pot::BrushSettings s = brush_;
    if (penEraser && tool_ != Tool::Eraser) {
        s = eraserSettings_;  // the stylus' eraser end
        s.eraser = true;
    }
    stroke_.begin(r->px, doc_->width(), doc_->height(), s, l.preserve_opacity != 0);
    strokeLayer_ = l.id;
    return true;
}

void MainWindow::strokePoint(double x, double y, double pressure)
{
    if (!stroke_.active()) return;
    pot::Rect dirty = stroke_.addPoint(x, y, pressure);
    if (!dirty.empty()) refresh(dirty);
}

void MainWindow::endStroke()
{
    if (!stroke_.active()) return;
    pot::StrokeResult res = stroke_.end();
    if (res.rect.empty()) return;
    if (pot::Raster *r = doc_->raster(strokeLayer_)) r->dirty = true;
    doc_->setModified();
    pushCommand(std::make_unique<pot::PixelCommand>(strokeLayer_, std::move(res)));
}

void MainWindow::adjustBrushSize(double f)
{
    brush_.size = std::min(500.0, std::max(1.0, std::round(brush_.size * f * 10) / 10));
    tools_->sync();
}

// ---------------------------------------------------------------------------
// Layer commands
// ---------------------------------------------------------------------------
void MainWindow::newLayer(bool set)
{
    if (!doc_) return;
    long sel = doc_->indexOf(doc_->selectedId());
    size_t index = sel < 0 ? doc_->layerCount() : size_t(sel) + 1;
    uint32_t parent = 0;
    if (sel >= 0) {
        const sai_layer &s = doc_->layer(size_t(sel));
        // New layers go above the selection, inside the selected folder.
        parent = s.type == SAI_LAYER_SET ? s.id : doc_->parentOf(size_t(sel));
    }
    uint32_t id = set ? doc_->addLayerSet(index, "Folder", parent)
                      : doc_->addRasterLayer(index, doc_->nextLayerName(), parent);
    if (!id) return;
    long at = doc_->indexOf(id);
    pushCommand(pot::StructureCommand::inserted(size_t(at), id));
    doc_->setSelectedId(id);
    layersChanged();
}

void MainWindow::deleteLayer()
{
    if (!doc_ || doc_->layerCount() <= 1) return;
    long sel = doc_->indexOf(doc_->selectedId());
    if (sel < 0) return;
    const sai_layer &l = doc_->layer(size_t(sel));
    if (l.type == SAI_LAYER_SET && doc_->hasChildren(l.id)) {
        MessageBoxW(hwnd_, L"Empty the folder before deleting it.", kAppName, MB_ICONINFORMATION);
        return;
    }
    pushCommand(pot::StructureCommand::removeNow(*doc_, size_t(sel)));
    size_t next = size_t(sel) > 0 ? size_t(sel) - 1 : 0;
    if (doc_->layerCount()) doc_->setSelectedId(doc_->layer(std::min(next, doc_->layerCount() - 1)).id);
    layersChanged();
}

void MainWindow::renameLayer()
{
    if (!doc_) return;
    long sel = doc_->indexOf(doc_->selectedId());
    if (sel < 0) return;
    sai_layer &l = doc_->layer(size_t(sel));
    std::wstring name = fromAnsi(l.name);
    if (!promptText(hwnd_, L"Rename Layer", L"Name", name)) return;
    std::string ansi = toAnsi(name);
    if (ansi.size() > 255) ansi.resize(255);
    pot::LayerProps before = pot::LayerProps::of(l), after = before;
    std::memset(after.name, 0, sizeof(after.name));
    std::memcpy(after.name, ansi.data(), ansi.size());
    after.applyTo(l);
    pushCommand(std::make_unique<pot::PropsCommand>(l.id, before, after));
    doc_->setModified();
    layersChanged();
}

void MainWindow::clearLayer()
{
    if (!doc_) return;
    long sel = doc_->indexOf(doc_->selectedId());
    if (sel < 0) return;
    uint32_t id = doc_->layer(size_t(sel)).id;
    pot::Raster *r = doc_->raster(id);
    if (!r) return;
    pot::StrokeResult res;
    res.rect = pot::Rect{0, 0, doc_->width(), doc_->height()};
    res.before = r->px;
    res.after.assign(r->px.size(), 0);
    std::fill(r->px.begin(), r->px.end(), 0);
    r->dirty = true;
    doc_->setModified();
    pushCommand(std::make_unique<pot::PixelCommand>(id, std::move(res)));
    refresh();
}

// Swap the selected layer with its neighbour in the same folder.
void MainWindow::moveLayer(int dir)
{
    if (!doc_) return;
    long sel = doc_->indexOf(doc_->selectedId());
    if (sel < 0) return;
    const uint32_t parent = doc_->parentOf(size_t(sel));
    long j = sel + dir;
    while (j >= 0 && size_t(j) < doc_->layerCount() && doc_->parentOf(size_t(j)) != parent) j += dir;
    if (j < 0 || size_t(j) >= doc_->layerCount()) return;
    auto cmd = std::make_unique<pot::MoveCommand>(size_t(sel), size_t(j), parent, parent);
    cmd->redo(*doc_);
    pushCommand(std::move(cmd));
    layersChanged();
}

void MainWindow::showAbout()
{
    MessageBoxW(hwnd_,
                L"potationsail 0.1\n\nA small drawing program built on libsai.\n"
                L"Reads and writes PaintTool SAI 1.x (.sai) documents, composites layers\n"
                L"with SAI's own blend-mode arithmetic and smooths strokes with a\n"
                L"SAI-style stabilizer.\n\nPaintTool SAI is a product of SYSTEMAX.\n"
                L"This program is not affiliated with SYSTEMAX.",
                L"About potationsail", MB_ICONINFORMATION);
}

void MainWindow::command(int id)
{
    switch (id) {
    case ID_FILE_NEW: fileNew(); break;
    case ID_FILE_OPEN: fileOpen(); break;
    case ID_FILE_SAVE: fileSave(false); break;
    case ID_FILE_SAVEAS: fileSave(true); break;
    case ID_FILE_EXIT: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
    case ID_EDIT_UNDO:
    case ID_EDIT_REDO: {
        if (!doc_ || stroke_.active()) break;
        size_t before = doc_->layerCount();
        pot::Rect r = id == ID_EDIT_UNDO ? undo_.undo(*doc_) : undo_.redo(*doc_);
        if (doc_->indexOf(doc_->selectedId()) < 0 && doc_->layerCount())
            doc_->setSelectedId(doc_->layer(doc_->layerCount() - 1).id);
        layers_->rebuild();
        refresh(before == doc_->layerCount() ? r : pot::Rect{});
        break;
    }
    case ID_LAYER_NEW: newLayer(false); break;
    case ID_LAYER_NEWSET: newLayer(true); break;
    case ID_LAYER_DELETE: deleteLayer(); break;
    case ID_LAYER_RENAME: renameLayer(); break;
    case ID_LAYER_CLEAR: clearLayer(); break;
    case ID_LAYER_UP: moveLayer(+1); break;
    case ID_LAYER_DOWN: moveLayer(-1); break;
    case ID_VIEW_ZOOMIN: canvas_->zoomCentered(1.25); break;
    case ID_VIEW_ZOOMOUT: canvas_->zoomCentered(0.8); break;
    case ID_VIEW_FIT: canvas_->fit(); break;
    case ID_VIEW_ACTUAL: canvas_->actualSize(); break;
    case ID_TOOL_PEN: setTool(Tool::Pen); break;
    case ID_TOOL_ERASER: setTool(Tool::Eraser); break;
    case ID_TOOL_BIGGER: adjustBrushSize(1.15); break;
    case ID_TOOL_SMALLER: adjustBrushSize(1 / 1.15); break;
    case ID_HELP_ABOUT: showAbout(); break;
    }
}

}  // namespace app
