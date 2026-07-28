#version 450 core
// Reflection temporal reprojection. Reads velocity to reproject previous frame's
// temporally-resolved reflection, applies 3x3 neighborhood clamp and
// depth/normal/roughness-based disocclusion rejection to prevent ghosting.
// Operates on RGBA16F DerivativeMain-style reflection data:
//   opaque:      rgb = reflected radiance * specular, a = trace distance/filter data
//   translucent: rgb = reflected radiance * specular, a = scene pass-through

#include "gbuffer_contract.glsl"
#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uCurrentTex;
layout(binding = 1) uniform sampler2D uHistoryTex;
layout(binding = 2) uniform sampler2D uVelocityTex;
layout(binding = 3) uniform sampler2D uDepthTex;
layout(binding = 4) uniform sampler2D uNormalAoTex;
layout(binding = 5) uniform sampler2D uMaterialTex;
layout(binding = 6) uniform sampler2D uMaterialAuxTex;

layout(push_constant) uniform RhiPushConstants {
    vec2 uScreenSize;
    float uHistoryWeight;
    float uNear;
};

vec3 reconstructNormal(vec4 packedNormalAo) {
    return unpackGBufferNormal(packedNormalAo);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);

    vec4 current = texelFetch(uCurrentTex, texel, 0);
    float depth = texelFetch(uDepthTex, texel, 0).r;

    // Sky pixels: pass through (no temporal accumulation for sky reflections)
    if (depth >= 0.9999) {
        FragColor = current;
        return;
    }

    SurfaceMaterialAux centerAux = unpackGBufferMaterialAux(texelFetch(uMaterialAuxTex, texel, 0));
    TranslucentMask centerTransMask = decodeTranslucentMask(centerAux.materialKind);

    // Opaque pixels with no reflection contribution should not accumulate history.
    if (!centerTransMask.isTranslucent && dot(current.rgb, current.rgb) < 1e-10) {
        FragColor = current;
        return;
    }

    // Reproject using velocity buffer
    vec2 velocity = texelFetch(uVelocityTex, texel, 0).rg;
    vec2 prevCoord = textureUv - velocity;

    // Out-of-bounds history: use current frame
    if (prevCoord.x < 0.0 || prevCoord.x > 1.0 ||
        prevCoord.y < 0.0 || prevCoord.y > 1.0) {
        FragColor = current;
        return;
    }

    // Depth disocclusion: compare linearized current depth with linearized depth at
    // reprojected position. Uses the same linearization as ssao_temporal.fs.
    float historyDepth = texture(uDepthTex, prevCoord).r;
    float linCurrent = 2.0 * uNear / max(1.0 - depth, 1e-7);
    float linHistory = 2.0 * uNear / max(1.0 - historyDepth, 1e-7);
    float relDepthDiff = abs(linCurrent - linHistory) / max(linCurrent, 0.1);
    float depthDisocclusion = smoothstep(0.05, 0.5, relDepthDiff);

    // Normal disocclusion: reject history where surface orientation changed sharply
    vec3 currentNormal = reconstructNormal(texelFetch(uNormalAoTex, texel, 0));
    vec3 historyNormal = reconstructNormal(texture(uNormalAoTex, prevCoord));
    float normalDot = max(dot(currentNormal, historyNormal), 0.0);
    float normalDisocclusion = 1.0 - smoothstep(0.5, 0.95, normalDot);

    // Roughness disocclusion: smooth surfaces are more sensitive to mismatch
    SurfaceMaterial centerMaterial = unpackGBufferMaterial(texelFetch(uMaterialTex, texel, 0));
    float centerRoughness = linearMaterialRoughness(centerMaterial);
    float roughnessFactor = mix(0.5, 1.0, 1.0 - centerRoughness);

    // Combined disocclusion
    float disocclusion = max(depthDisocclusion, normalDisocclusion * roughnessFactor);

    // 3x3 neighborhood min/max clamp for RGB.
    // Prevents history from exceeding the local reflection range -> kills ghosting.
    ivec2 clampedSize = ivec2(uScreenSize) - 1;
    vec3 minColor = current.rgb;
    vec3 maxColor = current.rgb;
    float minAlpha = current.a;
    float maxAlpha = current.a;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            ivec2 sampleTexel = clamp(texel + ivec2(x, y), ivec2(0), clampedSize);
            vec4 s = texelFetch(uCurrentTex, sampleTexel, 0);
            minColor = min(minColor, s.rgb);
            maxColor = max(maxColor, s.rgb);
            minAlpha = min(minAlpha, s.a);
            maxAlpha = max(maxAlpha, s.a);
        }
    }

    // Sample and clamp history
    vec2 safeHistoryUv = clamp(prevCoord, texelSize * 0.5, 1.0 - texelSize * 0.5);
    vec4 history = texture(uHistoryTex, safeHistoryUv);
    history.rgb = clamp(history.rgb, minColor, maxColor);
    history.a = clamp(history.a, minAlpha, maxAlpha);

    // Blend weight: base weight reduced by disocclusion and pixel motion magnitude.
    // Stationary pixels get full history weight; fast-moving or disoccluded pixels get less.
    float pixelMotion = length(velocity * uScreenSize);
    float motionFactor = exp(-pixelMotion * 0.25);
    float blendWeight = uHistoryWeight * (1.0 - disocclusion) * motionFactor;

    vec4 result = mix(current, history, blendWeight);

    FragColor = vec4(max(result.rgb, vec3(0.0)), result.a);
}
