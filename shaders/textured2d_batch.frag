#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
in vec2 vLocal;
in vec3 vBlendColor;
in vec4 vEdgeBlend;
in vec4 vMaterialParams; // x=kind, y=wetness, z=emissive, w=detail array layer
in vec2 vNextTexCoord;
in float vFrameBlend;
in vec2 vAnchor;
in vec4 vDetail;         // x=scale, y=strength, z=offsetU, w=offsetV
in float vMaterialFlags;
in float vOccluded;      // how much of this sprite is under casters, 0..1
in vec4 vTransition;     // layer, axis / base side mask, jitter, tangent origin
// Building texture array layer index. >=0 means sample from u_building_array.
in float vBuildingArrayLayer;
out vec4 FragColor;

#include "lib/common.glsl"

uniform sampler2D u_texture;
uniform sampler2D u_normal_texture;
// One layer per material family rather than one shared field. sampler2DArray is
// what makes that possible without a gutter ring or a fract() in the shader:
// GL_REPEAT is per-layer, so each material's world-anchored pattern tiles inside
// itself and no mip level ever averages two materials together.
uniform sampler2DArray u_detail_texture;
uniform int u_has_normal_map;
uniform int u_has_detail_map;
uniform int u_detail_layers;
// Building elevation texture array (one layer per building type).
uniform sampler2DArray u_building_array;
uniform int u_has_building_array;
// Anchor for the detail field, in the same clip space vAnchor is. The field is
// sampled from the sprite's clip position so it is continuous across
// neighbouring quads; subtracting this origin makes it a function of world
// position instead of of screen position. Without it the field is fixed to
// the camera, so the ground's micro-texture slides over the ground as the
// camera moves and the surface reads as flicker. The world passes the clip
// position of the world origin, a character passes its own.
uniform vec2 u_detail_origin;
uniform float u_time;
uniform float u_effect_quality;
uniform float u_aspect;
uniform vec2 u_light_direction;
uniform vec3 u_ambient_color;
uniform float u_ambient_intensity;
uniform sampler2D u_shadow_texture;
uniform int u_shadow_enabled;
// Per-part material mask, bound for the sprite textures that were paired with
// one (Renderer2D::registerSpriteMaterialMask). R = the texel's material part,
// G = that texel's gloss, B = which way its normal leans, A = coverage.
uniform sampler2D u_material_mask_texture;
uniform int u_has_material_mask;
// 0 at the darkest hour, 1 in full sun. Drives the building glass lamp.
uniform float u_daylight;
// 2D heightfield ambient occlusion scale (see WorldRenderer heightfield bake).
// 1.0 keeps the tuned look; 0.0 disables the edge-occlusion term entirely.
uniform float u_aoStrength;
// Subsurface scattering for translucent materials (foliage, cloth, skin).
// Warm light bleeds through thin silhouettes opposite the sun.
uniform float u_sssStrength;
uniform vec3 u_sssColor;
// Environment fog. Density 0 disables. Center is the focus in vAnchor units
// (clip x, viewport y); the delta is aspect-corrected into viewport units and
// then divided by u_fogWorldScale (viewport units per world unit), so the
// falloff measures WORLD distance and stays pinned to the world across zoom.
uniform vec3 u_fogColor;
uniform float u_fogDensity;
uniform vec2 u_fogCenter;
uniform vec2 u_fogWorldScale;
// Aerial perspective lowers contrast with distance before shifting hue toward
// the atmosphere colour: fragments first collapse toward their own luminance
// by this fraction of the fog factor, then mix toward u_fogColor.
const float kFogDesaturate = 0.40;
// Gameplay vignette: gentle inward focus toward the fog center (the player).
// A neutral multiply, never a tint - a tint would fight the anomaly grades
// that own screen-edge colour. 0 disables; the declaration default is 0 as
// well, so every caller that never sets it renders byte-identical frames.
uniform float u_vignette;

// MaterialKind2D values (see Material2D.h) packed into vMaterialParams.x.
const float kKindUnlit    = 0.0;
const float kKindWater    = 1.0;
const float kKindOrganic  = 2.0;
const float kKindMineral  = 3.0;
const float kKindEmissive = 4.0;
const float kKindFoliage  = 5.0;
const float kKindFabric   = 6.0;
const float kKindShadow   = 7.0;
const float kKindBuilding = 8.0;

const int kTransitionBase = 1;
const int kTransitionFarOnNear = 2;
const int kTransitionNearOnFar = 3;

// MaterialFlags2D bits (see Material2D.h).
const int kFlagFlatSurface = 1;
const int kFlagNoRim       = 2;
const int kFlagCastShadow  = 4;
// EdgeCategory2D values (see Material2D.h), 2 bits per side. Side order is
// North, East, South, West, matching game::EdgeCategory and the atlas cells.
//
// A side carries the treatment of the BOUNDARY, not the category of the
// neighbour it faces. The CPU computes it that way (game::boundaryTreatmentMask)
// so the two tiles sharing a boundary carry the same value and draw the same
// band: a boundary is one thing with two sides, and describing it from each
// side is what let the two descriptions disagree.
const int kEdgeJoined = 0;
const int kEdgeWater  = 1;
const int kEdgeSoil   = 2;
const int kEdgeRock   = 3;

// Per-category edge treatment, read from the table the caller uploads.
//
// These numbers used to be three hardcoded functions here, which meant every
// material a game could ever add had to be written into the library's GLSL
// before it could have a boundary of its own. The table is the *drawing
// parameters*, so it belongs to the caller that owns the materials
// (Renderer2D::setEdgeStyleTable); what stays here is the geometry and the
// lighting, which is the part that is genuinely rendering.
//
//   u_edge_style:   x = band width as a fraction of the quad, from the
//                   boundary inward; y = how much the band darkens;
//                   z = bevel strength.
//   u_edge_radius:  x = the base corner radius the category wants, as a
//                   fraction of a tile.
//   u_edge_tint:    rgb = the hue the boundary picks up from the far side, as
//                   a multiplier.
//
// Three separate vec3 arrays rather than one table of a seven-float struct,
// because that is the layout an upload can describe without guessing at a
// struct's padding.
//
// A shoreline is a damp stain, a path edge is a soft roll-off and a rock face
// is a hard cut, so the three cannot share one number - that uniformity is
// what made every transition read as the same painted line. A joined side's
// width is zero, which is what keeps a field of one terrain seamless.
//
// The widths are fractions of a whole tile because a quarter to a third of a
// tile is the smallest band that reads at all: the ground underneath carries a
// couple of percent of its own luminance variation, and a narrower or shallower
// band than that is invisible regardless of how correct it is.
//
// The darken term (y) is zero for every category, deliberately and completely:
// there is no dark outline at a material boundary anywhere in this renderer.
// Boundaries read as a material colour change with a lit lip and a hue shift,
// never as a shadow. A darkening band on both sides of every shared edge is
// what turned mixed terrain into a dark lattice - each edge drawn twice, each
// vertex four times - and no strength of it is small enough to survive that
// arithmetic. The width still shapes the lip and the tint, and the bevel still
// drives the lip, so the three materials keep distinct treatments without any
// of them going dark.
//
// A caller that never uploads a table gets zeros everywhere: no bands, no
// cuts, neutral tint. That is the honest default for a library that does not
// know what the caller's materials are - a boundary band that appeared on its
// own would be a bug, not a feature.
uniform vec3 u_edge_style[4];
uniform vec3 u_edge_radius[4];
uniform vec3 u_edge_tint[4];
uniform float u_edge_tint_strength;

vec3 edgeStyle(int category) {
    return u_edge_style[category];
}

// The chroma a boundary picks up from the material on the far side of it, as a
// multiplier on the band's own colour.
//
// A darkening band on its own says "the ground is lower here", which is a
// statement about height and reads identically whether the neighbour is a lake or
// a cliff. Adding the neighbour's colour is what makes it a *material*
// transition instead: the wet margin on a shoreline goes cool and slightly
// green, a worn path edge goes warm and dusty, a cut rock face goes cold and
// neutral. Three cues that agree - darker, the right hue, a lit lip - read as one
// thing; a darkening alone reads as a shadow and nothing else.
//
// Deliberately small. These are multipliers on an already-darkened band, so a
// large chroma shift turns the boundary into a painted line, which is the failure
// mode a uniform band already had.
vec3 edgeTint(int category) {
    return u_edge_tint[category];
}

// How far toward that chroma the band travels. Above one the multiplier would be
// extrapolated past the neighbour's own colour, which is not something a boundary
// can have. Uploaded with the rest of the table, so the caller controls the
// whole treatment in one place.
#define kEdgeTintStrength u_edge_tint_strength

// Straight material boundaries are crossfaded by two half-band overlays. The
// centre of a side belongs to them; the reserved ends still belong to the old
// corner treatment. Narrowing the crossfade, rather than fading its alpha,
// keeps the 50/50 contour continuous at the seam all the way into a corner.
float transitionWidthScale(vec2 local01, int crossAxis) {
    float tangent = crossAxis == 0 ? local01.y : local01.x;
    return smoothstep(0.30, 0.42, tangent)
        * (1.0 - smoothstep(0.58, 0.70, tangent));
}

// True when this side faces a different material and therefore carries a band.
bool sideIsOpen(int category) {
    return category != kEdgeJoined;
}

// CornerShape values (see game::CornerShape), 2 bits per corner, carried in
// bits 16-23 of the packed material flag field.
const int kCornerNone  = 0;
const int kCornerSpike = 1;
const int kCornerNotch = 2;
const int kCornerTaper = 3;

// The narrowest a *shading band* may get, in pixels, and for the same reason.
//
// A band's width is a fixed world distance too, so a tile small enough on screen
// makes the whole gradient sub-pixel. That does not just soften it: the ramp
// lands between samples, the band stops being a gradient and becomes noise, and
// it then beats against the antialiased alpha beside it. The band and the
// silhouette have to be blurred by the same rule or they disagree at small
// scales, which is worse than either being soft.
const float kBandMinPixels = 1.5;

// The limits of the per-axis corner radius, as fractions of a tile.
//
// The ceiling is what the corner patch underneath has to cover, so the two
// cannot drift apart without a cut landing on the backdrop instead of on the
// diagonal's material - see kCornerPatchScale in WorldRenderer.cpp. The floor
// stops a small rock cut shrinking below the width of its own shading band,
// which would put a full-strength band across the whole surviving lobe.
const float kCornerRadiusMin = 0.16;
const float kCornerRadiusMax = 0.37;

// How far one axis of the corner may be stretched relative to the other, from the
// tile's seed. Kept as a single number with both directions derived from it so
// the pair is exactly reciprocal and the mean radius cannot drift with the seed.
const float kCornerRadiusStretch = 1.075;

// How much of a straight side's band width a rounded corner keeps, and the same
// share of its darkening.
//
// A corner curve is longer than the straight run it replaces, so the boundary
// treatment has to be spread along it or the shading reaches the silhouette at
// full strength and stops dead. Dampened rather than merely narrowed because the
// new curve encloses a much larger lobe than the old one did and a band scaled
// straight from the side style would swallow most of it. The bevel is dampened by
// its own, smaller factor for the same reason.
const float kCornerBandDamp = 0.42;

// How much of the ambient fill a shadowed patch keeps. The ambient term below
// carries most of a ground sprite's brightness (the CPU tile tint is faint), so
// leaving the fill untouched pinned every shadow at ~12% no matter how strong
// the map got. 0.85 blocks most of the skylight a caster occludes while still
// leaving a warm floor under every shadow; see the note at the ambient term.
const float kShadowAmbientRate = 0.85;

// The base corner radius each material wants, as a fraction of a tile.
//
// A corner is a transition between two materials, so a water tile meeting a rock
// tile gets the average of the two: neither side should get to dictate a
// silhouette that runs along its own material's border. Water is the most
// generous because a shoreline is drawn with a soft, wet margin; rock is the
// tightest because a cut face is meant to read as broken rather than bevelled.
//
// The default 0.27 for an unlisted category is the mid radius it always was: a
// caller that uploads no table at all gets no corners to round anyway, so the
// value only matters when a category is added without a radius.
float naturalRadiusFor(int category) {
    float radius = u_edge_radius[category].x;
    return radius > 0.0 ? radius : 0.27;
}

// The radius a corner between these two materials wants, before the per-tile
// stretch. Shared with naturalCorner and with the side-band handover below, so
// all three agree on where the corner region begins - a cut, a band and a
// handover that disagreed about the radius would show up as a step in the middle
// of one continuous boundary.
float cornerBaseRadius(int sideA, int sideB) {
    return 0.5 * (naturalRadiusFor(sideA) + naturalRadiusFor(sideB));
}

// True for the two shapes where something other than the two straight sides
// carries the boundary across the corner.
//
// A spike cuts its corner and a corner patch fills it, so the corner band is the
// only thing describing that part of the boundary. A taper has no patch, but its
// own band already unwinds along the two straight edges, which is the same job.
// A notch and a none have no corner band at all, and damping the sides for those
// would leave a bright hole at every convex material corner - a mitre really is
// two straight bands crossing, and that is the correct treatment for it.
bool cornerTakesOver(int shape) {
    return shape == kCornerSpike || shape == kCornerTaper;
}

float cornerHandover(int shape, int sideA, int sideB, float vertexDistance) {
    if (!cornerTakesOver(shape)) {
        return 1.0;
    }
    return smoothstep(0.0, cornerBaseRadius(sideA, sideB), vertexDistance);
}

// The signed distance from a fragment to a corner's curve, in this quad's own
// units: negative inside the tile, positive in the part the tile has given up.
//
// This is the rounded-box corner field rather than a superellipse, and the
// difference is not cosmetic. A superellipse has to be evaluated with three
// nested pow() calls whose exponents vary per corner, which is both the most
// expensive thing in this shader and the least stable at the vertex, where the
// metric's gradient is singular and a finite difference across it is
// meaningless. The rounded-box form is one divide and one length(): it is
// exactly a circle scaled independently per axis, so an anisotropic corner falls
// out of a single expression instead of a fitted exponent, and the result stays
// monotonic in the fragment coordinate, which is what the antialiasing ramp
// below depends on.
//
// The clamp to zero is what makes this a *corner* and not a full box: a fragment
// more than one radius in from either side is off the curve entirely and reads
// as solid, so only the quadrant near the vertex contributes. Scaled by the
// smaller axis so the field is a true distance in the same units as the band
// widths above, which keeps the band and the silhouette blurred by one rule.
float naturalCornerDistance(vec2 edgeDistance, vec2 radius) {
    vec2 q = max(vec2(1.0) - edgeDistance / radius, vec2(0.0));
    return (length(q) - 1.0) * min(radius.x, radius.y);
}

// One corner of the quad, as (coverage, band, shade, bevel).
//
// `shape` is the CornerShape the level layout gives this corner, `sideA`/`sideB`
// are the two side treatments meeting there, `corner` is its index for the seed
// lookup, `edgeDistance` is the fragment's distance from the two edges in
// question, `sideBand` is the band the open side laid down and `pixelWidth` is
// one screen pixel in the quad's own units.
//
// A spike is the only shape that gives anything up: this tile stands alone at the
// vertex, so it cuts its corner and the opaque patch of the diagonal's material
// beneath it shows through. A notch is the same vertex seen from the majority
// tile, and the minority tile's patch already fills it, so it must NOT cut or
// both would punch a hole in the same corner. Taper has no patch at all - the
// boundary treatment simply unwinds along the two straight edges. None is a mitre
// between two open sides and keeps the whole square.
vec4 naturalCorner(
    int shape,
    int sideA,
    int sideB,
    int corner,
    int styleSeed,
    vec2 edgeDistance,
    float sideBand,
    float pixelWidth) {
    vec3 styleA = edgeStyle(sideA);
    vec3 styleB = edgeStyle(sideB);

    if (shape == kCornerSpike) {
        float baseRadius = cornerBaseRadius(sideA, sideB);

        // One bit per axis, read from the tile's stable seed, decides whether that
        // axis stretches or squeezes, turning the circle into an ellipse. The
        // magnitude is the point: enough that a wall of identical tiles stops
        // looking stamped from one mould, small enough that no single corner reads
        // as damaged. It is a fixed fraction of the radius rather than an absolute
        // size, so a big shoreline and a small rock cut keep the same character.
        // The seed is five bits wide, which is not enough to hand every corner
        // its own disjoint pair of bits, so the two bits come from a hash of
        // the seed and the corner index instead. Reading the seed's own bits
        // made the fourth corner's second bit constant - the run it wanted was
        // one bit past the end of the seed - so that corner never had the
        // other orientation and every tile's north-west corner was the same
        // shape.
        uint cornerHash = uint(styleSeed) * 2654435761u + uint(corner) * 2246822519u;
        int xCode = int((cornerHash >> 13u) & 1u);
        int yCode = int((cornerHash >> 27u) & 1u);
        vec2 stretch = xCode != 0
            ? vec2(kCornerRadiusStretch, 1.0 / kCornerRadiusStretch)
            : vec2(1.0 / kCornerRadiusStretch, kCornerRadiusStretch);
        if (yCode == 0) {
            stretch = vec2(stretch.y, stretch.x);
        }
        vec2 radius = clamp(baseRadius * stretch, vec2(kCornerRadiusMin), vec2(kCornerRadiusMax));

        float distance = naturalCornerDistance(edgeDistance, radius);
        // Antialiased on the field itself rather than on a fixed width, so the
        // softness follows the curve and stays one pixel wide all the way round.
        // The old version anchored the ramp on the radius, which is the contour
        // where the tile has *entirely* given itself away - 19% outside the line
        // the eye actually reads - so the drawn edge sat inside the blend and the
        // curve changed size with the zoom level.
        float coverage = 1.0 - smoothstep(-pixelWidth, pixelWidth, distance);

        // The band is measured inward from the curve, which is the direction the
        // tile survives: full strength on the silhouette and fading as the tile
        // gets deeper into itself. That is the same profile as a straight side,
        // and it has to be - the band is continuous where the curve runs out and
        // the straight edge takes over, so a different profile here shows up as a
        // kink in the gradient at exactly the place the eye is following it.
        //
        // Width is the harder of the two sides' own bands, floored at the same
        // kBandMinPixels the straight sides use. Not the floor alone: a corner
        // band measured in pixels is a plateau at full strength covering every
        // opaque texel of the lobe, ending in a cliff a pixel and a half from the
        // silhouette, which is a dark rim drawn around the round. The two ends of
        // that ramp cannot both be wrong, so the width has to be the material's.
        float inwardDistance = max(-distance, 0.0);
        float width = max(max(styleA.x, styleB.x) * kCornerBandDamp, pixelWidth * 2.0);
        float band = 1.0 - smoothstep(0.0, width, inwardDistance);
        float shade = max(styleA.y, styleB.y) * kCornerBandDamp * band;
        float bevel = max(styleA.z, styleB.z) * 0.70 * band;
        return vec4(coverage, band, shade, bevel);
    }

    if (shape == kCornerTaper) {
        // One side joins and one does not, so the band has to fade out before
        // the corner or it ends in a square tab. Measured against the two edges
        // rather than the curve, because a taper is the band unwinding along the
        // boundary and the boundary here is still straight.
        vec3 style = sideIsOpen(sideA) ? styleA : styleB;
        float taper = 1.0 - smoothstep(0.0, style.x, min(edgeDistance.x, edgeDistance.y));
        float band = sideBand * taper;
        return vec4(1.0, band, style.y * band, style.z * band);
    }

    // Notch and None both keep everything: see the note above.
    return vec4(1.0, 0.0, 0.0, 0.0);
}

vec3 sampleMaterialRgb(vec2 uv) {
    vec3 color = texture(u_texture, uv).rgb;
    if (vMaterialParams.x > kKindUnlit - 0.5 && vMaterialParams.x < kKindWater + 0.5 && vFrameBlend > 0.0) {
        vec2 nextUv = uv + (vNextTexCoord - vTexCoord);
        color = mix(color, texture(u_texture, nextUv).rgb, clamp(vFrameBlend, 0.0, 1.0));
    }
    return color;
}

// ---- Dynamic building material system -------------------------------------
// A building elevation is one quad and one albedo, but it is not one material:
// brick does not behave like slate, glass does not behave like brick, and a
// cast-iron downspout behaves like neither. So each texel carries its part in a
// companion mask (R), how glossy that texel is (G) and which way its normal
// leans (B), and the part then takes the light by its own measure.
//
// The table and the formula below are mirrored one for one by
// src/game/BuildingMaterialSystem.h - the same duplication discipline the edge
// categories already follow, kept so the response can be verified headlessly.
// The numbers live there; change them there and here together.

// Out-of-screen component of this shader's own light vector, and the fixed
// viewer direction. They fix the tilt of the half vector at ~25 degrees, which
// is what the part normals are tuned against: a part only glints when its own
// normal leans about that far in the sun's direction, which is exactly why a
// brick wall never does and a downspout does.
const float kBuildingSunZ = 0.82;
const float kBuildingViewerZ = 1.0;

// How far the per-texel sheen may rotate a part's normal, in radians at swing
// 1. A full turn is what a cylinder wants: a downspout is a tube, so the texel
// lined up with the sun is a different one every hour, and the highlight
// travels across it. A roof face is a plane and only needs enough registration
// to keep its sheen from reading as one flat sticker.
const float kBuildingSheenSwing = 6.2831853;

// Direct-term response: the brightness a part reaches with the sun square on
// its normal, and the one it reaches with the sun gone.
const float kBuildingDirectFloor = 0.72;
const float kBuildingDirectRange = 0.56;

// How much brightness a fully shadowed part keeps, per unit of its shadow
// weight, and how much of its sky fill it keeps. A shadow blocks the sun, not
// the hemisphere, so the second is far gentler than the first.
const float kBuildingShadowDrop = 0.55;
const float kBuildingShadowSkyKeep = 0.70;

// Sky response: the fill every part gets, and the one the sky-catching parts
// get. A facade is not a paving stone - glass and snow take a hemisphere.
const float kBuildingSkyBase = 1.0;
const float kBuildingSkyCatch = 1.75;

// Per-texel gloss weight, keeping a highlight from reading as a decal pasted
// over the surface.
const float kBuildingGlossFloor = 0.55;
const float kBuildingGlossRange = 0.90;

// The highlight and lamp tints. The highlight takes the sky's hue (warm at
// noon, cool and blue under the moon) and is scaled by how strong the sky is -
// the raw ambient colour is the same at dawn as at noon, so without that
// scaling a first-light flare came out as hot as a midday one. The lamp is the
// fixed warm colour of a room behind glass, and it is NOT scaled by the sky:
// it is not sky light.
const float kBuildingSpecTintScale = 3.0;
const vec3 kBuildingLampColor = vec3(0.55, 0.28, 0.11);
// The lamp's weight by the pane's own luma. A low floor rather than zero: a
// dark pane still leaks some light, and a floor of zero made unlit windows
// vanish into the wall instead of reading as glass in shadow.
const float kBuildingLampAlbedoFloor = 0.15;
const float kBuildingLampAlbedoRange = 0.85;

// How many parts there are, and the factor that takes a mask byte back into
// the space the table below branches on. The painter writes part/Count into R,
// so the read-back scale is (Count - 1). This must stay equal to
// BuildingPart::Count in ProceduralBuildings.h.
const float kBuildingPartCount = 11.0;
const float kBuildingPartRescale = kBuildingPartCount - 1.0;

struct BuildingPartResponse {
    float diffuse;         // 0..1 how hard the part rides the direct sun
    float sky;             // 0..1 how much of the hemisphere it catches
    float gloss;           // 0..1 specular strength
    float specularPower;   // highlight tightness
    float shadow;          // 0..1 how much of a cast shadow it takes
    float lamp;            // 0..1 night self-illumination
    vec2 lean;             // in-plane lean of the part's own normal
    float sheenSwing;      // how far the texel sheen rotates that lean
    float wetness;         // how much of the rain on the facade it takes on
};

// The part table, one row per BuildingPart in ProceduralBuildings.h. Those
// numeric ids are what the mask is baked with and what is branched on below,
// so the two lists are one list in two files.
//
// The four roof faces are the reason the list is this long. A roof is drawn
// from one fixed camera, so all four read as one roof in paint - but the near
// slope faces the viewer and away from this world's northern sun, the far
// slope above the ridge faces north and takes the noon, and the two hips face
// east and west and take the low sun. Their rows differ mainly in the lean:
// which way that face points is the whole of when it lights up.
BuildingPartResponse buildingPartResponse(float part) {
    if (part < 0.5) {
        // Wall: brick, weatherboard, plaster. Near-flat and almost matte.
        return BuildingPartResponse(0.85, 0.35, 0.05, 14.0, 1.00, 0.00, vec2(0.0, 0.03), 0.01, 1.00);
    }
    if (part < 1.5) {
        // RoofSlope: the near face the camera sees. Lives on the sky and on
        // the low sun, and goes matte at the zenith.
        return BuildingPartResponse(0.55, 0.55, 0.45, 26.0, 0.95, 0.00, vec2(0.0, -0.34), 0.08, 0.55);
    }
    if (part < 2.5) {
        // RoofBack: the far face above the ridge, facing north. The face that
        // takes the noon sun, so it swings hardest of anything on the roof.
        return BuildingPartResponse(0.95, 0.50, 0.55, 30.0, 0.95, 0.00, vec2(0.0, 0.34), 0.08, 0.55);
    }
    if (part < 3.5) {
        // RoofHipEast: the morning face.
        return BuildingPartResponse(0.85, 0.50, 0.50, 28.0, 0.95, 0.00, vec2(0.40, 0.0), 0.06, 0.50);
    }
    if (part < 4.5) {
        // RoofHipWest: the evening face.
        return BuildingPartResponse(0.85, 0.50, 0.50, 28.0, 0.95, 0.00, vec2(-0.40, 0.0), 0.06, 0.50);
    }
    if (part < 5.5) {
        // Glass: north-leaning, because this world's sun stands north at noon
        // and the panes take the high sun. It beads in the rain and keeps its
        // sparkle, so it takes almost none of the wet film.
        return BuildingPartResponse(0.35, 1.00, 0.85, 34.0, 0.35, 1.00, vec2(0.0, 0.40), 0.12, 0.15);
    }
    if (part < 6.5) {
        // Trim: paint over wood, leaning up-screen the way a cornice's upper
        // face actually does.
        return BuildingPartResponse(0.90, 0.55, 0.55, 30.0, 0.90, 0.00, vec2(0.0, 0.36), 0.10, 0.70);
    }
    if (part < 7.5) {
        // Metal: the one part that earns a hard dot. A full-turn sheen because
        // the part really is a tube, so the highlight crosses it through the
        // day instead of flaring once and going out.
        return BuildingPartResponse(0.95, 0.70, 0.85, 44.0, 0.70, 0.00, vec2(0.44, 0.0), 1.00, 0.35);
    }
    if (part < 8.5) {
        // Foundation: stone and concrete at the base.
        return BuildingPartResponse(0.80, 0.30, 0.04, 12.0, 1.00, 0.00, vec2(0.0, 0.02), 0.01, 0.45);
    }
    if (part < 9.5) {
        // Fabric: cloth scatters in every direction, so it takes a lot of sky
        // and holds only the softest lobe. It also takes the most rain of
        // anything on a facade: an awning soaks.
        return BuildingPartResponse(0.65, 0.75, 0.30, 20.0, 0.85, 0.00, vec2(0.0, -0.14), 0.14, 1.20);
    }
    // Snow: bright, matte, and the one part that gives up the least to shade.
    // It is already water - it melts rather than wetting - so the wet film is
    // switched off for it rather than darkening the brightest thing there is.
    return BuildingPartResponse(0.95, 0.90, 0.15, 18.0, 0.45, 0.00, vec2(0.0, 0.08), 0.02, 0.00);
}

// Applies one fragment's per-part response to the lit colour. `partId` is the
// mask's R channel, `glossTexel` its G and `sheenTexel` its B.
//
// The three additive terms carry the frame's own colours: the sky term takes
// the ambient fill (which is why the flat ambient term below is skipped for a
// building - it would otherwise give a facade the one thing it must not have,
// the same sky as a paving stone), the highlight takes the sky's hue at the
// sky's own strength, and the lamp is the fixed warm colour of a room behind
// glass.
//
// Nothing in here can be declared const: GLSL only accepts a constant
// expression there, and a response is by definition a function of this frame's
// uniforms and this texel's mask.
void buildingMaterialResponse(
    float partId,
    float glossTexel,
    float sheenTexel,
    float albedoLuma,
    inout float wetnessFilm,
    inout vec3 color) {
    BuildingPartResponse material = buildingPartResponse(partId);
    // The rain this part takes on. A facade is not one material in the rain any
    // more than it is in the sun: masonry drinks it and goes dark, slate sheds
    // it and only glistens where it stands, glass beads up and keeps its
    // sparkle, an awning soaks. Without this the wet film was one flat value
    // over the whole elevation, and a shower made a roof and a window equally
    // dark - which reads as dirt rather than as rain.
    wetnessFilm *= material.wetness;
    float leanLength = length(material.lean);

    // The texel's own normal: the part's lean, rotated by this texel's sheen.
    // A zero-lean part keeps a flat normal and therefore never glints, which
    // is the whole of what makes brick read as brick beside a downspout.
    float lean = 0.0;
    if (leanLength > 0.0001) {
        lean = atan(material.lean.y, material.lean.x)
            + (sheenTexel - 0.5) * kBuildingSheenSwing * material.sheenSwing;
    }
    float tilt = min(leanLength, 0.999);
    vec3 normal = normalize(vec3(
        cos(lean) * tilt,
        sin(lean) * tilt,
        sqrt(max(0.0, 1.0 - tilt * tilt))));

    // Toward the sun, and the half vector between the sun and the viewer.
    vec3 sun = normalize(vec3(u_light_direction, kBuildingSunZ));
    vec3 halfVec = normalize(sun + vec3(0.0, 0.0, kBuildingViewerZ));

    // Direct: mixed rather than multiplied, so a part that never turns toward
    // the sun holds a constant brightness instead of going black, and the
    // parts that do turn swing with it. The shadow drop is per part: a wall
    // takes all of it and a window keeps most of the sky it mirrors, so a
    // shaded facade still reads as a building with windows in it.
    float ndl = max(dot(normal, sun), 0.0);
    float brightness = mix(1.0, kBuildingDirectFloor + kBuildingDirectRange * ndl, material.diffuse);
    brightness *= 1.0 - kBuildingShadowDrop * clamp(vOccluded, 0.0, 1.0) * material.shadow;
    color *= brightness;

    // Sky: every part reads the fill, the sky-catching parts read more of it,
    // and a shadowed part keeps most of what is left because a shadow blocks
    // the sun and not the hemisphere.
    float sky = mix(kBuildingSkyBase, kBuildingSkyCatch, material.sky)
        * mix(1.0, kBuildingShadowSkyKeep, clamp(vOccluded, 0.0, 1.0));
    color += u_ambient_color * u_ambient_intensity * sky;

    // Highlight: the lobe's strength, tightness and per-texel variation, tinted
    // by the sky at the sky's own strength.
    float nh = max(dot(normal, halfVec), 0.0);
    float gloss = pow(nh, material.specularPower)
        * material.gloss
        * (kBuildingGlossFloor + kBuildingGlossRange * glossTexel);
    color += u_ambient_color * u_ambient_intensity * kBuildingSpecTintScale * gloss;

    // Lamp: only the lamp parts carry one, and only once the sky is gone. It is
    // weighted by the pane's own luma because the light behind the glass shapes
    // the glow - a flat term gave a lit pane, a dark pane and the mullion
    // between them the same orange, and a facade of windows read as a grid of
    // rectangles instead of as glass with rooms behind it.
    color += kBuildingLampColor * material.lamp
        * (1.0 - clamp(u_daylight, 0.0, 1.0))
        * (kBuildingLampAlbedoFloor + kBuildingLampAlbedoRange * albedoLuma);
}

void main() {
    float quality = clamp(u_effect_quality, 0.0, 1.0);
    float kind = vMaterialParams.x;
    float wetness = clamp(vMaterialParams.y, 0.0, 1.0);
    float emissive = clamp(vMaterialParams.z, 0.0, 1.0);
    // Fraction of this fragment the corner shapes keep. A ground tile yields
    // alpha at the corners it gives up, and the corner patch drawn underneath it
    // shows through, so the pair composites as a clean lerp between two opaque
    // surfaces rather than as two half-transparent layers stacked.
    float cornerCoverage = 1.0;

    bool isWater = abs(kind - kKindWater) < 0.5;
    // A building elevation: one quad, many materials, each on its own response.
    bool isBuilding = abs(kind - kKindBuilding) < 0.5;
    // How much of this fragment's surface is under a water film.
    //
    // A body of water is not a wet *surface* - it is the wet case already, with its
    // own depth blend and its own shoreline band - so the film terms below are for
    // porous ground and are switched off for it. Its specular still runs, because
    // a wet surface is the wettest thing there is and that response is shared.
    float wetFilm = isWater ? 0.0 : wetness;
    bool isOrganic = abs(kind - kKindOrganic) < 0.5 || abs(kind - kKindFoliage) < 0.5;
    bool isMineral = abs(kind - kKindMineral) < 0.5;
    bool isEmissive = abs(kind - kKindEmissive) < 0.5;
    bool isFoliage = abs(kind - kKindFoliage) < 0.5;
    bool isFabric = abs(kind - kKindFabric) < 0.5;
    // Packed float layout: flags 0-1, per-tile corner style 2-7, side treatments
    // 8-15, corner shapes 16-23. Every integer through 24 bits is exact in float.
    int packedFlags = int(vMaterialFlags + 0.5);
    bool flatSurface = (packedFlags & kFlagFlatSurface) != 0;
    bool noRim = (packedFlags & kFlagNoRim) != 0;
    int cornerStyle = (packedFlags >> 3) & 0x1F;
    int edgeSides = (packedFlags >> 8) & 0xFF;
    int cornerShapes = (packedFlags >> 16) & 0xFF;
    int transitionMode = int(vTransition.x + 0.5);

    // Sample from building array if this sprite uses it (vBuildingArrayLayer >= 0),
// otherwise sample from the regular atlas texture.
vec4 texelColor;
if (u_has_building_array != 0 && vBuildingArrayLayer >= 0.0) {
    texelColor = texture(u_building_array, vec3(vTexCoord, vBuildingArrayLayer));
} else {
    texelColor = texture(u_texture, vTexCoord);
}
if (isWater && vFrameBlend > 0.0) {
    vec4 nextSample;
    if (u_has_building_array != 0 && vBuildingArrayLayer >= 0.0) {
        nextSample = texture(u_building_array, vec3(vNextTexCoord, vBuildingArrayLayer));
    } else {
        nextSample = texture(u_texture, vNextTexCoord);
    }
    texelColor.rgb = mix(texelColor.rgb, nextSample.rgb, clamp(vFrameBlend, 0.0, 1.0));
}

    // The low half draws the far material over the near tile; the high half
    // draws the near material over the far tile. Both evaluate the SAME far
    // weight, so at their shared edge the two composites are identical. The
    // material's ordinary shader path still runs below: albedo, tint, jitter,
    // animation, wetness, detail, light and fog therefore blend together.
    if (transitionMode == kTransitionFarOnNear
        || transitionMode == kTransitionNearOnFar) {
        int crossAxis = int(vTransition.y + 0.5);
        vec2 transitionLocal = clamp(vLocal * 0.5 + 0.5, 0.0, 1.0);
        float along = crossAxis == 0 ? transitionLocal.x : transitionLocal.y;
        float tangent = crossAxis == 0 ? transitionLocal.y : transitionLocal.x;
        float worldTangent = vTransition.w + tangent;
        float wave = vTransition.z * (
            sin(6.2831853 * worldTangent * 0.83)
            + 0.45 * sin(6.2831853 * worldTangent * 1.97 + 1.2)) / 1.45;
        float widthScale = max(transitionWidthScale(transitionLocal, crossAxis), 0.001);
        float weightCoordinate = transitionMode == kTransitionFarOnNear
            ? 0.5 * (1.0 + (along - 1.0) / widthScale + wave)
            : 0.5 * (1.0 + along / widthScale + wave);
        float farWeight = smoothstep(0.0, 1.0, weightCoordinate);
        texelColor.a *= transitionMode == kTransitionFarOnNear
            ? farWeight
            : 1.0 - farWeight;
    }

    vec2 local01 = clamp(vLocal * 0.5 + 0.5, 0.0, 1.0);
    // One screen pixel, in this quad's own units, for the corner cut's
    // antialiasing and for the pixel floor on every shading band. Taken from the
    // unclamped local coordinate: local01 is clamped at the quad border, which is
    // exactly where the corner cut lives, and a clamped derivative there is zero
    // - the AA would switch itself off precisely where it is needed. Hoisted out
    // of the boundary branch so it does not sit in conditional control flow.
    //
    // Named for the quad rather than the corner because the band floor needs it
    // on plain tile edges too, where there is no corner to speak of.
    float quadPixel = 0.5 * (fwidth(vLocal.x) + fwidth(vLocal.y));
    float leftBlend = (1.0 - smoothstep(0.0, 0.58, local01.x)) * vEdgeBlend.x;
    float rightBlend = smoothstep(0.42, 1.0, local01.x) * vEdgeBlend.y;
    float downBlend = (1.0 - smoothstep(0.0, 0.58, local01.y)) * vEdgeBlend.z;
    float upBlend = smoothstep(0.42, 1.0, local01.y) * vEdgeBlend.w;
    float edgeBlend = max(max(leftBlend, rightBlend), max(downBlend, upBlend));
    edgeBlend = smoothstep(0.04, 0.96, edgeBlend);

    // A rounded volume rather than a flat plane: limbs, trunks and props read
    // as cylindrical because the fake normal curves across the quad and turns
    // with it, so rotating a bone also rotates its shading.
    //
    // Tiling ground opts out. The normal is derived from the quad's own local
    // coordinates, so on a repeated ground texture it re-imposes the one-tile
    // period as a lighting ramp and the grid comes straight back.
    vec3 geometricNormal = flatSurface
        ? vec3(0.0, 0.0, 1.0)
        : normalize(vec3(vLocal * 0.42, 1.0));
    vec3 lightDirection = normalize(vec3(u_light_direction, 0.82));
    float diffuse = max(dot(geometricNormal, lightDirection), 0.0);
    // The rim is a silhouette device for volumetric art, and like the geometric
    // normal it is derived from the quad's own local coordinates: a parabola
    // over the vertical axis that brightens the bottom of every quad. On tiled
    // ground that re-imposes the one-tile period as a lighting gradient, which
    // is the grid the flat path exists to remove, so the rim belongs to the
    // volumetric set rather than behind a flag nobody sets. Flat art opts out
    // of it together with everything else.
    float rim = (noRim || flatSurface)
        ? 0.0
        : pow(1.0 - clamp(vLocal.y * 0.5 + 0.5, 0.0, 1.0), 2.0);
    float edgeOcclusion = flatSurface
        ? 0.0
        : 1.0 - smoothstep(0.54, 1.0, max(abs(vLocal.x), abs(vLocal.y)));
    float directionalLight = mix(1.0, mix(0.84, 1.12, diffuse), flatSurface ? 0.0 : 1.0);
    directionalLight *= 1.0 - edgeOcclusion * mix(0.10, 0.18, quality) * clamp(u_aoStrength, 0.0, 1.0);
    // Weak wrapped rim light keeps silhouettes readable against dark terrain
    // without flattening the volume.
    directionalLight += rim * 0.05 * diffuse * quality;

    vec3 color = texelColor.rgb * vColor.rgb;
    float edgeMixLimit = isWater ? 0.72 : 0.48;
    color = mix(color, texelColor.rgb * vBlendColor, clamp(edgeBlend, 0.0, edgeMixLimit));
    color *= directionalLight;
    if (isWater) {
        color *= 1.0 - edgeBlend * 0.28;
    }

    // ---- Fast path for simple flat ground tiles -----------------------------
    // Ground tiles that are NOT buildings, water, emissive, or volumetric props
    // can skip the expensive per-fragment calculations (building materials,
    // detail normal from derivatives, SSS, animated sheen, wet highlight).
    // They still need: edge bands, corner patches, detail map, shadow, fog.
    bool isSimpleGround = flatSurface && !isBuilding && !isWater && !isEmissive
        && !isOrganic && !isMineral && !isFoliage && !isFabric;
    if (isSimpleGround) {
        // Wet film for porous ground (darkens and saturates)
        if (wetFilm > 0.001) {
            vec3 dry = color;
            float dryLuma = dot(dry, vec3(0.2126, 0.7152, 0.0722));
            vec3 deep = mix(vec3(dryLuma), dry, 1.16);
            color = mix(dry, deep * 0.86, wetFilm);
        }

        // World-space detail layer (micro texture breakup)
        float detailStrength = clamp(vDetail.y, 0.0, 1.0) * step(0.0001, vDetail.x);
        if (u_has_detail_map != 0 && detailStrength > 0.001) {
            vec2 world = (vAnchor - u_detail_origin) * vec2(u_aspect, 1.0) * vDetail.x;
            vec2 detailUv = world + vDetail.zw;
            float layer = clamp(
                floor(vMaterialParams.w + 0.5),
                0.0,
                max(float(u_detail_layers) - 1.0, 0.0));
            vec4 detail = texture(u_detail_texture, vec3(detailUv, layer));
            float relief = 0.96 + detail.b * 0.08;
            float macro = 0.97 + detail.a * 0.06;
            float reliefStrength = detailStrength * mix(1.0, 0.28, wetFilm);
            color *= mix(1.0, relief, reliefStrength);
            color *= mix(1.0, macro, detailStrength * mix(0.40, 0.85, wetFilm));
        }

        // Screen-space shadow modulation
        float shadow = 1.0f;
        if (u_shadow_enabled > 0 && (packedFlags & kFlagCastShadow) == 0) {
            vec2 shadowUv = vAnchor * 0.5 + 0.5;
            shadow = 1.0f - texture(u_shadow_texture, shadowUv).r;
        }
        color *= shadow;

        // Ambient light (ground uses flat ambient term)
        color += u_ambient_color * u_ambient_intensity
            * mix(1.0, shadow, kShadowAmbientRate);

        // Environment fog
        if (u_fogDensity > 0.0001) {
            vec2 fogVec = (vAnchor - u_fogCenter) * vec2(u_aspect, 1.0) / u_fogWorldScale;
            float fogDist = length(fogVec);
            float fogFactor = clamp(1.0 - exp(-fogDist * fogDist * u_fogDensity * u_fogDensity), 0.0, 1.0);
            float fogLuma = dot(color, vec3(0.299, 0.587, 0.114));
            color = mix(color, vec3(fogLuma), fogFactor * kFogDesaturate);
            color = mix(color, u_fogColor, fogFactor);
        }

        // Gameplay vignette
        if (u_vignette > 0.0001) {
            vec2 vigVec = (vAnchor - u_fogCenter) * vec2(u_aspect, 1.0);
            float vig = smoothstep(0.45, 1.35, length(vigVec));
            color *= 1.0 - u_vignette * vig;
        }

        // Per-side material boundary (edge bands + corner patches)
        if (edgeSides != 0 || cornerShapes != 0) {
            // ... same edge/corner logic as below, but without shade/bevel modulation
            // that depends on volumetric normals. For ground, bands are purely
            // color transitions with lit lips.
            int edgeN = edgeSides & 3;
            int edgeE = (edgeSides >> 2) & 3;
            int edgeS = (edgeSides >> 4) & 3;
            int edgeW = (edgeSides >> 6) & 3;
            int shapeNE = cornerShapes & 3;
            int shapeSE = (cornerShapes >> 2) & 3;
            int shapeSW = (cornerShapes >> 4) & 3;
            int shapeNW = (cornerShapes >> 6) & 3;

            float dN = 1.0 - local01.y;
            float dE = 1.0 - local01.x;
            float dS = local01.y;
            float dW = local01.x;

            vec3 styleN = edgeStyle(edgeN);
            vec3 styleE = edgeStyle(edgeE);
            vec3 styleS = edgeStyle(edgeS);
            vec3 styleW = edgeStyle(edgeW);

            float wN = styleN.x > 0.0 ? max(styleN.x, kBandMinPixels * quadPixel) : 0.0;
            float wE = styleE.x > 0.0 ? max(styleE.x, kBandMinPixels * quadPixel) : 0.0;
            float wS = styleS.x > 0.0 ? max(styleS.x, kBandMinPixels * quadPixel) : 0.0;
            float wW = styleW.x > 0.0 ? max(styleW.x, kBandMinPixels * quadPixel) : 0.0;

            float bandN = wN > 0.0 ? 1.0 - dN / wN : 0.0;
            float bandE = wE > 0.0 ? 1.0 - dE / wE : 0.0;
            float bandS = wS > 0.0 ? 1.0 - dS / wS : 0.0;
            float bandW = wW > 0.0 ? 1.0 - dW / wW : 0.0;

            // No speck suppression here, and none on the long path either: the
            // band on a side is the treatment of the BOUNDARY, which the two
            // tiles sharing it agree on, so scaling it by one tile's
            // surroundings made the same seam read at two strengths. Nothing
            // darkens a band any more, so there is no speck to suppress.

            // Corner handover
            vec2 dNE = vec2(dE, dN);
            vec2 dSE = vec2(dE, dS);
            vec2 dSW = vec2(dW, dS);
            vec2 dNW = vec2(dW, dN);
            float hNE = cornerHandover(shapeNE, edgeN, edgeE, length(dNE));
            float hSE = cornerHandover(shapeSE, edgeE, edgeS, length(dSE));
            float hSW = cornerHandover(shapeSW, edgeS, edgeW, length(dSW));
            float hNW = cornerHandover(shapeNW, edgeW, edgeN, length(dNW));
            bandN *= max(hNW, hNE);
            bandE *= max(hNE, hSE);
            bandS *= max(hSE, hSW);
            bandW *= max(hSW, hNW);
            if (transitionMode == kTransitionBase) {
                int sides = int(vTransition.y + 0.5);
                if ((sides & 1) != 0) bandN *= 1.0 - transitionWidthScale(local01, 1);
                if ((sides & 2) != 0) bandE *= 1.0 - transitionWidthScale(local01, 0);
                if ((sides & 4) != 0) bandS *= 1.0 - transitionWidthScale(local01, 1);
                if ((sides & 8) != 0) bandW *= 1.0 - transitionWidthScale(local01, 0);
            }
            float band = max(max(bandN, bandE), max(bandS, bandW));

            // The corner's own treatment, so the boundary follows the curve and
            // not only the two straight runs that meet it. Computed before the
            // colour, because a corner that is cut but not shaded is a
            // material change with nothing on it.
            float pixelWidth = max(0.5 * quadPixel, 0.001);
            vec4 cNE = naturalCorner(shapeNE, edgeN, edgeE, 0, cornerStyle, dNE, bandN, pixelWidth);
            vec4 cSE = naturalCorner(shapeSE, edgeE, edgeS, 1, cornerStyle, dSE, bandE, pixelWidth);
            vec4 cSW = naturalCorner(shapeSW, edgeS, edgeW, 2, cornerStyle, dSW, bandS, pixelWidth);
            vec4 cNW = naturalCorner(shapeNW, edgeW, edgeN, 3, cornerStyle, dNW, bandW, pixelWidth);
            cornerCoverage *= cNE.x * cSE.x * cSW.x * cNW.x;
            band = max(band, max(max(cNE.y, cSE.y), max(cSW.y, cNW.y)));

            // Lit side modulation
            vec2 inward = vec2(0.0)
                + vec2(0.0, -1.0) * max(bandN, 0.0)
                + vec2(-1.0, 0.0) * max(bandE, 0.0)
                + vec2(0.0, 1.0) * max(bandS, 0.0)
                + vec2(1.0, 0.0) * max(bandW, 0.0);
            float inwardLength = length(inward);
            float facing = inwardLength > 1e-4
                ? dot(u_light_direction, inward / inwardLength)
                : 0.0;

            // Shade from edge bands (darkening)
            float shade = max(
                max(styleN.y * max(bandN, 0.0), styleE.y * max(bandE, 0.0)),
                max(styleS.y * max(bandS, 0.0), styleW.y * max(bandW, 0.0)));
            shade = max(shade, max(max(cNE.z, cSE.z), max(cSW.z, cNW.z)));
            shade *= 1.0 - 0.22 * facing;
            color *= 1.0 - shade;

            // Edge tint (neighbor material hue bleed)
            vec3 tintWeight = vec3(0.0);
            float tintTotal = 0.0;
            tintWeight += edgeTint(edgeN) * max(bandN, 0.0);
            tintWeight += edgeTint(edgeE) * max(bandE, 0.0);
            tintWeight += edgeTint(edgeS) * max(bandS, 0.0);
            tintWeight += edgeTint(edgeW) * max(bandW, 0.0);
            tintTotal = max(bandN, 0.0) + max(bandE, 0.0) + max(bandS, 0.0) + max(bandW, 0.0);
            if (tintTotal > 0.0001) {
                color = mix(color, color * (tintWeight / tintTotal), band * kEdgeTintStrength);
            }

            // Lit lip highlight
            float bevel = max(
                max(styleN.z * max(bandN, 0.0), styleE.z * max(bandE, 0.0)),
                max(styleS.z * max(bandS, 0.0), styleW.z * max(bandW, 0.0)));
            bevel = max(bevel, max(max(cNE.w, cSE.w), max(cSW.w, cNW.w)));
            float lip = 4.0 * band * (1.0 - band);
            float litSide = clamp(0.5 - 0.5 * facing, 0.0, 1.0);
            color += vec3(0.055, 0.052, 0.044) * lip * bevel * 2.0 * litSide;

        }

        // Tonemap and output
        FragColor = vec4(toneMap(color), texelColor.a * vColor.a * cornerCoverage);
        return;
    }

    // ---- Dynamic building material ----------------------------------------
    // Runs before every term below because it produces the building's whole
    // lighting: its albedo is already shaded by the painter, so the response is
    // the only thing that decides how each part of the facade meets the world's
    // light. A quad with no mask paired to it falls back to a flat lit
    // surface, which is what an unpaired building texture used to be.
    if (isBuilding) {
        // R = the texel's part, G = how glossy that texel is, B = which way its
        // normal leans. The painter writes the part spread across the whole
        // channel, so it is read back rescaled by (Count - 1). A quad with no
        // mask paired to it falls back to a flat wall, which is exactly what an
        // unpaired building used to look like.
        vec3 mask = u_has_material_mask != 0
            ? texture(u_material_mask_texture, vTexCoord).rgb
            : vec3(0.0, 0.5, 0.5);
        float albedoLuma = dot(texelColor.rgb, vec3(0.2126, 0.7152, 0.0722));
        buildingMaterialResponse(
            mask.r * kBuildingPartRescale, mask.g, mask.b, albedoLuma, wetFilm, color);
    }

    // ---- Wet porous surface ------------------------------------------------
    // Water filling a pore does not just dim the surface, it *deepens* the colour:
    // the light that would have scattered back out of a dry grain instead enters
    // the film and comes back through more of the material. So the effect is a
    // darkening and a saturation gain together, not a flat grey multiply - a
    // grey multiply is what makes wet ground read as dirty rather than as wet.
    //
    // Applied to the albedo and not to the lighting terms, because it is a
    // property of the surface's reflectance and has to move with the light rather
    // than being re-derived from it.
    if (wetFilm > 0.001) {
        vec3 dry = color;
        float dryLuma = dot(dry, vec3(0.2126, 0.7152, 0.0722));
        vec3 deep = mix(vec3(dryLuma), dry, 1.16);
        color = mix(dry, deep * 0.86, wetFilm);
    }

    // ---- World-space detail layer -----------------------------------------
    // Sampled from the sprite's clip position so the detail is continuous
    // across neighbouring quads. Without this, a seamless ground texture still
    // repeats on a one-tile period and the repetition reads as a grid.
    //
    // The clip position is then made world-relative by subtracting
    // u_detail_origin, so the field is pinned to the world rather than to the
    // camera: the ground's micro-texture stays on the ground instead of
    // sliding across it as the camera moves.
    float detailStrength = clamp(vDetail.y, 0.0, 1.0) * step(0.0001, vDetail.x);
    vec3 detailNormal = vec3(0.0, 0.0, 1.0);
    if (u_has_detail_map != 0 && detailStrength > 0.001) {
        // World position, not screen position: the anchor turns the clip
        // position into a world-relative one, so the field is pinned to the
        // world and does not crawl across the ground when the camera moves.
        vec2 world = (vAnchor - u_detail_origin) * vec2(u_aspect, 1.0) * vDetail.x;
        vec2 detailUv = world + vDetail.zw;
        // Clamped because an out-of-range sampler2DArray layer returns texel
        // (0,0,0) of the *first* layer rather than nothing, so an unclamped index
        // would quietly read a different material's detail.
        float layer = clamp(
            floor(vMaterialParams.w + 0.5),
            0.0,
            max(float(u_detail_layers) - 1.0, 0.0));
        // RGBA detail map: RG = tangent normal xy, B = height, A = macro mask.
        vec4 detail = texture(u_detail_texture, vec3(detailUv, layer));
        // B multiplies albedo so the surface gains visible micro texture; A is a
        // slow macro mask that stops large ground planes reading as one sheet.
        // Keep the procedural breakup low-contrast. Large per-texel albedo
        // swings read as grain/noise when minified to a few dozen screen pixels.
        //
        // A film of water fills the micro relief and leaves the large forms
        // alone, so the B term is damped hard and the A term barely at all. That
        // asymmetry is the whole of the "it rained" read on a material: the
        // silhouette of the ground survives, its tooth does not.
        float relief = 0.96 + detail.b * 0.08;
        float macro = 0.97 + detail.a * 0.06;
        float reliefStrength = detailStrength * mix(1.0, 0.28, wetFilm);
        color *= mix(1.0, relief, reliefStrength);
        color *= mix(1.0, macro, detailStrength * mix(isFoliage ? 0.65 : 0.40, 0.85, wetFilm));
        // The relief the light sees is the relief that is still above the water,
        // so the tangent normal is flattened by the same factor as the albedo
        // modulation rather than staying fully detailed under a smooth surface.
        vec3 layerNormal = normalize(vec3((detail.rg * 2.0 - 1.0) * 0.75, 1.0));
        detailNormal = normalize(mix(layerNormal, vec3(0.0, 0.0, 1.0), wetFilm * 0.55));
    } else if (wetFilm > 0.001) {
        // No detail layer for this material, but it is wet: the surface is still
        // smoother than a dry one would be, and the volumetric normal is the only
        // relief it has.
        detailNormal = normalize(mix(
            flatSurface ? vec3(0.0, 0.0, 1.0) : normalize(vec3(vLocal * 0.42, 1.0)),
            vec3(0.0, 0.0, 1.0),
            wetFilm * 0.55));
    }

    float wetHighlight = 0.0;
    if (quality >= UNISCOPE_QUALITY_T1) {
        // Ground already has a world-anchored procedural detail map. Adding
        // quad-local noise on top creates a fresh noise pattern in every tile,
        // so flat surfaces deliberately skip this generic sprite grain.
        if (!flatSurface && abs(kind - kKindUnlit) > 0.5) {
            float broadGrain = valueNoise(vTexCoord * 24.0 + vLocal * 2.0);
            color *= 0.985 + broadGrain * 0.03 * quality;
            if (isOrganic) {
                color *= mix(vec3(0.985, 1.01, 0.985), vec3(1.01, 0.985, 0.97), broadGrain * 0.30);
            } else if (isMineral) {
                color *= 0.985 + broadGrain * 0.03;
            }
        }
        if (isFabric && !flatSurface) {
            // Cloth catches a soft sheen at grazing angles.
            color += vec3(0.05, 0.05, 0.06) * pow(rim, 1.5) * quality;
        }
    }

    if (quality >= UNISCOPE_QUALITY_T2) {
        vec3 detailTangent = detailNormal;
        if (u_has_detail_map == 0 || detailStrength <= 0.001) {
            if (u_has_normal_map != 0) {
                // Pre-baked tangent-space normal map: one tap instead of four
                // neighbour samples. RG decode to [-1,1], B is unused.
                vec3 n = texture(u_normal_texture, vTexCoord).rgb * 2.0 - 1.0;
                detailTangent = normalize(vec3(n.xy * 1.8, 1.0));
            } else {
                vec2 texel = 1.0 / vec2(textureSize(u_texture, 0));
                float left = dot(sampleMaterialRgb(vTexCoord - vec2(texel.x, 0.0)), vec3(0.3333));
                float right = dot(sampleMaterialRgb(vTexCoord + vec2(texel.x, 0.0)), vec3(0.3333));
                float down = dot(sampleMaterialRgb(vTexCoord - vec2(0.0, texel.y)), vec3(0.3333));
                float up = dot(sampleMaterialRgb(vTexCoord + vec2(0.0, texel.y)), vec3(0.3333));
                detailTangent = normalize(vec3((left - right) * 1.8, (down - up) * 1.8, 1.0));
            }
        }
        float detailDiffuse = max(dot(detailTangent, lightDirection), 0.0);
        color *= flatSurface
            ? (0.985 + detailDiffuse * 0.03)
            : (0.92 + detailDiffuse * 0.16);
        // Wet surfaces specularly reflect, and how sharply is a continuous
        // function of how much water is on them: the old `step(0.02, wetness)`
        // switched the entire response on at one value, so a tile that had been
        // rained on for one frame looked exactly as wet as one under a downpour
        // and everything between the two was identical to bone dry.
        wetHighlight = pow(max(dot(reflect(-lightDirection, detailTangent), vec3(0.0, 0.0, 1.0)), 0.0), 18.0)
            * smoothstep(0.0, 0.55, wetness);
        color += vec3(0.16, 0.21, 0.24) * wetHighlight * 0.12 * quality;
    }

    if (quality >= UNISCOPE_QUALITY_T3 && !flatSurface) {
        float animatedSheen = sin((vTexCoord.x + vTexCoord.y) * 42.0 + u_time * 0.7) * 0.5 + 0.5;
        color += vec3(0.035, 0.043, 0.05) * animatedSheen * wetHighlight * 0.12;
    }

    // ---- Subsurface scattering -------------------------------------------
    // Translucent materials (leaf, cloth, skin) pass warm light through their
    // thin parts when backlit. Approximated in 2D as a wrap-lit bleed: where
    // the fake normal turns away from the sun, the silhouette edge gains the
    // transmitted tint, strongest at the quad rim (thin) and fading toward the
    // quad middle (thick). Organic and fabric kinds only; flat ground and
    // unlit art keep their response.
    if (u_sssStrength > 0.001 && quality >= UNISCOPE_QUALITY_T1
        && (isOrganic || isFabric) && !flatSurface) {
        float wrapDiffuse = clamp((dot(geometricNormal, lightDirection) + 0.7) / 1.7, 0.0, 1.0);
        float thickness = 1.0 - clamp(length(vLocal) * 0.55, 0.0, 1.0);
        float translucency = pow(1.0 - wrapDiffuse, 2.0) * thickness;
        color += u_sssColor * translucency * u_sssStrength * (0.35 + 0.65 * quality);
    }

    if (isEmissive) {
        // Per-tile phase offset so neighbouring flames do not pulse in sync.
        float tilePhase = hash12(floor(vTexCoord * 64.0)) * 6.2831;
        float flamePulse = 0.94 + 0.06 * sin(u_time * 5.3 + tilePhase + vTexCoord.x * 31.0 + vTexCoord.y * 17.0);
        color += vec3(0.18, 0.055, 0.012) * flamePulse * (0.35 + 0.65 * quality) * max(emissive, 0.4);
    }

    // ---- Per-side material boundary ----------------------------------------
    // Ground quads carry a 2-bit category per side describing what that edge
    // abuts. A side whose neighbour is the same material is Joined and draws
    // nothing, which is the whole point: the volume read comes back only where
    // the material actually changes, instead of the per-quad vignette that
    // turned every tile into a visible cell.
    //
    // This runs on the final colour rather than feeding the lighting terms,
    // because a boundary is a fact about the terrain's layout, not about this
    // quad's orientation - and it is deliberately not scaled by `quality`, so
    // the transition between two materials never disappears on a low tier.
    if (edgeSides != 0 || cornerShapes != 0) {
        int edgeN = edgeSides & 3;
        int edgeE = (edgeSides >> 2) & 3;
        int edgeS = (edgeSides >> 4) & 3;
        int edgeW = (edgeSides >> 6) & 3;
        // Corner shapes clockwise from the top-right: NE, SE, SW, NW. Corner i
        // sits between side i and side (i + 1) % 4.
        int shapeNE = cornerShapes & 3;
        int shapeSE = (cornerShapes >> 2) & 3;
        int shapeSW = (cornerShapes >> 4) & 3;
        int shapeNW = (cornerShapes >> 6) & 3;

        // Distances from each side of the quad, 0 at the boundary and 1 at the
        // far side. vLocal.y runs south (-1) to north (+1), matching the mask bit
        // order in game::EdgeCategory.
        float dN = 1.0 - local01.y;
        float dE = 1.0 - local01.x;
        float dS = local01.y;
        float dW = local01.x;

        vec3 styleN = edgeStyle(edgeN);
        vec3 styleE = edgeStyle(edgeE);
        vec3 styleS = edgeStyle(edgeS);
        vec3 styleW = edgeStyle(edgeW);

        // Union of the per-side bands. Using max rather than a product is what
        // keeps a corner from becoming a dark square where two bands cross.
        //
        // `style.x == 0` is not a narrow band, it is *no* band: it is how a joined
        // side is expressed, and it is what keeps a field of one terrain
        // seamless. So the pixel floor is applied only to a band that exists,
        // and never resurrects one that does not - flooring a zero width would
        // put a stain on every shared edge in the world, which is the exact
        // failure the joined case exists to prevent.
        //
        // For a band that does exist the width is a fixed world distance, so a
        // tile small enough on screen makes the whole gradient narrower than a
        // pixel: the ramp then falls between samples, the band stops being a
        // gradient and becomes speckle, and the speckle beats against the
        // antialiased corner beside it. Flooring stretches the tail only - the
        // peak stays pinned to the boundary - so the transition grows toward the
        // interior, which is where there is still tile left to shade.
        float wN = styleN.x > 0.0 ? max(styleN.x, kBandMinPixels * quadPixel) : 0.0;
        float wE = styleE.x > 0.0 ? max(styleE.x, kBandMinPixels * quadPixel) : 0.0;
        float wS = styleS.x > 0.0 ? max(styleS.x, kBandMinPixels * quadPixel) : 0.0;
        float wW = styleW.x > 0.0 ? max(styleW.x, kBandMinPixels * quadPixel) : 0.0;

        float bandN = wN > 0.0 ? 1.0 - dN / wN : 0.0;
        float bandE = wE > 0.0 ? 1.0 - dE / wE : 0.0;
        float bandS = wS > 0.0 ? 1.0 - dS / wS : 0.0;
        float bandW = wW > 0.0 ? 1.0 - dW / wW : 0.0;

        // ---- A boundary is not a speck ----------------------------------
        //
        // The band on a side used to be scaled down when the tile had other
        // open sides: full strength with one or two, half with three, nothing
        // with four. The reason was that a tile surrounded on three or four
        // sides used to darken into a blob, and a field of such specks turned
        // every tile into a dark square - the lattice this whole file exists
        // to prevent.
        //
        // Two things have changed since. Nothing darkens a band any more
        // (every category's darkening term is zero), so there is no blob to
        // suppress; and the band on a side is now the treatment of the
        // BOUNDARY, which the two tiles sharing it agree on, so scaling it by
        // one tile's surroundings made the same seam read at two different
        // strengths - a boundary that was full on one side and half or absent
        // on the other.
        //
        // An isolated patch SHOULD have a margin: it is an island, and the
        // corner patch under it already draws it as one. So the band is drawn
        // at full strength on every open side, and the only thing that decides
        // how a boundary looks is the boundary itself.

        // ---- Hand the last radius of a side over to its corners -------------
        //
        // A side band is a straight strip of width w along one edge. Where two open
        // sides meet, their strips overlap in a *square* w x w at the vertex, so
        // the union is full strength over an area rather than along a line. That is
        // what the eye reads as a dark blob instead of a mitre, and because
        // vertices sit on the tile lattice, a field of mixed terrain turns those
        // blobs into a regular dark grid - the same lattice the seamless ground
        // work exists to destroy, arriving through the boundary instead of
        // through the texture.
        //
        // So each side band fades out over the corner radius of a corner that has
        // its own band to take over, and the corner's band - already shaped like
        // the silhouette - carries the boundary across. The two meet at zero
        // overlap, so there is nothing to double.
        //
        // `max` over the side's two corners, which is what keeps a straight run
        // completely untouched: a side is one tile long and two corner radii do
        // not add up to it, so at any point on the run at least one end still
        // reports full strength. Measured, the water and rock band checks in
        // Reverseworks.RenderSmoke are unchanged by this - which is the point,
        // since they exist to pin the straight-run profile.
        vec2 dNE = vec2(dE, dN);
        vec2 dSE = vec2(dE, dS);
        vec2 dSW = vec2(dW, dS);
        vec2 dNW = vec2(dW, dN);
        float hNE = cornerHandover(shapeNE, edgeN, edgeE, length(dNE));
        float hSE = cornerHandover(shapeSE, edgeE, edgeS, length(dSE));
        float hSW = cornerHandover(shapeSW, edgeS, edgeW, length(dSW));
        float hNW = cornerHandover(shapeNW, edgeW, edgeN, length(dNW));
        bandN *= max(hNW, hNE);
        bandE *= max(hNE, hSE);
        bandS *= max(hSE, hSW);
        bandW *= max(hSW, hNW);
        if (transitionMode == kTransitionBase) {
            int sides = int(vTransition.y + 0.5);
            if ((sides & 1) != 0) bandN *= 1.0 - transitionWidthScale(local01, 1);
            if ((sides & 2) != 0) bandE *= 1.0 - transitionWidthScale(local01, 0);
            if ((sides & 4) != 0) bandS *= 1.0 - transitionWidthScale(local01, 1);
            if ((sides & 8) != 0) bandW *= 1.0 - transitionWidthScale(local01, 0);
        }
        float band = max(max(bandN, bandE), max(bandS, bandW));

        // Per-side shade, so a rock face next to a path is harder than either
        // next to water. The darkest active side wins, which is what stops a
        // corner where a rock and a soil edge meet from averaging out.
        float shade = max(
            max(styleN.y * max(bandN, 0.0), styleE.y * max(bandE, 0.0)),
            max(styleS.y * max(bandS, 0.0), styleW.y * max(bandW, 0.0)));
        float bevel = max(
            max(styleN.z * max(bandN, 0.0), styleE.z * max(bandE, 0.0)),
            max(styleS.z * max(bandS, 0.0), styleW.z * max(bandW, 0.0)));

        // ---- Organic rounded corners --------------------------------------
        // A rounded spike is an anisotropic SDF corner. The patch underneath
        // carries no shape bits, so this tile's alpha is the only thing that
        // decides where the curve is and the patch simply shows through it. The
        // seed varies each tile's radius slightly without another vertex
        // attribute; Taper corners soften only their existing edge band.
        float pixelWidth = max(0.5 * quadPixel, 0.001);
        vec4 cNE = naturalCorner(shapeNE, edgeN, edgeE, 0, cornerStyle, dNE, bandN, pixelWidth);
        vec4 cSE = naturalCorner(shapeSE, edgeE, edgeS, 1, cornerStyle, dSE, bandE, pixelWidth);
        vec4 cSW = naturalCorner(shapeSW, edgeS, edgeW, 2, cornerStyle, dSW, bandS, pixelWidth);
        vec4 cNW = naturalCorner(shapeNW, edgeW, edgeN, 3, cornerStyle, dNW, bandW, pixelWidth);

        cornerCoverage *= cNE.x * cSE.x * cSW.x * cNW.x;
        // The corner's own band joins the sides', so the treatment follows the
        // curve and not only the two straight runs that meet it. Nothing is
        // scaled by the tile's surroundings: the boundary is one boundary, and
        // both of its sides already agree on what it looks like.
        band = max(band, max(max(cNE.y, cSE.y), max(cSW.y, cNW.y)));
        shade = max(shade, max(max(cNE.z, cSE.z), max(cSW.z, cNW.z)));
        bevel = max(bevel, max(max(cNE.w, cSE.w), max(cSW.w, cNW.w)));
        band = clamp(band, 0.0, 1.0);
        shade = clamp(shade, 0.0, 1.0);

        // Sides facing the light get a shallower band and sides facing away a
        // deeper one, so the boundary reads as a lit form rather than a line
        // drawn on the map. A side's inward normal is the direction the band
        // grows in, which is opposite to the direction the light comes from when
        // the boundary is turned away.
        //
        // Derived from a weighted blend of the open sides' inward normals rather
        // than from the difference of the four band values, and that is the second
        // half of the vertex fix. The difference form could not tell one open
        // side from two: at a vertex with north and east open it returned
        // `-(dir.x + dir.y)`, which for a light direction of (0.42, 0.68) is
        // -1.10, and the band was then made 31% *darker* at every such corner
        // than along either straight run. Because vertices are on the lattice,
        // that excess is precisely a dark grid drawn once per tile corner.
        //
        // A blend of unit vectors is a unit vector, so |facing| <= |light| and no
        // combination of open sides can exceed the modulation a single side gets.
        // It also gives the mitre between two open sides the diagonal normal it
        // should have, instead of whichever of the two happened to be larger.
        vec2 inward = vec2(0.0)
            + vec2(0.0, -1.0) * max(bandN, 0.0)
            + vec2(-1.0, 0.0) * max(bandE, 0.0)
            + vec2(0.0, 1.0) * max(bandS, 0.0)
            + vec2(1.0, 0.0) * max(bandW, 0.0);
        float inwardLength = length(inward);
        float facing = inwardLength > 1e-4
            ? dot(u_light_direction, inward / inwardLength)
            : 0.0;
        shade *= 1.0 - 0.22 * facing;

        color *= 1.0 - shade;
        // The band's own chroma, taken from whichever side is contributing it and
        // faded in and out with the band itself so the transition has no second
        // edge of its own. `band` is 1 on the boundary and 0 one band-width
        // inward, so this puts the neighbour's colour where the boundary is and
        // leaves the tile interior alone.
        //
        // Weighted and normalised rather than max'd per channel. Max is what the
        // shade and bevel above use, and it is right for those because each is a
        // single scalar competing for one axis: the hardest boundary wins. For a
        // colour it is not, because a vertex with rock to the north and soil to
        // the east takes the larger red from one and the larger blue from the
        // other and lands on a hue that is neither material. Dividing by the
        // weight instead mixes them, which is what a corner between two materials
        // actually is.
        vec3 tintWeight = vec3(0.0);
        float tintTotal = 0.0;
        tintWeight += edgeTint(edgeN) * max(bandN, 0.0);
        tintWeight += edgeTint(edgeE) * max(bandE, 0.0);
        tintWeight += edgeTint(edgeS) * max(bandS, 0.0);
        tintWeight += edgeTint(edgeW) * max(bandW, 0.0);
        tintTotal = max(bandN, 0.0) + max(bandE, 0.0) + max(bandS, 0.0) + max(bandW, 0.0);
        if (tintTotal > 0.0001) {
            color = mix(color, color * (tintWeight / tintTotal), band * kEdgeTintStrength);
        }
        // A thin bright line where the band rolls off: the lit lip of a raised
        // edge. `lip` peaks in the middle of the band and vanishes at both ends,
        // so the boundary keeps a highlight instead of only a shadow, and it is
        // strongest on the lit side.
        float lip = 4.0 * band * (1.0 - band);
        float litSide = clamp(0.5 - 0.5 * facing, 0.0, 1.0);
        color += vec3(0.055, 0.052, 0.044) * lip * bevel * 2.0 * litSide;
    }

    // Shadow quads are rendered into the screen-space shadow map in a
    // separate pass; they should not appear in the main lit pass.
    if (abs(kind - kKindShadow) < 0.5) {
        discard;
    }

    // ---- Screen-space shadow modulation ------------------------------------
    // The shadow map stores darkness (0.0 = lit), accumulated darkest-wins
    // so overlaps never compound. Modulate the lit colour before
    // ambient/fog so shadowed areas still receive the ambient fill.
    //
    // Shadow-casting sprites skip this step: their own silhouette is already in
    // the shadow map (the renderer shears a copy of the sprite to draw it), so
    // and other non-casters still receive the projected shadow.
    //
    // `shadow` survives past the ambient term below. Multiplying only the
    // albedo and then adding the full ambient back left the shadow at a
    // quarter of its nominal strength: a morning sun with 0.25 darkness and
    // ~0.35 of ambient fill measured a 12% luminance difference, which reads
    // as a dirty smudge rather than as shade. Scaling most of the fill with
    // the shadow keeps the darkest part of a shadow from going flat while
    // letting it actually read.
    float shadow = 1.0f;
    if (u_shadow_enabled > 0 && (packedFlags & kFlagCastShadow) == 0) {
        vec2 shadowUv = vAnchor * 0.5 + 0.5;
        shadow = 1.0f - texture(u_shadow_texture, shadowUv).r;
    }
    color *= shadow;

    // ---- Ambient light -----------------------------------------------------
    // Additive hemisphere term. Intensity can go above 1.0 to lift night or
    // overcast scenes; the colour shifts the shadow fill so it never reads as
    // flat grey. Applied before fog so fogged distant sprites still match the
    // aerial perspective.
    //
    // This term carries most of a ground sprite's brightness - the CPU tile
    // tint is faint and the albedo multiplier therefore moves little - so the
    // shadow has to reach the fill or it never reads. At 0.85 the skylight a
    // caster occludes is most of the light it blocks: the scene keeps a warm
    // floor under every shadow (see kShadowAmbientRate) and a 0.30 nominal
    // shadow lands at a 25% luminance drop instead of the 12% an unshadowed
    // fill used to leave, which read as a dirt smudge rather than as shade.
    //
    // A building takes its fill from the per-part response above instead, which
    // already carries this same colour and intensity and weights it per part;
    // adding the flat term on top would double it.
    color += u_ambient_color * u_ambient_intensity
        * mix(1.0, shadow, kShadowAmbientRate) * (isBuilding ? 0.0 : 1.0);

    // ---- Environment fog ---------------------------------------------------
    // Aerial perspective by WORLD distance from the camera focus: the
    // aspect-corrected viewport delta is divided by u_fogWorldScale so the
    // falloff is a property of the world, not of the current zoom. Contrast
    // drops before the hue shift. Applied pre-tonemap so fogged fragments roll
    // into the highlight shoulder like the rest of the frame instead of
    // clipping past it.
    if (u_fogDensity > 0.0001) {
        vec2 fogVec = (vAnchor - u_fogCenter) * vec2(u_aspect, 1.0) / u_fogWorldScale;
        float fogDist = length(fogVec);
        float fogFactor = clamp(1.0 - exp(-fogDist * fogDist * u_fogDensity * u_fogDensity), 0.0, 1.0);
        float fogLuma = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(color, vec3(fogLuma), fogFactor * kFogDesaturate);
        color = mix(color, u_fogColor, fogFactor);
    }

    // ---- Gameplay vignette -------------------------------------------------
    // Same radial metric as the fog above, so the focus sits on the player by
    // construction rather than on wherever a second center would drift to.
    // Runs after fog (darkens the haze too) and before tonemap (rolls into
    // the highlight shoulder instead of clipping past it).
    if (u_vignette > 0.0001) {
        vec2 vigVec = (vAnchor - u_fogCenter) * vec2(u_aspect, 1.0);
        float vig = smoothstep(0.45, 1.35, length(vigVec));
        color *= 1.0 - u_vignette * vig;
    }

    FragColor = vec4(toneMap(color), texelColor.a * vColor.a * cornerCoverage);
}

