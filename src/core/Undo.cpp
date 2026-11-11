#include "Undo.h"

#include <cstring>

namespace pot {

void PixelCommand::put(Document &d, const std::vector<PixelBGRA> &px)
{
    Raster *r = d.raster(id_);
    if (!r || r_.rect.empty()) return;
    const int W = d.width(), rw = r_.rect.width();
    for (int y = r_.rect.y0; y < r_.rect.y1; y++)
        std::memcpy(&r->px[size_t(y) * W + r_.rect.x0], &px[size_t(y - r_.rect.y0) * rw],
                    size_t(rw) * sizeof(PixelBGRA));
    r->dirty = true;
    d.setModified();
}

LayerProps LayerProps::of(const sai_layer &l)
{
    LayerProps p;
    std::memcpy(p.name, l.name, sizeof(p.name));
    p.opacity = l.opacity;
    p.visible = l.visible;
    p.preserve = l.preserve_opacity;
    p.clipping = l.clipping;
    p.blend = l.blend;
    return p;
}

void LayerProps::applyTo(sai_layer &l) const
{
    std::memcpy(l.name, name, sizeof(l.name));
    l.opacity = opacity;
    l.visible = visible;
    l.preserve_opacity = preserve;
    l.clipping = clipping;
    l.blend = blend;
}

void PropsCommand::undo(Document &d)
{
    long i = d.indexOf(id_);
    if (i >= 0) before_.applyTo(d.layer(size_t(i)));
    d.setModified();
}

void PropsCommand::redo(Document &d)
{
    long i = d.indexOf(id_);
    if (i >= 0) after_.applyTo(d.layer(size_t(i)));
    d.setModified();
}

std::unique_ptr<StructureCommand> StructureCommand::inserted(size_t index, uint32_t id)
{
    std::unique_ptr<StructureCommand> c(new StructureCommand());
    c->inDoc_ = true;
    c->index_ = index;
    c->id_ = id;
    return c;
}

std::unique_ptr<StructureCommand> StructureCommand::removeNow(Document &d, size_t index)
{
    std::unique_ptr<StructureCommand> c(new StructureCommand());
    c->index_ = index;
    c->id_ = d.layer(index).id;
    c->prevSelected_ = d.selectedId();
    c->held_ = d.detach(index);
    c->inDoc_ = false;
    return c;
}

// Undo of an insertion removes; undo of a removal re-inserts.
void StructureCommand::undo(Document &d) { redo(d); }

void StructureCommand::redo(Document &d)
{
    if (inDoc_) {
        long i = d.indexOf(id_);
        if (i < 0) return;
        index_ = size_t(i);
        prevSelected_ = d.selectedId();
        held_ = d.detach(index_);
        inDoc_ = false;
        if (d.selectedId() == id_ && d.layerCount())
            d.setSelectedId(d.layer(index_ > 0 ? index_ - 1 : 0).id);
    } else {
        d.attach(index_, std::move(held_));
        inDoc_ = true;
        if (prevSelected_ && d.indexOf(prevSelected_) >= 0) d.setSelectedId(prevSelected_);
    }
}

size_t StructureCommand::bytes() const
{
    if (inDoc_) return 64;
    return 64 + (held_.valid() ? held_.rasterBytes() : 0);
}

void MoveCommand::move(Document &d, size_t from, size_t to, uint32_t parent)
{
    DetachedLayer l = d.detach(from);
    if (!l.valid()) return;
    uint32_t id = l.id();
    d.attach(to, std::move(l));
    long i = d.indexOf(id);
    if (i >= 0) {
        sai_layer &s = d.layer(size_t(i));
        s.has_parent_set = parent ? 1 : 0;
        s.parent_set_id = parent;
    }
}

void UndoStack::push(std::unique_ptr<Command> c)
{
    cmds_.resize(pos_);  // drop the redo branch
    cmds_.push_back(std::move(c));
    pos_ = cmds_.size();
    size_t total = 0;
    for (auto &k : cmds_) total += k->bytes();
    while (total > budget_ && cmds_.size() > 1) {
        total -= cmds_.front()->bytes();
        cmds_.erase(cmds_.begin());
        pos_--;
    }
}

Rect UndoStack::undo(Document &d)
{
    if (!canUndo()) return Rect{};
    Command &c = *cmds_[--pos_];
    c.undo(d);
    return c.area();
}

Rect UndoStack::redo(Document &d)
{
    if (!canRedo()) return Rect{};
    Command &c = *cmds_[pos_++];
    c.redo(d);
    return c.area();
}

}  // namespace pot
