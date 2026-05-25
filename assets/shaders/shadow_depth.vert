#version 450 core
#include "derivative_shadow.glsl"

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;
layout (location = 3) in float aSunlight;
layout (location = 4) in float aBlockLight;
layout (location = 5) in float aAO;
layout (location = 6) in float aLayer;
layout (location = 7) in float aAnimationFrameCount;
layout (location = 8) in float aAnimationFps;
layout (location = 9) in float aAnimated;
layout (location = 10) in uint aTintPacked;

uniform mat4 viewProj;
uniform mat4 uShadowModelView;
uniform mat4 uShadowProjection;
uniform mat4 uShadowProjectionInverse;
uniform mat4 model;
uniform int uUseModel;

out vec2 vUV;
out float vLayer;
out float vAnimationFrameCount;
out float vAnimationFps;
out float vAnimated;
out float vNormal;
out vec3 vWorldPos;
flat out int vMaterialKind;
out float vSkylight;
flat out float vTintKind;
out vec2 vTintUV;

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    vec4 worldPos = (uUseModel != 0) ? model * localPos : localPos;

    // Mecraft CSM: linear projection, no distortion warp.
    vec3 viewPos = mat3(uShadowModelView) * worldPos.xyz + uShadowModelView[3].xyz;
    vec3 clipPos = vec3(uShadowProjection[0].x, uShadowProjection[1].y, uShadowProjection[2].z) * viewPos + uShadowProjection[3].xyz;
    gl_Position = vec4(clipPos, 1.0);
    vUV = aUV;
    vLayer = aLayer;
    vAnimationFrameCount = aAnimationFrameCount;
    vAnimationFps = aAnimationFps;
    vAnimated = aAnimated;
    vNormal = aNormal;
    vWorldPos = worldPos.xyz;

    // Extract DerivativeMain material id from packed tint data.
    uint materialKind = (aTintPacked >> 8u) & 63u;
    vMaterialKind = int(materialKind);

    // Skylight from vertex attribute (DerivativeMain Shadow.frag:83 — shadowcolor1Out.z = lightmap.y)
    vSkylight = aSunlight;

    // DerivativeMain Shadow.frag:76,78 — shadowcolor0Out = albedo.rgb * tint
    // Mecraft uses tint colormap lookup (grass/foliage) instead of vertex color.
    uint tintKind = (aTintPacked >> 14u) & 3u;
    uint tintU = (aTintPacked >> 4u) & 15u;
    uint tintV = aTintPacked & 15u;
    vTintKind = float(tintKind);
    vTintUV = (vec2(float(tintU), float(tintV)) + vec2(0.5)) / 16.0;
}
