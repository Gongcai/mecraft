// Particle scene-resolved vertex shader — Mecraft Phase 5.5.
// Renders block-break particles into SceneResolved before volumetric fog
// composite so the fog pass applies atmospheric scattering uniformly.
// This is NOT a GBuffer MRT pass — it writes a single RGBA target with
// alpha blending. Same billboard vertex format as particle.vs but forwards
// world position for voxel light sampling.

#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aLayer;
layout (location = 3) in float aAlpha;
layout (location = 4) in float aBiomeTintFactor;

uniform mat4 viewProj;

out vec2 vUV;
out float vLayer;
out float vAlpha;
out float vBiomeTintFactor;
out vec3 vWorldPos;

void main() {
    gl_Position = viewProj * vec4(aPos, 1.0);
    vUV = aUV;
    vLayer = aLayer;
    vAlpha = aAlpha;
    vBiomeTintFactor = aBiomeTintFactor;
    vWorldPos = aPos;
}
