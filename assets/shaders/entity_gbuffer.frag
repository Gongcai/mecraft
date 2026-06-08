// Entity GBuffer fragment shader — Mecraft Phase 5.7 enhanced.
// Writes humanoid/mob entities into the 5-MRT GBuffer matching terrain layout.
// Entities use MATERIAL_SKIN with conservative PBR parameters.
// Phase 5.7: per-entity voxel light from CPU world light query replaces hardcoded fallback.

#version 450 core
#include "gbuffer_contract.glsl"

layout (location = 0) out vec4 GAlbedoMaterial;
layout (location = 1) out vec4 GNormalAo;
layout (location = 2) out vec4 GVoxelLight;
layout (location = 3) out vec4 GMaterial;
layout (location = 4) out vec4 GMaterialAux;
layout (location = 5) out vec2 FragPerObjectVelocity;

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vVelocity;

uniform sampler2D uTexture;
// Per-entity voxel light from CPU world light query (0-1 range, normalized from 0-15).
uniform float uEntitySunlight;
uniform float uEntityBlockLight;
uniform float uHurtFlash;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 skinColor = mix(texColor.rgb, vec3(1.0, 0.22, 0.22), clamp(uHurtFlash, 0.0, 1.0) * 0.70);
    vec3 albedo = srgbToLinear(skinColor);
    vec3 normal = normalize(vNormal);

    // Entity skin: no emissive, no vertex AO.
    float emissiveHint = 0.0;
    float vertexAo = 1.0;

    GAlbedoMaterial = vec4(albedo, emissiveHint);
    GNormalAo = vec4(normal * 0.5 + 0.5, vertexAo);
    GVoxelLight = vec4(clamp(uEntitySunlight, 0.0, 1.0), clamp(uEntityBlockLight, 0.0, 1.0), 0.0, 1.0);
    GMaterial = packGBufferMaterial(surfaceMaterialForKind(float(MATERIAL_SKIN), 0.0));
    GMaterialAux = packGBufferMaterialAux(surfaceMaterialAuxForKind(float(MATERIAL_SKIN)));

    // Per-object screen-space velocity for TAA/motion blur.
    FragPerObjectVelocity = vVelocity;
}
