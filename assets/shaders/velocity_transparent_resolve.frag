#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec2 FragVelocity;

layout(binding = 0) uniform sampler2D uOpaqueDepthTex;
layout(binding = 1) uniform sampler2D uTransparentDepthTex;
layout(binding = 2) uniform sampler2D uTransparencyMaskTex;

layout(push_constant) uniform RhiPushConstants {
    // Same fp64-composed clip-to-previous-clip transform as the opaque pass.
    mat4 uClipToPrevClip;
    vec4 uScreenParams;
};

const float kDepthEpsilon = 1.0e-5;
const vec2 kRejectHistoryVelocity = vec2(2.0);

bool badVec2(vec2 value) {
    return any(isnan(value)) || any(isinf(value));
}

bool badVec4(vec4 value) {
    return any(isnan(value)) || any(isinf(value));
}

vec2 sanitizeVelocity(vec2 velocity) {
    if (badVec2(velocity)) {
        return kRejectHistoryVelocity;
    }
    return clamp(velocity, vec2(-2.0), vec2(2.0));
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    float transparency = texelFetch(uTransparencyMaskTex, texel, 0).r;
    if (transparency <= 1.0e-4) {
        discard;
    }

    float opaqueDepth = texelFetch(uOpaqueDepthTex, texel, 0).r;
    float transparentDepth = texelFetch(uTransparentDepthTex, texel, 0).r;
    if (badVec2(vec2(opaqueDepth, transparentDepth)) ||
        transparentDepth >= 1.0 - kDepthEpsilon ||
        transparentDepth >= opaqueDepth - kDepthEpsilon) {
        discard;
    }

    if (uScreenParams.z != 0.0) {
        FragVelocity = vec2(0.0);
        return;
    }

    vec2 screenUv = rhiNativeFragCoordToScreenUv(
        vec2(gl_FragCoord.xy), uScreenParams.xy);
    vec2 currentTextureUv = rhiScreenUvToTextureUv(screenUv);
    vec2 clipUv = rhiScreenUvToClipUv(screenUv);
    vec4 currentClip = vec4(clipUv * 2.0 - 1.0,
                            transparentDepth * 2.0 - 1.0, 1.0);
    vec4 previousClip = uClipToPrevClip * currentClip;
    if (badVec4(previousClip) || previousClip.w <= 0.00001) {
        FragVelocity = kRejectHistoryVelocity;
        return;
    }
    vec2 previousClipUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
    vec2 previousScreenUv = rhiScreenUvToClipUv(previousClipUv);
    vec2 previousTextureUv = rhiScreenUvToTextureUv(previousScreenUv);
    FragVelocity = sanitizeVelocity(currentTextureUv - previousTextureUv);
}
