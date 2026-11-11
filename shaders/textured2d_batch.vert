#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;
layout (location = 3) in vec2 aLocal;
layout (location = 4) in vec3 aBlendColor;
layout (location = 5) in vec4 aEdgeBlend;
layout (location = 6) in vec4 aMaterialParams; // x=kind, y=wetness, z=emissive, w=detailRot
layout (location = 7) in vec3 aNextFrame;
layout (location = 8) in vec4 aDetail;         // x=scale, y=strength, z=offsetU, w=offsetV
layout (location = 9) in float aMaterialFlags;
// How much of this sprite is under casters this frame. Only the per-part
// material response (MaterialKind2D::BuildingSurface) reads it; every other
// kind ignores it, so it rides its own attribute rather than sharing one.
layout (location = 10) in float aOccluded;
// Building texture array layer, -1 when the sprite samples the regular atlas.
layout (location = 11) in float aBuildingArrayLayer;
// Ground transition overlay: mode, cross-boundary axis / base side mask,
// low-frequency weight perturbation, and world-tile tangent origin.
layout (location = 12) in vec4 aTransition;

out vec2 vTexCoord;
out vec4 vColor;
out vec2 vLocal;
out vec3 vBlendColor;
out vec4 vEdgeBlend;
out vec4 vMaterialParams;
out vec2 vNextTexCoord;
out float vFrameBlend;
out vec2 vAnchor;
out vec4 vDetail;
out float vMaterialFlags;
out float vOccluded;
out float vBuildingArrayLayer;
out vec4 vTransition;

void main() {
    gl_Position = vec4(aPos, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vLocal = aLocal;
    vBlendColor = aBlendColor;
    vEdgeBlend = aEdgeBlend;
    vMaterialParams = aMaterialParams;
    vNextTexCoord = aNextFrame.xy;
    vFrameBlend = aNextFrame.z;
    // Clip-space position expressed in height-normalized units. Because the
    // camera maps world space to this space linearly, sampling the detail map
    // with it makes the detail continuous across neighbouring quads instead
    // of repeating on the quad's own period, which is what removes the
    // repeating-tile grid.
    //
    // On its own that also anchors the field to the CAMERA: the field then
    // stays put on screen while the ground slides under it. The shader
    // subtracts u_detail_origin, so what reaches the sampler is a world
    // position and the field is pinned to the world instead.
    vAnchor = aPos.xy;
    vDetail = aDetail;
    vMaterialFlags = aMaterialFlags;
    vOccluded = aOccluded;
    vBuildingArrayLayer = aBuildingArrayLayer;
    vTransition = aTransition;
}
