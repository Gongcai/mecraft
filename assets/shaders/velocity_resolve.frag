#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 1) in vec2 vClipUv;
layout(location = 0) out vec2 FragVelocity;

layout(binding = 0) uniform sampler2D uDepthTex;
layout(binding = 1) uniform sampler2D uPerObjectVelocityTex;

layout(push_constant) uniform RhiPushConstants {
    mat4 uInvViewProj;
    mat4 uPreviousViewProj;
    vec4 uScreenParams;
};

const vec2 kRejectHistoryVelocity = vec2(2.0);

bool badVec2(vec2 v) {
    return any(isnan(v)) || any(isinf(v));
}

bool badVec4(vec4 v) {
    return any(isnan(v)) || any(isinf(v));
}

vec2 sanitizeVelocity(vec2 velocity) {
    if (badVec2(velocity)) {
        return kRejectHistoryVelocity;
    }
    // Keep finite but very large reprojection errors bounded before RG16F storage.
    return clamp(velocity, vec2(-2.0), vec2(2.0));
}

vec3 reconstructWorldPosition(vec2 clipUv, float depth, out bool valid) {
    vec4 clip = vec4(clipUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    valid = !badVec4(world) && abs(world.w) > 0.00001;
    if (!valid) {
        return vec3(0.0);
    }
    return world.xyz / world.w;
}

void main() {
    // A/B test: force zero velocity to verify TAA pure accumulation
    if (uScreenParams.z != 0.0) {
        FragVelocity = vec2(0.0);
        return;
    }

    ivec2 texel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uDepthTex, texel, 0).r;

    // Use the center pixel depth for reprojection. Replacing it with a nearest
    // 3x3 neighbor causes alpha-cutout foliage edges to inherit motion from an
    // unrelated trunk, terrain, or sky pixel.
    vec3 closestFragment = vec3(texel, depth);

    // DerivativeMain: no sky early return. depth=1 (sky/far-plane) gets
    // a valid far-plane reprojection velocity. This is critical for TAA
    // to properly accumulate VFog dither; sky pixels must track camera
    // rotation, not stay pinned to screen space.
    // DerivativeMain/program/Post/Temporal.frag::GetClosestFragment returns
    // closestFragment.xy *= screenPixelSize, without a half-texel offset.
    vec2 closestTextureUv = closestFragment.xy / uScreenParams.xy;
    vec2 closestScreenUv = rhiNativeFragCoordToScreenUv(
        closestFragment.xy, uScreenParams.xy);
    vec2 closestClipUv = rhiScreenUvToClipUv(closestScreenUv);
    bool worldValid = false;
    vec3 worldPos = reconstructWorldPosition(closestClipUv, closestFragment.z, worldValid);
    if (!worldValid) {
        FragVelocity = kRejectHistoryVelocity;
        return;
    }

    // Standard world-space reprojection. The shared velocity buffer stores native texture UV
    // deltas so TAA, SSAO, reflections, volumetric fog, and motion blur consume one domain.
    vec4 previousClip = uPreviousViewProj * vec4(worldPos, 1.0);
    if (badVec4(previousClip) || previousClip.w <= 0.00001) {
        FragVelocity = kRejectHistoryVelocity;
        return;
    }
    vec2 previousClipUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
    vec2 previousScreenUv = rhiScreenUvToClipUv(previousClipUv);
    vec2 previousTextureUv = rhiScreenUvToTextureUv(previousScreenUv);

    vec2 cameraVelocity = closestTextureUv - previousTextureUv;

    // Per-object velocity: entity/drop GBuffer shaders write texture UV velocity to a separate
    // MRT attachment. Use it when non-zero to
    // override camera-only reprojection for moving objects.
    vec2 perObjectVel = texelFetch(uPerObjectVelocityTex, ivec2(closestFragment.xy), 0).rg;
    vec2 velocity = (!badVec2(perObjectVel) && dot(perObjectVel, perObjectVel) > 1e-10)
        ? perObjectVel
        : cameraVelocity;
    FragVelocity = sanitizeVelocity(velocity);
}
