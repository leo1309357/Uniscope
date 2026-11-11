#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D u_scene;
uniform vec2 u_texel;       // 1 / low-res scene size
uniform float u_sharpness;  // 0 = plain bilinear upscale, 0..1 RCAS-lite peaking (bilinear path only)
uniform int u_bicubic;      // 1 = Catmull-Rom bicubic reconstruction, 0 = bilinear

// Catmull-Rom weights for the four neighbours at offsets -1, 0, 1, 2 along one
// axis; t is the fractional texel position in [0, 1). The spline interpolates
// the sample position through the four surrounding texels, which keeps slanted
// world edges from turning into a staircase when the world pass rendered below
// native resolution.
vec4 crWeights(float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4(
        0.5 * (-t3 + 2.0 * t2 - t),
        0.5 * (3.0 * t3 - 5.0 * t2 + 2.0),
        0.5 * (-3.0 * t3 + 4.0 * t2 + t),
        0.5 * (t3 - t2));
}

// Separable 4x4 Catmull-Rom (16 texture fetches). Provides true bicubic
// reconstruction without the ringing of a naive 4x4 box filter.
vec3 sampleBicubic(vec2 uv) {
    vec2 p = uv / u_texel;
    vec2 base = floor(p - 0.5) + 0.5;
    vec2 f = p - base;
    vec4 wx = crWeights(f.x);
    vec4 wy = crWeights(f.y);
    vec2 rowStart = (base - 1.0) * u_texel;

    vec3 col = vec3(0.0);
    for (int j = 0; j < 4; ++j) {
        vec3 row = vec3(0.0);
        for (int i = 0; i < 4; ++i) {
            vec2 offset = vec2(float(i - 1), float(j - 1)) * u_texel;
            row += texture(u_scene, rowStart + offset).rgb * wx[i];
        }
        col += row * wy[j];
    }
    return col;
}

void main() {
    vec3 color;
    if (u_bicubic == 1) {
        color = sampleBicubic(vTexCoord);
    } else {
        color = texture(u_scene, vTexCoord).rgb;
        if (u_sharpness > 0.001) {
            // 4-tap cross around the center texel, clamped to the neighbour box
            // so the peaking can never ring past a real edge in the low-res
            // image. This is the Low-tier path: cheap, with a light sharpening.
            vec3 north = texture(u_scene, vTexCoord + vec2(0.0, u_texel.y)).rgb;
            vec3 south = texture(u_scene, vTexCoord - vec2(0.0, u_texel.y)).rgb;
            vec3 east  = texture(u_scene, vTexCoord + vec2(u_texel.x, 0.0)).rgb;
            vec3 west  = texture(u_scene, vTexCoord - vec2(u_texel.x, 0.0)).rgb;
            vec3 lobe = north + south + east + west;
            vec3 boxMin = min(min(north, south), min(east, west));
            vec3 boxMax = max(max(north, south), max(east, west));
            color = clamp(
                color + (color - lobe * 0.25) * u_sharpness * 0.6,
                boxMin,
                boxMax);
        }
    }

    // The scene shader already tonemapped before writing into this target;
    // applying the curve again here would compress the highlights a second
    // time and push the world toward grey. The upscale pass only reconstructs
    // the pixels, it does not re-grade them.
    FragColor = vec4(color, 1.0);
}