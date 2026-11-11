#version 330 core
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D u_texture;
uniform vec2 u_texel;
uniform vec2 u_direction;
// Scale factor from quality preset (0.5 for Low/Medium, 1.0 for High/Ultra).
// At half-res the blur covers more world-space pixels, so we widen the
// Gaussian to keep visual softness constant across tiers.
uniform float u_shadowResolutionScale;

void main() {
    // upscale.vert flips the v coordinate for image textures; the shadow map
    // is rendered in OpenGL screen space, so flip it back before sampling.
    vec2 uv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);

    // Gaussian sigma in texels, scaled so a half-res shadow map gets the same
    // world-space blur radius as a full-res one. At scale=0.5 we double sigma.
    float invScale = 1.0 / max(u_shadowResolutionScale, 0.25);
    float sigma = 2.0 * invScale;       // ~2 texels at full res
    float sigma2 = 2.0 * sigma * sigma; // 2*sigma^2 for the Gaussian formula

    // Dynamic 9-tap weights derived from the scaled sigma. This avoids
    // pre-baking separate kernels per tier and automatically adapts to any
    // scale the preset provides.
    float weights[5];
    float sum = 0.0;
    for (int i = 0; i < 5; ++i) {
        float x = float(i);
        weights[i] = exp(-x * x / sigma2);
        sum += (i == 0) ? weights[i] : 2.0 * weights[i];
    }
    for (int i = 0; i < 5; ++i) {
        weights[i] /= sum;
    }

    vec3 result = texture(u_texture, uv).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = u_texel * u_direction * float(i);
        result += texture(u_texture, uv + offset).rgb * weights[i];
        result += texture(u_texture, uv - offset).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}