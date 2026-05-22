// Entity GBuffer fragment shader — Mecraft extension.
// Writes humanoid/mob entities into the 5-MRT GBuffer matching terrain layout.
// Entities use MATERIAL_SKIN with conservative PBR parameters.
// Includes gbuffer_contract.glsl for pack/unpack functions and material definitions.

#version 450 core
#include "gbuffer_contract.glsl"

layout (location = 0) out vec4 GAlbedoMaterial;
layout (location = 1) out vec4 GNormalAo;
layout (location = 2) out vec4 GVoxelLight;
layout (location = 3) out vec4 GMaterial;
layout (location = 4) out vec4 GMaterialAux;

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 albedo = srgbToLinear(texColor.rgb);
    vec3 normal = normalize(vNormal);

    // Entity skin: no emissive, no vertex AO.
    float emissiveHint = 0.0;
    float vertexAo = 1.0;
    // TODO(P2): Entities have no voxel light data. Setting skylight=0.5 as a
    // conservative fallback so entities aren't pitch-black in caves. Proper fix:
    // sample world light cache at entity position on CPU and pass as uniform,
    // or sample light texture in this shader using worldPos.
    float skyLightFallback = 0.5;
    float blockLightFallback = 0.0;

    GAlbedoMaterial = vec4(albedo, emissiveHint);
    GNormalAo = vec4(normal * 0.5 + 0.5, vertexAo);
    GVoxelLight = vec4(skyLightFallback, blockLightFallback, 0.0, 1.0);
    GMaterial = packGBufferMaterial(surfaceMaterialForKind(float(MATERIAL_SKIN), 0.0));
    GMaterialAux = packGBufferMaterialAux(surfaceMaterialAuxForKind(float(MATERIAL_SKIN)));
}
