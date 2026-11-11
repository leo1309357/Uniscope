#version 330 core
in vec2 vUv;
out vec4 FragColor;

// The white-out: a light that starts at the centre of the screen and pushes
// the darkness out to the edges, the way morning pushes fog aside.
//
// The radius is where the light's edge is, in the same units as the screen's
// half-height (1.0 is the top edge). The intensity is how bright the middle
// is: at 1.0 the middle is blown out to pure white, which is what an
// overexposed morning looks like.
//
// The light is white with the intensity as its alpha, so the world shows
// through it as it thins out - the scene comes back through the glow.

uniform float u_radius;
uniform float u_intensity;
uniform float u_aspect;

void main() {
	vec2 p = vUv * 2.0 - 1.0;
	vec2 centered = vec2(p.x * u_aspect, p.y);

	// The distance from the centre, in screen units.
	float distance = length(centered);

	// A soft edge: the light does not stop at the radius, it thins out.
	// The softness grows as the light grows, so the bloom opens rather than
	// snapping.
	float softness = max(0.08, u_radius * 0.55);
	float light = 1.0 - smoothstep(u_radius * 0.35, u_radius + softness, distance);

	// The core saturates first: the middle blows out before the edges do.
	float core = smoothstep(0.0, 1.0, u_radius);
	float brightness = u_intensity * mix(0.30, 1.0, light) * mix(0.55, 1.0, core);

	FragColor = vec4(vec3(1.0), clamp(brightness, 0.0, 1.0));
}
