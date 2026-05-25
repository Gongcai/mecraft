// Item drop GBuffer fragment shader — Mecraft Phase 5.3 extension.
// Renders dropped non-block items into the 5-MRT GBuffer.
// Items use a 2D atlas texture (not array). Per-face normals are provided
// via vertex attribute from the extruded mesh. Material is MATERIAL_DEFAULT
// with conservative PBR parameters.

#version 450 core
#include "gbuffer_contract.glsl"

layout (location = 0) out vec4 GAlbedoMaterial;
layout (location = 1) out vec4 GNormalAo;
layout (location = 2) out vec4 GVoxelLight;
layout (location = 3) out vec4 GMaterial;
layout (location = 4) out vec4 GMaterialAux;
layout (location = 5) out vec2 FragPerObjectVelocity;

in vec2 vUV;
in float vShade;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vVelocity;

uniform sampler2D uAtlas;
// Per-drop voxel light from CPU world light query (0-1 range, normalized from 0-15).
uniform float uDropSunlight;
uniform float uDropBlockLight;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(uAtlas, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 albedo = srgbToLinear(texColor.rgb);
    vec3 normal = normalize(vNormal);
    float vertexAo = 1.0;

    GAlbedoMaterial = vec4(albedo, 0.0);
    GNormalAo = vec4(normal * 0.5 + 0.5, vertexAo);
    GVoxelLight = vec4(clamp(uDropSunlight, 0.0, 1.0), clamp(uDropBlockLight, 0.0, 1.0), 0.0, 1.0);
    GMaterial = packGBufferMaterial(surfaceMaterialForKind(float(MATERIAL_DEFAULT), 0.0));
    GMaterialAux = packGBufferMaterialAux(surfaceMaterialAuxForKind(float(MATERIAL_DEFAULT)));

    // Per-object screen-space velocity for TAA/motion blur.
    FragPerObjectVelocity = vVelocity;
}
