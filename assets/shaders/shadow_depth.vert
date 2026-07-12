#version 450 core
#extension GL_ARB_shader_draw_parameters : require
#include "derivative_shadow.glsl"
#include "terrain_vertex_decode.glsl"

layout (location = 0) in vec3 aLegacyPos;
layout (location = 1) in vec2 aLegacyUV;
layout (location = 2) in float aLegacyNormal;
layout (location = 3) in float aLegacySunlight;
layout (location = 4) in float aLegacyBlockLight;
layout (location = 5) in float aLegacyAO;
layout (location = 6) in float aLegacyLayer;
layout (location = 7) in float aLegacyAnimationFrameCount;
layout (location = 8) in float aLegacyAnimationFps;
layout (location = 9) in float aLegacyAnimated;
layout (location = 10) in uint aLegacyTintPacked;
layout (location = 11) in uint aPackedPos;
layout (location = 12) in uint aPackedUV;
layout (location = 13) in uint aPackedLightAoLayer;
layout (location = 14) in uint aPackedTintAnim;

#ifdef RHI_TERRAIN_SHADOW_MDI
#include "terrain_shadow_params.glsl"
#else
uniform mat4 viewProj;
uniform mat4 uShadowModelView;
uniform mat4 uShadowProjection;
uniform mat4 uShadowProjectionInverse;
uniform mat4 model;
uniform int uUseModel;
uniform int uVertexFormat;
#endif

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vLayer;
layout(location = 2) out float vAnimationFrameCount;
layout(location = 3) out float vAnimationFps;
layout(location = 4) out float vAnimated;
layout(location = 5) out float vNormal;
layout(location = 6) out vec3 vWorldPos;
layout(location = 7) flat out int vMaterialKind;
layout(location = 8) out float vSkylight;
layout(location = 9) flat out float vTintKind;
layout(location = 10) out vec2 vTintUV;

void main() {
#ifdef RHI_TERRAIN_SHADOW_MDI
    TerrainVertexDecoded vertex = decodeTerrainPackedVertex(
        aPackedPos,
        aPackedUV,
        aPackedLightAoLayer,
        aPackedTintAnim,
        gl_BaseInstanceARB);
    vec4 worldPos = vec4(vertex.pos, 1.0);
#else
    TerrainVertexDecoded vertex = (uVertexFormat == 1)
        ? decodeTerrainPackedVertex(aPackedPos, aPackedUV, aPackedLightAoLayer, aPackedTintAnim, gl_BaseInstanceARB)
        : decodeLegacyBlockVertex(aLegacyPos, aLegacyUV, aLegacyNormal, aLegacySunlight, aLegacyBlockLight,
                                  aLegacyAO, aLegacyLayer, aLegacyAnimationFrameCount, aLegacyAnimationFps,
                                  aLegacyAnimated, aLegacyTintPacked);
    vec4 localPos = vec4(vertex.pos, 1.0);
    vec4 worldPos = (uUseModel != 0) ? model * localPos : localPos;
#endif

    // Mecraft CSM: linear projection, no distortion warp.
    vec3 viewPos = mat3(uShadowModelView) * worldPos.xyz + uShadowModelView[3].xyz;
    vec3 clipPos = vec3(uShadowProjection[0].x, uShadowProjection[1].y, uShadowProjection[2].z) * viewPos + uShadowProjection[3].xyz;
    gl_Position = vec4(clipPos, 1.0);
    vUV = vertex.uv;
    vLayer = vertex.layer;
    vAnimationFrameCount = vertex.animationFrameCount;
    vAnimationFps = vertex.animationFps;
    vAnimated = vertex.animated;
    vNormal = vertex.normal;
    vWorldPos = worldPos.xyz;

    // Extract DerivativeMain material id from packed tint data.
    uint materialKind = (vertex.tintPacked >> 8u) & 63u;
    vMaterialKind = int(materialKind);

    // Skylight from vertex attribute (DerivativeMain Shadow.frag:83 — shadowcolor1Out.z = lightmap.y)
    vSkylight = vertex.sunlight;

    // DerivativeMain Shadow.frag:76,78 — shadowcolor0Out = albedo.rgb * tint
    // Mecraft uses tint colormap lookup (grass/foliage) instead of vertex color.
    uint tintKind = (vertex.tintPacked >> 14u) & 3u;
    uint tintU = (vertex.tintPacked >> 4u) & 15u;
    uint tintV = vertex.tintPacked & 15u;
    vTintKind = float(tintKind);
    vTintUV = (vec2(float(tintU), float(tintV)) + vec2(0.5)) / 16.0;
}
