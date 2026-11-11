#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
in vec2 vLocal;
in vec3 vBlendColor;
in vec4 vEdgeBlend;
in vec4 vMaterialParams;
in vec2 vNextTexCoord;
in float vFrameBlend;
in vec2 vAnchor;
in vec4 vDetail;
in float vMaterialFlags;

out vec4 FragColor;

uniform sampler2D u_texture;

const float kKindShadow = 7.0;

void main() {
    float kind = vMaterialParams.x;
    // Only shadow-casting quads contribute to the shadow map.
    if (abs(kind - kKindShadow) >= 0.5) {
        discard;
    }

    vec4 texelColor = texture(u_texture, vTexCoord);
    float coverage = texelColor.a * vColor.a;
    // The shadow map stores darkness: 0.0 = fully lit, 1.0 = fully shadowed
    // (vColor.a already carries the frame shadow alpha). Accumulated with a
    // MAX blend so overlapping casters take the darkest single value instead
    // of multiplying toward black; the lit pass applies (1 - darkness).
    FragColor = vec4(coverage, coverage, coverage, 1.0);
}
