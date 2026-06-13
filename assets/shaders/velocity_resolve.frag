#version 450 core

in vec2 vTexCoord;
out vec2 FragVelocity;

uniform sampler2D uDepthTex;
uniform sampler2D uPerObjectVelocityTex;
uniform mat4 uInvViewProj;
uniform mat4 uPreviousViewProj;
uniform vec3 uCameraPosition;
uniform vec3 uPreviousCameraPosition;
uniform vec2 uScreenSize;
uniform int uForceZeroVelocity; // A/B test: 1 = output zero velocity everywhere
uniform int uDisableCameraTranslation; // Debug: 1 = ignore camera translation for all fragments
const vec2 kRejectHistoryVelocity = vec2(2.0);

// DerivativeMain: 3x3 neighborhood offsets (excluding center)
const ivec2 offset3x3N[8] = ivec2[8](
    ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1),
    ivec2(-1,  0),                ivec2(1,  0),
    ivec2(-1,  1), ivec2(0,  1), ivec2(1,  1)
);

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

vec3 reconstructWorldPosition(vec2 uv, float depth, out bool valid) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    valid = !badVec4(world) && abs(world.w) > 0.00001;
    if (!valid) {
        return vec3(0.0);
    }
    return world.xyz / world.w;
}

void main() {
    // A/B test: force zero velocity to verify TAA pure accumulation
    if (uForceZeroVelocity != 0) {
        FragVelocity = vec2(0.0);
        return;
    }

    ivec2 texel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uDepthTex, texel, 0).r;

    // DerivativeMain-style closest fragment: search 3x3 neighborhood for the
    // nearest depth texel. This stabilizes velocity at depth discontinuities.
    vec3 closestFragment = vec3(texel, depth);
    for (int i = 0; i < 8; ++i) {
        ivec2 sampleTexel = offset3x3N[i] + texel;
        float sampleDepth = texelFetch(uDepthTex, clamp(sampleTexel, ivec2(0), ivec2(uScreenSize) - 1), 0).r;
        if (sampleDepth < closestFragment.z) {
            closestFragment = vec3(sampleTexel, sampleDepth);
        }
    }

    // DerivativeMain: no sky early return. depth=1 (sky/far-plane) gets
    // a valid far-plane reprojection velocity. This is critical for TAA
    // to properly accumulate VFog dither; sky pixels must track camera
    // rotation, not stay pinned to screen space.
    // DerivativeMain/program/Post/Temporal.frag::GetClosestFragment returns
    // closestFragment.xy *= screenPixelSize, without a half-texel offset.
    vec2 closestUv = closestFragment.xy / uScreenSize;
    bool worldValid = false;
    vec3 worldPos = reconstructWorldPosition(closestUv, closestFragment.z, worldValid);
    if (!worldValid) {
        FragVelocity = kRejectHistoryVelocity;
        return;
    }

    // DerivativeMain raw reprojection: Reproject() uses raw projection
    // matrices and does not manually subtract current/previous jitter.
    // CRITICAL: only add camera translation for FAR fragments (depth > 0.56).
    // Near fragments should only see rotation-induced velocity, not translation.
    // This prevents motion blur on nearby objects when walking, matching
    // DerivativeMain's Temporal.frag::Reproject() behavior.
    vec3 cameraTranslation = vec3(0.0);
    if (uDisableCameraTranslation == 0 && closestFragment.z > 0.56) {
        cameraTranslation = uCameraPosition - uPreviousCameraPosition;
    }
    vec3 worldPosWithTranslation = worldPos + cameraTranslation;

    vec4 previousClip = uPreviousViewProj * vec4(worldPosWithTranslation, 1.0);
    if (badVec4(previousClip) || previousClip.w <= 0.00001) {
        FragVelocity = kRejectHistoryVelocity;
        return;
    }
    vec2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;

    vec2 cameraVelocity = closestUv - previousUv;

    // Per-object velocity: entity/drop GBuffer shaders write screen-space
    // velocity to a separate texture via MRT. Use it when non-zero to
    // override camera-only reprojection for moving objects.
    vec2 perObjectVel = texelFetch(uPerObjectVelocityTex, ivec2(closestFragment.xy), 0).rg;
    vec2 velocity = (!badVec2(perObjectVel) && dot(perObjectVel, perObjectVel) > 1e-10)
        ? perObjectVel
        : cameraVelocity;
    FragVelocity = sanitizeVelocity(velocity);
}
