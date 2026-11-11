/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_ANIMATION_RIGATLAS2D_H
#define UNISCOPE_D2_ANIMATION_RIGATLAS2D_H

#include "../gfx/Texture2D.h"
#include "../gfx/UvRect.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace uniscope::d2 {

// The grid a slot atlas is cut into. Slots are numbered row-major from the
// bottom-left cell (row 0 is the bottom row), matching the UV convention the
// texture upload and the authored art already use.
struct RigAtlasLayout2D {
    int slotCount = 0;
    int grid = 1;
    int atlasSize = 0;
    int facingCount = 1;

    int cellSize() const { return grid > 0 ? atlasSize / grid : 0; }
    bool valid() const {
        return slotCount > 0 && grid > 0 && atlasSize > 0 && facingCount > 0
            && grid * grid >= slotCount;
    }
};

// Opaque texel count in one slot's cell of a full-atlas RGBA buffer.
int rigAtlasOpaqueTexels(
    const std::vector<unsigned char>& pixels,
    const RigAtlasLayout2D& layout,
    int slot);

// Bounding box of a slot's alpha in cell-local texels:
// {minX, minY, maxX, maxY}. A max below min means the slot is empty. Y is
// reported painter-top-down (0 = top of the cell) so callers stay stable
// across the bottom-up storage flip.
void rigAtlasAlphaBounds(
    const std::vector<unsigned char>& pixels,
    const RigAtlasLayout2D& layout,
    int slot,
    int outBounds[4]);

// Composites one decoded image over a slot's cell, clearing the cell first and
// stretching the source across it with nearest-neighbour sampling. Straight
// alpha "source over" with the output alpha composed too. Pure CPU.
void rigAtlasCompositeCell(
    std::vector<unsigned char>& atlasPixels,
    const RigAtlasLayout2D& layout,
    int slot,
    const std::vector<unsigned char>& srcPixels,
    int srcWidth,
    int srcHeight);

// A slot grid atlas: procedural base, one external override per slot, optional
// per-facing variants composited over the active facing, and the GPU texture
// they are uploaded into.
//
// Everything character-specific is a callback: which pixels are procedural
// (full rebuild and single-cell repaint), which (slot, facing) pairs may carry
// directional art, and whether a GL context exists. The mechanism - cell UVs,
// slot addressing, binding, compositing, rollback, region upload and the
// facing-swap no-op - is owned here.
class RigAtlas2D {
public:
    using FullPainter = std::function<std::vector<unsigned char>()>;
    using CellPainter = std::function<void(std::vector<unsigned char>&, int)>;
    using Gate = std::function<bool()>;

    // `slotCount` cells of `atlasSize` in a `grid` x `grid` layout. Frames are
    // registered immediately, so headless callers can resolve UVs. Returns
    // false for a layout that cannot hold every slot.
    bool configure(const RigAtlasLayout2D& layout);
    const RigAtlasLayout2D& layout() const { return layout_; }

    // Procedural sources: a full-atlas painter (called on buildProcedural) and
    // a single-cell painter (called by repaintSlots). Both must exist before
    // anything is built.
    void setProceduralPainters(FullPainter full, CellPainter cell);
    // The GL gate. Without it the atlas still registers frames and records
    // bindings; it just declines to upload. The game passes its context check,
    // because touching a GL entry point with no current context is undefined
    // behaviour.
    void setUploadGate(Gate gate);
    // Which (slot, facing) pairs may carry variants. Default: any in range.
    void setVariantGate(std::function<bool(int, int)> gate);
    // Leaves a slot's frame unregistered (a slot that has no art cell, such as
    // a separately-drawn shadow) and makes it unbindable.
    void skipFrame(int slot);

    bool valid() const { return texture_.valid(); }
    const Texture2D& texture() const { return texture_; }
    const UvRect* frame(int slot) const;

    // Resets every binding and rebuilds the procedural base, uploading it when
    // there is a context. The UVs are re-registered either way.
    void buildProcedural();
    // Repaints the procedural base while keeping every bound external slot and
    // variant (used when only the procedural source changed, e.g. eye state).
    void repaintProcedural();
    // Repaints and re-uploads only the procedural cells of `slots`, with their
    // external art and active variant composited over them. The cheap path for
    // a two-cell change.
    void repaintSlots(const int* slots, std::size_t count);

    // Binds one slot with art loaded from disk. Existing UVs are kept, so the
    // skin never needs a re-resolve. Returns false (and changes nothing) when
    // there is no upload path, the slot is unbindable, or the file will not
    // decode.
    bool setSlotImage(int slot, const std::string& path);
    // Binds directional art over a slot's cell for one facing. The active
    // variant is composited on top of the slot's art at upload time; a missing
    // variant falls back to whatever the cell already shows.
    bool bindSlotVariant(int slot, int facing, const std::string& path);
    bool hasVariant(int slot, int facing) const;
    // Switches the visible facing. Never fails: an unbound variant is the
    // fallback chain, not an error. A swap between two variant-less facings is
    // a no-op (the composited pixels would be byte-identical).
    void setActiveFacing(int facing);
    int activeFacing() const { return activeFacing_; }

    // True only after the art actually reached the GPU.
    bool slotIsExternal(int slot) const;
    int externalSlotCount() const;

private:
    bool uploadAllowed() const { return uploadGate_ ? uploadGate_() : true; }
    bool variantAllowed(int slot, int facing) const;
    bool facingHasVariants(int facing) const;
    bool rebuildAndUpload();
    void clearBindings();
    int slotOriginX(int slot) const;
    int slotOriginY(int slot) const;
    int variantIndex(int slot, int facing) const;

    RigAtlasLayout2D layout_;
    FullPainter fullPainter_;
    CellPainter cellPainter_;
    Gate uploadGate_;
    std::function<bool(int, int)> variantGate_;
    std::vector<char> skipFrame_;
    std::vector<UvRect> frames_;
    Texture2D texture_;
    std::vector<unsigned char> atlasPixels_;
    std::vector<char> external_;
    std::vector<std::vector<unsigned char>> externalPixels_;
    std::vector<int> externalWidths_;
    std::vector<int> externalHeights_;
    std::vector<std::vector<unsigned char>> variantPixels_;
    std::vector<int> variantWidths_;
    std::vector<int> variantHeights_;
    std::vector<char> hasVariant_;
    int activeFacing_ = 0;
};

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_ANIMATION_RIGATLAS2D_H
