#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aLayer;
layout (location = 3) in float aAlpha;
layout (location = 4) in float aGrassTintFactor;

uniform mat4 viewProj;

out vec2 vUV;
out float vLayer;
out float vAlpha;
out float vGrassTintFactor;

void main() {
    gl_Position = viewProj * vec4(aPos, 1.0);
    vUV = aUV;
    vLayer = aLayer;
    vAlpha = aAlpha;
    vGrassTintFactor = aGrassTintFactor;
}
