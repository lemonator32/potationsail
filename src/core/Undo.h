// Command based undo/redo.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "Brush.h"
#include "Document.h"

namespace pot {

class Command {
public:
    virtual ~Command() = default;
    virtual void undo(Document &d) = 0;
    virtual void redo(Document &d) = 0;
    virtual size_t bytes() const { return 64; }
    // Area that needs re-compositing after undo/redo; empty = whole canvas.
    virtual Rect area() const { return Rect{}; }
};

// Pixels of one raster layer inside a rectangle.
class PixelCommand : public Command {
public:
    PixelCommand(uint32_t layerId, StrokeResult &&r) : id_(layerId), r_(std::move(r)) {}
    void undo(Document &d) override { put(d, r_.before); }
    void redo(Document &d) override { put(d, r_.after); }
    size_t bytes() const override { return (r_.before.size() + r_.after.size()) * 4; }
    Rect area() const override { return r_.rect; }

private:
    void put(Document &d, const std::vector<PixelBGRA> &px);
    uint32_t id_;
    StrokeResult r_;
};

// Snapshot of a layer's header fields (name, opacity, blend, flags...).
struct LayerProps {
    char name[257] = {};
    uint8_t opacity = 100, visible = 1, preserve = 0, clipping = 0;
    uint32_t blend = 0;
    static LayerProps of(const sai_layer &l);
    void applyTo(sai_layer &l) const;
};

class PropsCommand : public Command {
public:
    PropsCommand(uint32_t id, const LayerProps &before, const LayerProps &after)
        : id_(id), before_(before), after_(after) {}
    void undo(Document &d) override;
    void redo(Document &d) override;

private:
    uint32_t id_;
    LayerProps before_, after_;
};

// Insert (added = true) or remove a layer; owns the detached layer when it is
// not in the document.
class StructureCommand : public Command {
public:
    // Record an insertion that has already happened at `index`.
    static std::unique_ptr<StructureCommand> inserted(size_t index, uint32_t id);
    // Remove the layer at `index` now and record it.
    static std::unique_ptr<StructureCommand> removeNow(Document &d, size_t index);
    void undo(Document &d) override;
    void redo(Document &d) override;
    size_t bytes() const override;

private:
    StructureCommand() = default;
    bool inDoc_ = true;
    size_t index_ = 0;
    uint32_t id_ = 0;
    uint32_t prevSelected_ = 0;
    DetachedLayer held_;
};

// Move one layer from index `from` to `to` (and optionally into another set).
class MoveCommand : public Command {
public:
    MoveCommand(size_t from, size_t to, uint32_t oldParent, uint32_t newParent)
        : from_(from), to_(to), oldParent_(oldParent), newParent_(newParent) {}
    void undo(Document &d) override { move(d, to_, from_, oldParent_); }
    void redo(Document &d) override { move(d, from_, to_, newParent_); }
    static void move(Document &d, size_t from, size_t to, uint32_t parent);

private:
    size_t from_, to_;
    uint32_t oldParent_, newParent_;
};

class UndoStack {
public:
    explicit UndoStack(size_t budgetBytes = size_t(512) << 20) : budget_(budgetBytes) {}
    void push(std::unique_ptr<Command> c);
    bool canUndo() const { return pos_ > 0; }
    bool canRedo() const { return pos_ < cmds_.size(); }
    Rect undo(Document &d);
    Rect redo(Document &d);
    void clear() { cmds_.clear(); pos_ = 0; }

private:
    std::vector<std::unique_ptr<Command>> cmds_;
    size_t pos_ = 0;
    size_t budget_;
};

}  // namespace pot
