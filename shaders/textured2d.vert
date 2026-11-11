#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform float u_position_x;
uniform float u_position_y;
uniform float u_half_width;
uniform float u_half_height;
uniform float u_aspect;

out vec2 vTexCoord;
out vec2 vLocal;
out vec2 vScreen;

void main() {
    vec2 position = aPos.xy * vec2(u_half_width, u_half_height) + vec2(u_position_x, u_position_y);
    position.x /= u_aspect;

    gl_Position = vec4(position, aPos.z, 1.0);
    vTexCoord = aTexCoord;
    vLocal = aPos.xy;
    vScreen = position;
}
