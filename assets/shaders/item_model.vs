#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aShade;
layout (location = 3) in vec3 aNormal;  // not used in forward path; present for VAO compatibility

uniform mat4 model;
uniform mat4 viewProj;

out vec2 vUV;
out float vShade;

void main() {
	gl_Position = viewProj * model * vec4(aPos, 1.0);
	vUV = aUV;
	vShade = aShade;
}
