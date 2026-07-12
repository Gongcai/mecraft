#version 450 core
#include "gbuffer_contract.glsl"
layout(location = 0) out vec4 gAlbedoMaterial;
layout(location = 1) out vec4 gNormalAo;
layout(location = 2) out vec4 gVoxelLight;
layout(location = 3) out vec4 gMaterial;
layout(location = 4) out vec4 gMaterialAux;
layout(location = 5) out vec2 gVelocity;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vVertexData;
layout(location = 2) in vec4 vAnimationData;
layout(location = 3) flat in uvec2 vMaterialTint;
layout(location = 4) in vec2 vTintUv;
layout(location = 5) in vec3 vWorldPosition;
layout(location = 6) in vec2 vVelocity;
layout(binding = 0) uniform sampler2DArray uTextureArray;
layout(binding = 1) uniform sampler2D uGrassColormap;
layout(binding = 2) uniform sampler2D uFoliageColormap;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uPreviousViewProj;
    mat4 uModel;
    mat4 uPreviousModel;
    vec4 uLightAnimation;
};
vec3 srgbToLinear(vec3 color) { return pow(max(color, vec3(0.0)), vec3(2.2)); }
vec3 redstoneTintSrgb(vec2 tintUv) {
    float power = clamp(floor(tintUv.x * 16.0), 0.0, 15.0) / 15.0;
    int tint = int(clamp(floor(tintUv.y * 16.0), 0.0, 15.0));
    const vec3 low[16] = vec3[16](
        vec3(.30,0,0),vec3(0,.05,.30),vec3(0,.22,.03),vec3(.22,.18,0),
        vec3(.18,0,.28),vec3(0,.20,.24),vec3(.28,.09,0),vec3(.22),
        vec3(.35,.02,.12),vec3(.10,.20,.36),vec3(.04,.28,.17),vec3(.32,.24,.04),
        vec3(.25,.07,.34),vec3(.02,.30,.30),vec3(.32,.16,.08),vec3(.36));
    const vec3 high[16] = vec3[16](
        vec3(1,.10,.02),vec3(.08,.35,1),vec3(.08,.95,.18),vec3(1,.86,.08),
        vec3(.78,.18,1),vec3(.05,.92,1),vec3(1,.38,.05),vec3(.82),
        vec3(1,.18,.42),vec3(.35,.62,1),vec3(.18,1,.62),vec3(1,.74,.20),
        vec3(.82,.40,1),vec3(.25,1,.92),vec3(1,.56,.25),vec3(1));
    return mix(low[tint], high[tint], power);
}
vec3 decodeFaceNormal(float face) {
    if (face > -2.5 && face < -0.5) return vec3(0.0, 1.0, 0.0);
    int index = int(round(face));
    if (index == 0) return vec3(0.0, 1.0, 0.0);
    if (index == 1) return vec3(0.0, -1.0, 0.0);
    if (index == 2) return vec3(0.0, 0.0, 1.0);
    if (index == 3) return vec3(0.0, 0.0, -1.0);
    if (index == 4) return vec3(-1.0, 0.0, 0.0);
    return vec3(1.0, 0.0, 0.0);
}
void main() {
    float normalMarker = vVertexData.w;
    bool crossVegetation = normalMarker > -2.5 && normalMarker < -0.5;
    float layer = vAnimationData.x;
    if (vAnimationData.w > 0.5 && vAnimationData.y > 1.0 && vAnimationData.z > 0.0) {
        layer += mod(floor(uLightAnimation.z * vAnimationData.z), vAnimationData.y);
    }
    vec4 texel = crossVegetation
        ? textureLod(uTextureArray, vec3(vUv, layer), 0.0)
        : texture(uTextureArray, vec3(vUv, layer));
    if (texel.a < 0.1) discard;
    vec3 albedo = srgbToLinear(texel.rgb);
    if (vMaterialTint.y == 1u) albedo *= srgbToLinear(texture(uGrassColormap, vTintUv).rgb);
    else if (vMaterialTint.y == 2u) albedo *= srgbToLinear(texture(uFoliageColormap, vTintUv).rgb);
    else if (vMaterialTint.y == 3u) albedo *= srgbToLinear(redstoneTintSrgb(vTintUv));
    vec3 normal = crossVegetation ? vec3(0.0, 1.0, 0.0) : decodeFaceNormal(normalMarker);
    float ao = clamp(vVertexData.z / 3.0, 0.0, 1.0);
    int derivativeMaterialId = derivativeFragmentMaterialId(materialKindId(float(vMaterialTint.x)));
    bool emissiveMaterial = isDerivativeEmissiveMaterialId(derivativeMaterialId) ||
                            derivativeMaterialId == MATERIAL_ORE || derivativeMaterialId == MATERIAL_NETHER_ORE;
    float peak = max(max(albedo.r, albedo.g), albedo.b);
    float luma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float emissive = emissiveMaterial
        ? smoothstep(0.34, 0.72, max(luma, peak * 0.72)) * clamp(uLightAnimation.y * 1.25, 0.0, 1.0)
        : 0.0;
    gAlbedoMaterial = vec4(albedo, emissive);
    gNormalAo = vec4(normal * 0.5 + 0.5, ao);
    gVoxelLight = vec4(clamp(uLightAnimation.x, 0.0, 1.0), clamp(uLightAnimation.y, 0.0, 1.0), 0.0, 1.0);
    gMaterial = packGBufferMaterial(surfaceMaterialForKind(float(vMaterialTint.x), emissive));
    gMaterialAux = packGBufferMaterialAux(surfaceMaterialAuxForKind(float(vMaterialTint.x)));
    gVelocity = vVelocity;
}
