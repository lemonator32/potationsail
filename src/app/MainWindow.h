// Top-level window: owns the document, undo stack, brush state and the three
// child panels (tools on the left, canvas in the middle, layers on the right).
#pragma once
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "Brush.h"
#include "Document.h"
#include "Undo.h"

namespace app {

class CanvasView;
class LayerPanel;
class ToolPanel;

enum class Tool { Pen, Eraser };

class MainWindow {
public:
    MainWindow();
    ~MainWindow();
    bool create(HINSTANCE inst, int show);
    HWND hwnd() const { return hwnd_; }
    HACCEL accelerators() const { return accel_; }
    void openFile(const std::wstring &path);

    // --- shared state used by the panels ---------------------------------
    pot::Document *doc() { return doc_.get(); }
    pot::UndoStack &undo() { return undo_; }
    pot::BrushSettings &brush() { return brush_; }
    Tool tool() const { return tool_; }
    void setTool(Tool t);
    int stabilizer() const { return stabilizer_; }
    void setStabilizer(int code) { stabilizer_ = code; }
    const std::vector<pot::PixelBGRA> &display() const { return display_; }

    // Painting (canvas coordinates).
    bool beginStroke(bool penEraser);
    void strokePoint(double x, double y, double pressure);
    void endStroke();
    bool stroking() const { return stroke_.active(); }

    // Re-composite a canvas area (empty rect = everything) and repaint.
    void refresh(pot::Rect r = pot::Rect{});
    void layersChanged();  // structure / properties changed
    void pushCommand(std::unique_ptr<pot::Command> c);

    void command(int id);
    void adjustBrushSize(double factor);

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void buildMenu();
    void layout();
    void setDocument(std::unique_ptr<pot::Document> d, const std::wstring &path);
    void updateTitle();
    bool confirmDiscard();
    void fileNew();
    void fileOpen();
    bool fileSave(bool saveAs);
    void newLayer(bool set);
    void deleteLayer();
    void renameLayer();
    void clearLayer();
    void moveLayer(int dir);
    void showAbout();

    HWND hwnd_ = nullptr;
    HACCEL accel_ = nullptr;
    std::unique_ptr<CanvasView> canvas_;
    std::unique_ptr<LayerPanel> layers_;
    std::unique_ptr<ToolPanel> tools_;

    std::unique_ptr<pot::Document> doc_;
    std::wstring path_;
    pot::UndoStack undo_;
    pot::BrushSettings brush_;
    pot::BrushSettings eraserSettings_;
    Tool tool_ = Tool::Pen;
    int stabilizer_ = 3;

    pot::StrokeEngine stroke_;
    uint32_t strokeLayer_ = 0;
    std::vector<pot::PixelBGRA> composite_;  // premultiplied, transparent backdrop
    std::vector<pot::PixelBGRA> display_;    // composite over the checkerboard
};

// Menu / accelerator command ids.
enum : int {
    ID_FILE_NEW = 100, ID_FILE_OPEN, ID_FILE_SAVE, ID_FILE_SAVEAS, ID_FILE_EXIT,
    ID_EDIT_UNDO = 200, ID_EDIT_REDO,
    ID_LAYER_NEW = 300, ID_LAYER_NEWSET, ID_LAYER_DELETE, ID_LAYER_RENAME, ID_LAYER_CLEAR,
    ID_LAYER_UP, ID_LAYER_DOWN,
    ID_VIEW_ZOOMIN = 400, ID_VIEW_ZOOMOUT, ID_VIEW_FIT, ID_VIEW_ACTUAL,
    ID_TOOL_PEN = 500, ID_TOOL_ERASER, ID_TOOL_BIGGER, ID_TOOL_SMALLER,
    ID_HELP_ABOUT = 600,
};

}  // namespace app
