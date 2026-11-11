/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_MATERIAL2D_H
#define UNISCOPE_D2_MATERIAL2D_H

#include <cstdint>
#include <string>

namespace uniscope::d2 {

enum class MaterialPreset2D {
    Unlit,
    Lit2D,
    SdfText,
    Particle,
    Custom
};

// Per-sprite material classification consumed by textured2d_batch.frag.
// Values are packed into the vertex material-params attribute so the shader
// can branch on an explicit kind instead of magic numeric ranges.
enum class MaterialKind2D : int {
    Unlit = 0,      // no material response: UI, player, plain sprites
    Water = 1,      // animated surface + depth-edge blending
    Organic = 2,    // grass/foliage: subtle hue variation
    Mineral = 3,    // rock/blocks: matte roughness
    Emissive = 4,   // fire/heat sources: pulse + additive glow
    Foliage = 5,    // dense ground cover: world-space detail breakup
    Fabric = 6,     // cloth/leather: soft anisotropic sheen
    Shadow = 7,     // projected shadow quad: darkened albedo, unlit
    // A building elevation. Responds per PART rather than per quad: the
    // companion material mask bound with the sprite says which material each
    // texel is (wall, roof, glass, trim, metal, stone, cloth, snow) and each
    // one takes the direct sun, the sky fill, a highlight and a cast shadow by
    // its own measure. Without a mask bound it renders as a flat lit surface,
    // so it is safe for a caller that has none.
    BuildingSurface = 8,
};

// Per-sprite shading overrides, packed into a small int flag field.
enum class MaterialFlags2D : int {
    None = 0,
    // Flat surface: the geometric normal is constant and per-quad edge
    // occlusion is skipped. Tiling ground MUST use this. The volumetric path
    // derives its normal from the quad's own local coordinates, so on a
    // repeated ground texture it re-creates the one-tile period as a lighting
    // gradient - which is exactly the grid the renderer is trying to remove.
    FlatSurface = 1 << 0,
    // Skip the rim term for volumetric art that must not get a silhouette
    // highlight. Flat art already opts out of the rim through FlatSurface.
    NoRim = 1 << 1,
    // Casts a ground-projected shadow quad when shadows are enabled. The
    // renderer emits a darkened, sun-offset copy of the sprite before the
    // sprite itself, sharing the same texture so batching is preserved.
    CastShadow = 1 << 2,
};

// How the material boundary on one side of a quad is drawn. Two bits per side:
// North/East/South/West in bits 0-7 of MaterialParams2D::edgeMask. The corner
// half of that field (bits 8-15) carries derived corner *shapes* instead - a
// reduced form of the diagonals, whose values are the game's game::CornerShape
// and do not belong to this enum.
//
// A side carries the treatment of the BOUNDARY, not the category of the
// neighbour it faces, and the two quads sharing a boundary carry the same one:
// the game layer picks the harder of the two materials (game::edgeCategoryRank)
// so that both sides of a seam draw the same band. A side whose two tiles are
// the same material is Joined and draws nothing, which is what keeps a field
// of one terrain seamless, while the edges facing a different material get a
// band instead of the per-quad vignette that used to turn every tile into a
// visible cell.
//
// Corner i sits between side i and side (i + 1) % 4.
//
// The shader implements the geometry in textured2d_batch.frag. Style numbers
// arrive through Renderer2D::setEdgeStyleTable, so this enum is only an index
// layout, not a second style table. The game-layer source of truth is
// game::EdgeCategory, and the corner half of the mask is game::CornerShape.
enum class EdgeCategory2D : int {
    // The two quads are one surface: no band.
    Joined = 0,
    // Standing water or ice. Wet, so this is a damp stain - narrow and soft.
    Water = 1,
    // Soil, sediment, vegetation. A path edge or a meadow-to-dirt change.
    Soil = 2,
    // Bare rock or impassable terrain. A hard cut face.
    Rock = 3,
};

// A ground transition draw. The ordinary ground quad uses Base only on sides
// where an overlay exists; the two overlays use FarOnNear and NearOnFar. Their
// alpha weights are complementary views of one field, so the two materials,
// their tints and their detail layers all crossfade with weights summing to 1.
enum class TransitionLayer2D : std::uint8_t {
    None = 0,
    Base = 1,
    FarOnNear = 2,
    NearOnFar = 3,
};

struct TransitionParams2D {
    TransitionLayer2D layer = TransitionLayer2D::None;
    // Overlay: 0 = cross X, 1 = cross Y. Base: bitmask of sides that have an
    // overlay, in North/East/South/West order. Keeping this outside edgeMask
    // preserves its exact 24-bit layout.
    int axisOrSideMask = 0;
    float jitter = 0.0f;
    float tangentOrigin = 0.0f;
};

// World-space detail mapping.
//
// A tileable detail texture is sampled at a coordinate derived from the
// sprite's own clip-space position, so the pattern is anchored to the world
// rather than to the quad. Repeating ground textures therefore stop reading as
// a grid: neighbouring tiles share one continuous detail field even when the
// albedo underneath them is identical.
//
// `layer` selects which slice of the bound detail texture array to read, which
// is what lets two materials that sit side by side carry genuinely different
// micro-structure instead of one shared field. The sampler is a
// sampler2DArray and the wrap mode is per-layer, so each layer tiles inside
// itself with no gutter ring and no mip level reaching across into the next
// material.
//
// There is deliberately no per-draw `rotation`. Orientation is a property of the
// material, so it belongs in the layer's own pixels; exposing it per draw call
// only makes it possible for a tile and the corner patch drawn over it to
// disagree about which way its grain runs, and the two are required to read as
// one surface.
struct DetailMap2D {
    float scale = 0.0f;     // detail repeats across the NDC y range; 0 disables
    float strength = 0.0f;  // 0..1 blend between albedo and detail modulation
    float offsetU = 0.0f;   // per-sprite offset so tiles do not align
    float offsetV = 0.0f;
    std::uint8_t layer = 0; // which slice of the detail array this material reads
};

class Material2D {
public:
    Material2D() = default;
    explicit Material2D(MaterialPreset2D preset);

    MaterialPreset2D preset() const { return preset_; }
    const std::string& customVertexPath() const { return customVertex_; }
    const std::string& customFragmentPath() const { return customFragment_; }

    void setCustomShaders(std::string vertexPath, std::string fragmentPath);

private:
    MaterialPreset2D preset_ = MaterialPreset2D::Unlit;
    std::string customVertex_;
    std::string customFragment_;
};

} // namespace uniscope::d2

#endif
