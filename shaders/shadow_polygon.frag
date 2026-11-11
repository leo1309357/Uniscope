#version 330 core
in float vAlpha;

out vec4 FragColor;

uniform float u_shadow_alpha;

void main() {
    // Darkness, matching shadowmap.frag: accumulated with a MAX blend so
    // overlapping casters never compound. See renderShadowMapPass().
    float dark = vAlpha * u_shadow_alpha;
    FragColor = vec4(dark, dark, dark, 1.0);
}
