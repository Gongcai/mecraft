// Item model vertex shader — Mecraft Phase 5.4 enhanced.
// Vertex layout: pos(3f), uv(2f), shade(1f), normal(3f).
// Phase 5.4: now passes vWorldPos and vNormal for shadow sampling.

#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aShade;
layout (location = 3) in vec3 aNormal;

uniform mat4 model;
uniform mat4 viewProj;

out vec2 vUV;
out float vShade;
out vec3 vWorldPos;
out vec3 vNormal;

void main() {
	vec4 worldPos = model * vec4(aPos, 1.0);
	gl_Position = viewProj * worldPos;
	vUV = aUV;
	vShade = aShade;
	vWorldPos = worldPos.xyz;
	vNormal = normalize(mat3(model) * aNormal);
}
