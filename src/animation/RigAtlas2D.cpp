/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "RigAtlas2D.h"

#include <algorithm>

namespace uniscope::d2 {

namespace {

bool validSlot(const RigAtlasLayout2D& layout, int slot) {
    return slot >= 0 && slot < layout.slotCount && layout.cellSize() > 0;
}

}  // namespace

int rigAtlasOpaqueTexels(
    const std::vector<unsigned char>& pixels,
    const RigAtlasLayout2D& layout,
    int slot) {
    const int cell = layout.cellSize();
    if (!validSlot(layout, slot) || cell <= 0) {
        return 0;
    }
    const int originX = (slot % layout.grid) * cell;
    const int originY = (layout.grid - 1 - slot / layout.grid) * cell;
    int opaque = 0;
    for (int y = 0; y < cell; ++y) {
        for (int x = 0; x < cell; ++x) {
            const std::size_t index = static_cast<std::size_t>(
                ((originY + y) * layout.atlasSize + (originX + x)) * 4);
            if (index + 3 < pixels.size() && pixels[index + 3] > 128) {
                ++opaque;
            }
        }
    }
    return opaque;
}

void rigAtlasAlphaBounds(
    const std::vector<unsigned char>& pixels,
    const RigAtlasLayout2D& layout,
    int slot,
    int outBounds[4]) {
    if (outBounds == nullptr) {
        return;
    }
    const int cell = layout.cellSize();
    outBounds[0] = cell;
    outBounds[1] = cell;
    outBounds[2] = -1;
    outBounds[3] = -1;
    if (!validSlot(layout, slot) || cell <= 0) {
        return;
    }
    const int originX = (slot % layout.grid) * cell;
    const int originY = (layout.grid - 1 - slot / layout.grid) * cell;
    for (int y = 0; y < cell; ++y) {
        for (int x = 0; x < cell; ++x) {
            const std::size_t index = static_cast<std::size_t>(
                ((originY + y) * layout.atlasSize + (originX + x)) * 4);
            if (index + 3 < pixels.size() && pixels[index + 3] > 24) {
                outBounds[0] = std::min(outBounds[0], x);
                // Painter-top-down Y, so callers stay stable across the
                // bottom-up storage flip.
                const int painterY = cell - 1 - y;
                outBounds[1] = std::min(outBounds[1], painterY);
                outBounds[2] = std::max(outBounds[2], x);
                outBounds[3] = std::max(outBounds[3], painterY);
            }
        }
    }
}

void rigAtlasCompositeCell(
    std::vector<unsigned char>& atlasPixels,
    const RigAtlasLayout2D& layout,
    int slot,
    const std::vector<unsigned char>& srcPixels,
    int srcWidth,
    int srcHeight) {
    const int cell = layout.cellSize();
    if (!validSlot(layout, slot) || cell <= 0 || srcWidth <= 0 || srcHeight <= 0
        || srcPixels.size()
            < static_cast<std::size_t>(srcWidth) * static_cast<std::size_t>(srcHeight) * 4) {
        return;
    }
    const std::size_t atlasSize =
        static_cast<std::size_t>(layout.atlasSize) * static_cast<std::size_t>(layout.atlasSize) * 4;
    if (atlasPixels.size() < atlasSize) {
        return;
    }
    const int originX = (slot % layout.grid) * cell;
    const int originY = (layout.grid - 1 - slot / layout.grid) * cell;
    // The source is stretched across the whole cell whatever resolution it was
    // authored at; nearest-neighbour keeps pixel-art edges crisp and never
    // invents colours at a soft edge the way bilinear on straight alpha would.
    //
    // A bound slot REPLACES its cell: clear it to transparent first, because
    // compositing over the procedural base leaves procedural art showing
    // through every transparent texel of the author's PNG.
    for (int ty = 0; ty < cell; ++ty) {
        for (int tx = 0; tx < cell; ++tx) {
            const std::size_t cleared = static_cast<std::size_t>(
                ((originY + ty) * layout.atlasSize + (originX + tx)) * 4);
            atlasPixels[cleared] = 0;
            atlasPixels[cleared + 1] = 0;
            atlasPixels[cleared + 2] = 0;
            atlasPixels[cleared + 3] = 0;
        }
    }
    for (int y = 0; y < cell; ++y) {
        for (int x = 0; x < cell; ++x) {
            const int srcX = (x * srcWidth) / cell;
            const int srcY = (y * srcHeight) / cell;
            const std::size_t source = static_cast<std::size_t>((srcY * srcWidth + srcX) * 4);
            const std::size_t target = static_cast<std::size_t>(
                ((originY + y) * layout.atlasSize + (originX + x)) * 4);
            const float srcA = static_cast<float>(srcPixels[source + 3]) / 255.0f;
            const float dstA = static_cast<float>(atlasPixels[target + 3]) / 255.0f;
            // Straight-alpha "source over" onto the cleared cell, with the
            // output alpha composed too.
            const float outA = srcA + dstA * (1.0f - srcA);
            for (int channel = 0; channel < 3; ++channel) {
                const float srcC =
                    static_cast<float>(srcPixels[source + static_cast<std::size_t>(channel)]);
                const float dstC =
                    static_cast<float>(atlasPixels[target + static_cast<std::size_t>(channel)]);
                const float outC = outA > 0.0f
                    ? (srcC * srcA + dstC * dstA * (1.0f - srcA)) / outA
                    : 0.0f;
                atlasPixels[target + static_cast<std::size_t>(channel)] =
                    static_cast<unsigned char>(outC + 0.5f);
            }
            atlasPixels[target + 3] =
                static_cast<unsigned char>(outA * 255.0f + 0.5f);
        }
    }
}

bool RigAtlas2D::configure(const RigAtlasLayout2D& layout) {
    layout_ = layout;
    if (!layout_.valid()) {
        layout_ = RigAtlasLayout2D{};
        return false;
    }
    const std::size_t slots = static_cast<std::size_t>(layout_.slotCount);
    const std::size_t variants = slots * static_cast<std::size_t>(layout_.facingCount);
    skipFrame_.assign(slots, 0);
    external_.assign(slots, 0);
    externalPixels_.assign(slots, {});
    externalWidths_.assign(slots, 0);
    externalHeights_.assign(slots, 0);
    variantPixels_.assign(variants, {});
    variantWidths_.assign(variants, 0);
    variantHeights_.assign(variants, 0);
    hasVariant_.assign(variants, 0);
    activeFacing_ = 0;
    texture_ = Texture2D{};
    atlasPixels_.clear();
    // UVs are arithmetic over the grid and need no GL, so they are registered
    // before any context check: a headless caller must still resolve its skin.
    frames_.assign(slots, UvRect{});
    for (int index = 0; index < layout_.slotCount; ++index) {
        frames_[static_cast<std::size_t>(index)] = cellUv(
            index % layout_.grid,
            index / layout_.grid,
            layout_.grid,
            layout_.grid,
            layout_.atlasSize);
    }
    return true;
}

void RigAtlas2D::setProceduralPainters(FullPainter full, CellPainter cell) {
    fullPainter_ = std::move(full);
    cellPainter_ = std::move(cell);
}

void RigAtlas2D::setUploadGate(Gate gate) {
    uploadGate_ = std::move(gate);
}

void RigAtlas2D::setVariantGate(std::function<bool(int, int)> gate) {
    variantGate_ = std::move(gate);
}

void RigAtlas2D::skipFrame(int slot) {
    if (slot < 0 || slot >= layout_.slotCount) {
        return;
    }
    skipFrame_[static_cast<std::size_t>(slot)] = 1;
    frames_[static_cast<std::size_t>(slot)] = UvRect{};
}

const UvRect* RigAtlas2D::frame(int slot) const {
    if (!validSlot(layout_, slot)) {
        return nullptr;
    }
    return &frames_[static_cast<std::size_t>(slot)];
}

int RigAtlas2D::slotOriginX(int slot) const {
    return (slot % layout_.grid) * layout_.cellSize();
}

int RigAtlas2D::slotOriginY(int slot) const {
    return (layout_.grid - 1 - slot / layout_.grid) * layout_.cellSize();
}

int RigAtlas2D::variantIndex(int slot, int facing) const {
    return slot * layout_.facingCount + facing;
}

bool RigAtlas2D::variantAllowed(int slot, int facing) const {
    if (!validSlot(layout_, slot)
        || facing < 0 || facing >= layout_.facingCount
        || skipFrame_[static_cast<std::size_t>(slot)] != 0) {
        return false;
    }
    return variantGate_ ? variantGate_(slot, facing) : true;
}

bool RigAtlas2D::facingHasVariants(int facing) const {
    if (facing < 0 || facing >= layout_.facingCount) {
        return false;
    }
    for (int slot = 0; slot < layout_.slotCount; ++slot) {
        if (hasVariant_[static_cast<std::size_t>(variantIndex(slot, facing))]) {
            return true;
        }
    }
    return false;
}

void RigAtlas2D::clearBindings() {
    std::fill(external_.begin(), external_.end(), 0);
    for (std::vector<unsigned char>& stored : externalPixels_) {
        stored.clear();
    }
    std::fill(externalWidths_.begin(), externalWidths_.end(), 0);
    std::fill(externalHeights_.begin(), externalHeights_.end(), 0);
    for (std::size_t index = 0; index < variantPixels_.size(); ++index) {
        hasVariant_[index] = 0;
        variantPixels_[index].clear();
        variantWidths_[index] = 0;
        variantHeights_[index] = 0;
    }
    atlasPixels_.clear();
}

void RigAtlas2D::buildProcedural() {
    clearBindings();
    // Regenerating from scratch throws away any art that was bound over the
    // top, so the external flags and the variant tables go with it.
    if (fullPainter_ && uploadAllowed()) {
        atlasPixels_ = fullPainter_();
        texture_ = Texture2D::loadCellPreservingFromPixels(
            atlasPixels_,
            layout_.atlasSize,
            layout_.atlasSize,
            layout_.grid,
            TextureQuality::High);
    } else if (!uploadAllowed()) {
        // Headless: drop the texture handle; frames and bindings stay usable.
        texture_ = Texture2D{};
    }
}

void RigAtlas2D::repaintProcedural() {
    const std::size_t slots = static_cast<std::size_t>(layout_.slotCount);
    const std::size_t variants = slots * static_cast<std::size_t>(layout_.facingCount);
    std::vector<std::vector<unsigned char>> keptExternal(slots);
    std::vector<int> keptWidths(slots, 0);
    std::vector<int> keptHeights(slots, 0);
    std::vector<char> keptFlags(slots, 0);
    for (std::size_t index = 0; index < slots; ++index) {
        keptExternal[index] = std::move(externalPixels_[index]);
        keptWidths[index] = externalWidths_[index];
        keptHeights[index] = externalHeights_[index];
        keptFlags[index] = external_[index];
    }
    std::vector<std::vector<unsigned char>> keptVariantPixels(variants);
    std::vector<int> keptVariantWidths(variants, 0);
    std::vector<int> keptVariantHeights(variants, 0);
    std::vector<char> keptHasVariant(variants, 0);
    for (std::size_t index = 0; index < variants; ++index) {
        keptVariantPixels[index] = std::move(variantPixels_[index]);
        keptVariantWidths[index] = variantWidths_[index];
        keptVariantHeights[index] = variantHeights_[index];
        keptHasVariant[index] = hasVariant_[index];
    }

    buildProcedural();

    for (std::size_t index = 0; index < slots; ++index) {
        externalPixels_[index] = std::move(keptExternal[index]);
        externalWidths_[index] = keptWidths[index];
        externalHeights_[index] = keptHeights[index];
        external_[index] = keptFlags[index];
    }
    for (std::size_t index = 0; index < variants; ++index) {
        variantPixels_[index] = std::move(keptVariantPixels[index]);
        variantWidths_[index] = keptVariantWidths[index];
        variantHeights_[index] = keptVariantHeights[index];
        hasVariant_[index] = keptHasVariant[index];
    }
    // Headless-safe: declines without a context or base atlas, leaving the
    // restored payloads in place for the next upload.
    rebuildAndUpload();
}

void RigAtlas2D::repaintSlots(const int* slots, std::size_t count) {
    if (slots == nullptr || count == 0) {
        return;
    }
    // Nothing uploaded yet: the first build/rebuild picks the state up, so
    // there is no work.
    if (!uploadAllowed() || atlasPixels_.empty() || !texture_.valid()) {
        return;
    }
    const int cell = layout_.cellSize();

    // 1) Refresh the procedural master for just these cells, so a later full
    //    rebuildAndUpload still composes the correct pixels.
    if (cellPainter_) {
        for (std::size_t i = 0; i < count; ++i) {
            if (validSlot(layout_, slots[i])) {
                cellPainter_(atlasPixels_, slots[i]);
            }
        }
    }

    // 2) Composite the per-cell overrides onto a copy (the master stays
    //    procedural, exactly as the full path keeps it) and upload only the
    //    touched cells.
    std::vector<unsigned char> composited = atlasPixels_;
    const int active = activeFacing_;
    for (std::size_t i = 0; i < count; ++i) {
        if (!validSlot(layout_, slots[i])) {
            continue;
        }
        const std::size_t slot = static_cast<std::size_t>(slots[i]);
        if (external_[slot]) {
            rigAtlasCompositeCell(
                composited,
                layout_,
                slots[i],
                externalPixels_[slot],
                externalWidths_[slot],
                externalHeights_[slot]);
        }
        const int variant = variantIndex(slots[i], active);
        if (hasVariant_[static_cast<std::size_t>(variant)]) {
            rigAtlasCompositeCell(
                composited,
                layout_,
                slots[i],
                variantPixels_[static_cast<std::size_t>(variant)],
                variantWidths_[static_cast<std::size_t>(variant)],
                variantHeights_[static_cast<std::size_t>(variant)]);
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!validSlot(layout_, slots[i])) {
            continue;
        }
        texture_.updateRegion(
            composited,
            layout_.atlasSize,
            slotOriginX(slots[i]),
            slotOriginY(slots[i]),
            cell,
            cell);
        // updateRegion only touches level 0; rebuild this cell's minified
        // levels too, or a downscaled figure keeps showing the old eyes.
        texture_.refreshCellMipmaps(
            composited, layout_.atlasSize, layout_.grid, slots[i]);
    }
}

bool RigAtlas2D::setSlotImage(int slot, const std::string& path) {
    if (!texture_.valid() || !uploadAllowed() || !validSlot(layout_, slot)
        || skipFrame_[static_cast<std::size_t>(slot)] != 0) {
        return false;
    }
    // The decoded size travels with the pixels: deriving the width from the
    // byte count yields the total texel count, not a row length.
    Texture2D::RawRgbaImage replacement = Texture2D::loadRawRgbaWithSize(path);
    if (replacement.pixels.empty() || replacement.width <= 0 || replacement.height <= 0) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(slot);
    std::vector<unsigned char> previousPixels = std::move(externalPixels_[index]);
    const int previousWidth = externalWidths_[index];
    const int previousHeight = externalHeights_[index];
    externalPixels_[index] = std::move(replacement.pixels);
    externalWidths_[index] = replacement.width;
    externalHeights_[index] = replacement.height;
    if (!rebuildAndUpload()) {
        externalPixels_[index] = std::move(previousPixels);
        externalWidths_[index] = previousWidth;
        externalHeights_[index] = previousHeight;
        return false;
    }
    // Recorded only here, after the re-upload, so the flag means the art is
    // actually on the GPU rather than merely that a file was named.
    external_[index] = 1;
    return true;
}

bool RigAtlas2D::bindSlotVariant(int slot, int facing, const std::string& path) {
    if (!texture_.valid() || !uploadAllowed() || !variantAllowed(slot, facing)) {
        return false;
    }
    Texture2D::RawRgbaImage replacement = Texture2D::loadRawRgbaWithSize(path);
    if (replacement.pixels.empty() || replacement.width <= 0 || replacement.height <= 0) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(variantIndex(slot, facing));
    std::vector<unsigned char> previousPixels = std::move(variantPixels_[index]);
    const int previousWidth = variantWidths_[index];
    const int previousHeight = variantHeights_[index];
    variantPixels_[index] = std::move(replacement.pixels);
    variantWidths_[index] = replacement.width;
    variantHeights_[index] = replacement.height;
    if (!rebuildAndUpload()) {
        variantPixels_[index] = std::move(previousPixels);
        variantWidths_[index] = previousWidth;
        variantHeights_[index] = previousHeight;
        return false;
    }
    hasVariant_[index] = 1;
    return true;
}

bool RigAtlas2D::hasVariant(int slot, int facing) const {
    if (!validSlot(layout_, slot)
        || facing < 0 || facing >= layout_.facingCount) {
        return false;
    }
    return hasVariant_[static_cast<std::size_t>(variantIndex(slot, facing))] != 0;
}

void RigAtlas2D::setActiveFacing(int facing) {
    if (facing < 0 || facing >= layout_.facingCount) {
        return;
    }
    if (activeFacing_ == facing) {
        return;
    }
    const int previous = activeFacing_;
    activeFacing_ = facing;
    // Headless-safe: without a context this records the request and
    // rebuildAndUpload declines, and the first upload picks it up.
    // No-op swap skip: neither side owns directional art, so the composited
    // pixels are byte-identical to what is already uploaded.
    if (!facingHasVariants(previous) && !facingHasVariants(facing)) {
        return;
    }
    rebuildAndUpload();
}

bool RigAtlas2D::slotIsExternal(int slot) const {
    if (!validSlot(layout_, slot)) {
        return false;
    }
    return external_[static_cast<std::size_t>(slot)] != 0;
}

int RigAtlas2D::externalSlotCount() const {
    int count = 0;
    for (const char isExternal : external_) {
        if (isExternal) {
            ++count;
        }
    }
    return count;
}

bool RigAtlas2D::rebuildAndUpload() {
    if (!uploadAllowed() || atlasPixels_.empty()) {
        return false;
    }
    std::vector<unsigned char> composited = atlasPixels_;
    for (int slot = 0; slot < layout_.slotCount; ++slot) {
        const std::size_t index = static_cast<std::size_t>(slot);
        if (!external_[index]) {
            continue;
        }
        rigAtlasCompositeCell(
            composited,
            layout_,
            slot,
            externalPixels_[index],
            externalWidths_[index],
            externalHeights_[index]);
    }
    // Directional variants replace their cell for the active facing: same
    // cell, so the skin and its UVs never move and a missing variant is just
    // the cell as it already stands.
    for (int slot = 0; slot < layout_.slotCount; ++slot) {
        const std::size_t index =
            static_cast<std::size_t>(variantIndex(slot, activeFacing_));
        if (!hasVariant_[index]) {
            continue;
        }
        rigAtlasCompositeCell(
            composited,
            layout_,
            slot,
            variantPixels_[index],
            variantWidths_[index],
            variantHeights_[index]);
    }
    texture_ = Texture2D::loadCellPreservingFromPixels(
        composited,
        layout_.atlasSize,
        layout_.atlasSize,
        layout_.grid,
        TextureQuality::High);
    return texture_.valid();
}

}  // namespace uniscope::d2
