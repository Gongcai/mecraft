#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 1) in vec2 vClipUv;
layout(location = 0) out vec2 FragVelocity;

layout(binding = 0) uniform sampler2D uDepthTex;
layout(binding = 1) uniform sampler2D uPerObjectVelocityTex;

layout(push_constant) uniform RhiPushConstants {
    // Clip-to-previous-clip reprojection composed on the CPU in double
    // precision; for a static camera it is the identity to fp64 accuracy, so
    // no jitter or matrix-inverse residue leaks into the velocity buffer.
    mat4 uClipToPrevClip;
    // Far-plane reprojection with camera translation removed. Three rows are
    // sufficient because clear-depth sky positions always have z = w = 1.
    vec4 uSkyClipToPrevClipRows[3];
    vec4 uScreenParams;
};

const vec2 kRejectHistoryVelocity = vec2(2.0);
const float kSkyDepth = 1.0;

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

vec4 reprojectSky(vec2 currentClipPosition) {
    vec3 currentSkyPosition = vec3(currentClipPosition, 1.0);
    vec3 previousSkyPosition = vec3(
        dot(uSkyClipToPrevClipRows[0].xyz, currentSkyPosition),
        dot(uSkyClipToPrevClipRows[1].xyz, currentSkyPosition),
        dot(uSkyClipToPrevClipRows[2].xyz, currentSkyPosition));
    return vec4(previousSkyPosition.xy, 0.0, previousSkyPosition.z);
}

void main() {
    // A/B test: force zero velocity to verify TAA pure accumulation
    if (uScreenParams.z != 0.0) {
        FragVelocity = vec2(0.0);
        return;
    }

    ivec2 texel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uDepthTex, texel, 0).r;
    vec2 pixelCenter = vec2(texel) + vec2(0.5);

    // texelFetch addresses the integer texel while clip-space reprojection
    // addresses its center. Keeping both coordinates on the same sample is
    // required for stable camera motion at depth discontinuities.
    vec2 currentScreenUv = rhiNativeFragCoordToScreenUv(
        pixelCenter, uScreenParams.xy);
    vec2 currentTextureUv = rhiScreenUvToTextureUv(currentScreenUv);
    vec2 currentClipUv = rhiScreenUvToClipUv(currentScreenUv);

    // Projective reprojection straight from clip space: the composed matrix
    // already contains inverse(current raster VP) * previous jittered VP, so
    // no intermediate world-space divide is needed and the shared velocity
    // buffer stays in the native texture UV domain for every consumer.
    vec4 currentClip = vec4(currentClipUv * 2.0 - 1.0,
                            depth * 2.0 - 1.0, 1.0);
    vec4 previousClip = depth == kSkyDepth
        ? reprojectSky(currentClip.xy)
        : uClipToPrevClip * currentClip;
    if (badVec4(previousClip) || previousClip.w <= 0.00001) {
        FragVelocity = kRejectHistoryVelocity;
        return;
    }
    vec2 previousClipUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
    vec2 previousScreenUv = rhiScreenUvToClipUv(previousClipUv);
    vec2 previousTextureUv = rhiScreenUvToTextureUv(previousScreenUv);

    vec2 cameraVelocity = currentTextureUv - previousTextureUv;

    // Per-object velocity: entity/drop GBuffer shaders write texture UV velocity to a separate
    // MRT attachment. Use it when non-zero to
    // override camera-only reprojection for moving objects.
    vec2 perObjectVel = texelFetch(uPerObjectVelocityTex, texel, 0).rg;
    vec2 velocity = (!badVec2(perObjectVel) && dot(perObjectVel, perObjectVel) > 1e-10)
        ? perObjectVel
        : cameraVelocity;
    FragVelocity = sanitizeVelocity(velocity);
}
