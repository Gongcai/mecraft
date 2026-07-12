#version 450 core
#include "gbuffer_contract.glsl"
layout(location = 0) out vec4 gAlbedoMaterial;
layout(location = 1) out vec4 gNormalAo;
layout(location = 2) out vec4 gVoxelLight;
layout(location = 3) out vec4 gMaterial;
layout(location = 4) out vec4 gMaterialAux;
layout(location = 5) out vec2 gVelocity;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vVelocity;
layout(binding = 0) uniform sampler2D uTexture;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uPreviousViewProj;
    mat4 uModel;
    mat4 uPreviousModel;
    vec4 uLightHurt;
};
void main() {
    vec4 texel = texture(uTexture, vUv);
    if (texel.a < 0.1) discard;
    vec3 skinColor = mix(texel.rgb, vec3(1.0, 0.22, 0.22),
                         clamp(uLightHurt.z, 0.0, 1.0) * 0.70);
    vec3 albedo = pow(max(skinColor, vec3(0.0)), vec3(2.2));
    gAlbedoMaterial = vec4(albedo, 0.0);
    gNormalAo = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
    gVoxelLight = vec4(clamp(uLightHurt.xy, 0.0, 1.0), 0.0, 1.0);
    gMaterial = packGBufferMaterial(surfaceMaterialForKind(float(MATERIAL_SKIN), 0.0));
    gMaterialAux = packGBufferMaterialAux(surfaceMaterialAuxForKind(float(MATERIAL_SKIN)));
    gVelocity = vVelocity;
}
