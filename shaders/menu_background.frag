#version 330 core
in vec2 vUv;
out vec4 FragColor;

#include "lib/common.glsl"

uniform vec3 u_background;
uniform vec3 u_accent;
uniform vec3 u_panel;
uniform float u_time;
uniform float u_aspect;
uniform float u_effect_quality;

float lineMask(float value, float scale, float width) {
	float distanceToLine = abs(fract(value * scale - 0.5) - 0.5);
	return 1.0 - smoothstep(width, width + 0.012, distanceToLine);
}

void main() {
	float quality = clamp(u_effect_quality, 0.0, 1.0);
	vec2 p = vUv * 2.0 - 1.0;
	vec2 centered = vec2(p.x * u_aspect, p.y);

	float horizon = smoothstep(-0.82, 0.90, p.y);
	vec3 color = mix(u_background * 0.70, u_panel * 0.76, horizon);

	float upperGlow = exp(-dot(vec2(centered.x * 0.42, (p.y - 0.56) * 1.35), vec2(centered.x * 0.42, (p.y - 0.56) * 1.35)) * 1.25);
	float horizonGlow = exp(-dot(vec2(centered.x * 0.20, (p.y + 0.12) * 1.8), vec2(centered.x * 0.20, (p.y + 0.12) * 1.8)) * 1.8);
	color += u_accent * (upperGlow * 0.095 + horizonGlow * 0.055) * quality;

	float perspective = 1.0 / max(p.y + 1.08, 0.12);
	vec2 gridPoint = vec2(centered.x * perspective * 0.72, perspective * 0.34);
	float grid = max(lineMask(gridPoint.x, 7.0, 0.025), lineMask(gridPoint.y, 8.0, 0.025));
	float lowerRegion = 1.0 - smoothstep(-0.78, -0.18, p.y);
	color = mix(color, u_accent * 0.34 + u_background * 0.66, grid * lowerRegion * 0.24 * quality);

	float scanPosition = fract(u_time * 0.035) * 2.8 - 1.4;
	float scan = exp(-abs(p.y - scanPosition) * 34.0);
	color += u_accent * scan * 0.035 * quality;

	float vignette = 1.0 - smoothstep(0.18, 1.55, dot(centered, centered));
	color *= mix(0.62, 1.0, vignette);
	color += (hash12(gl_FragCoord.xy + u_time) - 0.5) * 0.006 * quality;

	FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
