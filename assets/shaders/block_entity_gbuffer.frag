// Instanced block entity GBuffer fragment shader.
// Static block entity meshes write zero per-object velocity.

#version 450 core
#include "gbuffer_contract.glsl"

layout(location = 0) out vec4 GAlbedoMaterial;
layout(location = 1) out vec4 GNormalAo;
layout(location = 2) out vec4 GVoxelLight;
layout(location = 3) out vec4 GMaterial;
layout(location = 4) out vec4 GMaterialAux;
layout(location = 5) out vec2 FragPerObjectVelocity;

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vEntityLight;

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

    GAlbedoMaterial = vec4(albedo, 0.0);
    GNormalAo = vec4(normal * 0.5 + 0.5, 1.0);
    GVoxelLight = vec4(clamp(vEntityLight.x, 0.0, 1.0), clamp(vEntityLight.y, 0.0, 1.0), 0.0, 1.0);
    GMaterial = packGBufferMaterial(surfaceMaterialForKind(float(MATERIAL_SKIN), 0.0));
    GMaterialAux = packGBufferMaterialAux(surfaceMaterialAuxForKind(float(MATERIAL_SKIN)));
    FragPerObjectVelocity = vec2(0.0);
}
