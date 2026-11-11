#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in float aAlpha;

out float vAlpha;

uniform float u_aspect;

void main() {
    // aPos is in the same viewport space used by the sprite batch; divide x by
    // aspect to get clip-space coordinates that match the rest of the scene.
    gl_Position = vec4(aPos.x / max(u_aspect, 0.0001), aPos.y, 0.0, 1.0);
    vAlpha = aAlpha;
}
