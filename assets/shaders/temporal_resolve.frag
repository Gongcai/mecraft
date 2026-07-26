#version 450 core
// DerivativeMain-style temporal resolve with explicit transparent-surface
// rejection. The history color remains in the resolved, non-jittered domain;
// history depth is sampled in the previous frame's jittered raster domain.

#include "gbuffer_contract.glsl"
#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uCurrentTex;
layout(binding = 1) uniform sampler2D uHistoryTex;
layout(binding = 2) uniform sampler2D uHistoryDepthTex;
layout(binding = 3) uniform sampler2D uVelocityTex;
layout(binding = 4) uniform sampler2D uOpaqueDepthTex;
layout(binding = 5) uniform sampler2D uTransparentDepthTex;
layout(binding = 6) uniform sampler2D uReactiveMaskTex;
layout(binding = 7) uniform sampler2D uTransparencyMaskTex;
layout(binding = 8) uniform sampler2D uMaterialAuxTex;

layout(push_constant) uniform RhiPushConstants {
    mat4 uCurrentInvViewProj;
    mat4 uPreviousJitteredViewProj;
    vec4 uScreenSizeJitter;
    vec4 uTemporalParams;
};

const float kDepthEpsilon = 1.0e-5;

vec3 RGBtoYCoCgR(in vec3 rgbColor) {
    vec3 ycocg;
    ycocg.y = rgbColor.r - rgbColor.b;
    float temp = rgbColor.b + ycocg.y * 0.5;
    ycocg.z = rgbColor.g - temp;
    ycocg.x = temp + ycocg.z * 0.5;
    return ycocg;
}

vec3 YCoCgRtoRGB(in vec3 ycocg) {
    vec3 rgb;
    float temp = ycocg.x - ycocg.z * 0.5;
    rgb.g = ycocg.z + temp;
    rgb.b = temp - ycocg.y * 0.5;
    rgb.r = rgb.b + ycocg.y;
    return rgb;
}

float GetLuminance(in vec3 color) {
    return dot(color, vec3(0.2722, 0.6741, 0.0537));
}

vec3 reinhard(in vec3 color) {
    return color / (1.0 + GetLuminance(color));
}

vec3 invReinhard(in vec3 color) {
    return color / (1.0 - GetLuminance(color));
}

bool badVec2(vec2 value) {
    return any(isnan(value)) || any(isinf(value));
}

bool badVec4(vec4 value) {
    return any(isnan(value)) || any(isinf(value));
}

vec3 reconstructWorldPosition(vec2 clipUv, float depth, out bool valid) {
    vec4 clip = vec4(clipUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uCurrentInvViewProj * clip;
    valid = !badVec4(world) && abs(world.w) > 0.00001;
    if (!valid) {
        return vec3(0.0);
    }
    return world.xyz / world.w;
}

float linearizeDepth(float depth) {
    float nearPlane = max(uTemporalParams.z, 1.0e-4);
    float farPlane = max(uTemporalParams.w, nearPlane + 1.0e-3);
    float denominator = depth * (nearPlane - farPlane) + farPlane;
    return nearPlane * farPlane / max(denominator, 1.0e-7);
}

vec3 clipAABB(in vec3 boxMin, in vec3 boxMax, in vec3 previousSample) {
    vec3 p_clip = 0.5 * (boxMax + boxMin);
    vec3 e_clip = 0.5 * (boxMax - boxMin);
    vec3 v_clip = previousSample - p_clip;
    vec3 v_unit = v_clip / max(e_clip, vec3(1e-6));
    vec3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));
    if (ma_unit > 1.0) {
        return v_clip / ma_unit + p_clip;
    }
    return previousSample;
}

// DerivativeMain Temporal.frag: SMAA CatmullRom approximation (5-tap).
vec4 catmullRomFast(sampler2D tex, vec2 coord) {
    vec2 pxSize = 1.0 / max(uScreenSizeJitter.xy, vec2(1.0));
    vec2 position = uScreenSizeJitter.xy * coord;
    vec2 centerPosition = floor(position - 0.5) + 0.5;
    vec2 f = position - centerPosition;
    vec2 f2 = f * f;
    vec2 f3 = f * f2;

    const float sharpness = 0.7;
    vec2 w0 = -sharpness        * f3 + 2.0 * sharpness         * f2 - sharpness * f;
    vec2 w1 = (2.0 - sharpness) * f3 - (3.0 - sharpness)       * f2 + 1.0;
    vec2 w2 = (sharpness - 2.0) * f3 + (3.0 - 2.0 * sharpness) * f2 + sharpness * f;
    vec2 w3 = sharpness         * f3 - sharpness                * f2;

    vec2 w12 = w1 + w2;
    vec2 tc0 = pxSize * (centerPosition - 1.0);
    vec2 tc3 = pxSize * (centerPosition + 2.0);
    vec2 tc12 = pxSize * (centerPosition + w2 / w12);

    float l0 = w12.x * w0.y;
    float l1 = w0.x  * w12.y;
    float l2 = w12.x * w12.y;
    float l3 = w3.x  * w12.y;
    float l4 = w12.x * w3.y;

    vec4 color =  texture(tex, vec2(tc12.x, tc0.y )) * l0
               + texture(tex, vec2(tc0.x,  tc12.y)) * l1
               + texture(tex, vec2(tc12.x, tc12.y)) * l2
               + texture(tex, vec2(tc3.x,  tc12.y)) * l3
               + texture(tex, vec2(tc12.x, tc3.y )) * l4;

    return color / (l0 + l1 + l2 + l3 + l4);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 texelSize = 1.0 / max(uScreenSizeJitter.xy, vec2(1.0));
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);

    vec2 velocity = texelFetch(uVelocityTex, texel, 0).rg;
    vec2 previousCoord = textureUv - velocity;
    if (badVec2(velocity) || badVec2(previousCoord) ||
        any(greaterThan(abs(velocity), vec2(1.0)))) {
        FragColor = texelFetch(uCurrentTex, texel, 0);
        return;
    }

    if (previousCoord.x < 0.0 || previousCoord.x > 1.0 ||
        previousCoord.y < 0.0 || previousCoord.y > 1.0) {
        FragColor = texelFetch(uCurrentTex, texel, 0);
        return;
    }

    float opaqueDepth = texelFetch(uOpaqueDepthTex, texel, 0).r;
    float transparentDepth = texelFetch(uTransparentDepthTex, texel, 0).r;
    float reactiveMask = texelFetch(uReactiveMaskTex, texel, 0).r;
    float transparencyMask = texelFetch(uTransparencyMaskTex, texel, 0).r;
    bool transparentSurface = transparencyMask > 1.0e-4 &&
                              transparentDepth < 1.0 - kDepthEpsilon &&
                              transparentDepth < opaqueDepth - kDepthEpsilon;
    float currentDepth = transparentSurface ? transparentDepth : opaqueDepth;

    // Reconstruct the current surface using the matrix that produced the
    // raster depth, then locate that surface in the previous jittered depth.
    bool worldValid = false;
    vec3 worldPosition = reconstructWorldPosition(
        rhiScreenUvToClipUv(vScreenUv), currentDepth, worldValid);
    vec2 historyDepthUv = vec2(0.0);
    float expectedHistoryDepth = 1.0;
    bool historyDepthValid = worldValid;
    if (historyDepthValid) {
        vec4 previousJitteredClip =
            uPreviousJitteredViewProj * vec4(worldPosition, 1.0);
        historyDepthValid = !badVec4(previousJitteredClip) &&
                           previousJitteredClip.w > 0.00001;
        if (historyDepthValid) {
            vec2 previousClipUv = previousJitteredClip.xy /
                                  previousJitteredClip.w * 0.5 + 0.5;
            vec2 previousScreenUv = rhiScreenUvToClipUv(previousClipUv);
            historyDepthUv = rhiScreenUvToTextureUv(previousScreenUv);
            expectedHistoryDepth = previousJitteredClip.z /
                                   previousJitteredClip.w * 0.5 + 0.5;
            historyDepthValid = !badVec2(historyDepthUv) &&
                                !badVec2(vec2(expectedHistoryDepth)) &&
                                expectedHistoryDepth >= 0.0 &&
                                expectedHistoryDepth <= 1.0 &&
                                historyDepthUv.x >= 0.0 &&
                                historyDepthUv.x <= 1.0 &&
                                historyDepthUv.y >= 0.0 &&
                                historyDepthUv.y <= 1.0;
        }
    }

    // DerivativeMain applies taaOffset * 0.5 to the current color sample.
    vec2 samplePixel = textureUv * uScreenSizeJitter.xy +
        uScreenSizeJitter.zw * uScreenSizeJitter.xy * 0.5;
    ivec2 sampleTexel = clamp(
        ivec2(samplePixel), ivec2(0), ivec2(uScreenSizeJitter.xy) - 1);
    vec3 currentSample = texelFetch(uCurrentTex, sampleTexel, 0).rgb;

    ivec2 clampedSize = ivec2(uScreenSizeJitter.xy) - 1;
    vec3 col0 = RGBtoYCoCgR(currentSample);
    vec3 col1 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2(-1,  1), ivec2(0), clampedSize), 0).rgb);
    vec3 col2 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 0,  1), ivec2(0), clampedSize), 0).rgb);
    vec3 col3 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 1,  1), ivec2(0), clampedSize), 0).rgb);
    vec3 col4 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2(-1,  0), ivec2(0), clampedSize), 0).rgb);
    vec3 col5 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 1,  0), ivec2(0), clampedSize), 0).rgb);
    vec3 col6 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2(-1, -1), ivec2(0), clampedSize), 0).rgb);
    vec3 col7 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 0, -1), ivec2(0), clampedSize), 0).rgb);
    vec3 col8 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 1, -1), ivec2(0), clampedSize), 0).rgb);

    vec3 clipAvg = (col0 + col1 + col2 + col3 + col4 + col5 + col6 + col7 + col8) / 9.0;
    vec3 sqrVar = (col0*col0 + col1*col1 + col2*col2 + col3*col3 + col4*col4 + col5*col5 + col6*col6 + col7*col7 + col8*col8) / 9.0;
    vec3 variance = sqrt(abs(sqrVar - clipAvg * clipAvg));
    vec3 clipMin = clipAvg - variance * 1.25;
    vec3 clipMax = clipAvg + variance * 1.25;

    vec2 safeHistoryUv = clamp(previousCoord,
                               texelSize * 0.5,
                               1.0 - texelSize * 0.5);
    vec3 previousSample = RGBtoYCoCgR(
        catmullRomFast(uHistoryTex, safeHistoryUv).rgb);
    previousSample = clipAABB(clipMin, clipMax, previousSample);
    previousSample = YCoCgRtoRGB(previousSample);

    float depthDisocclusion = 1.0;
    if (historyDepthValid) {
        ivec2 historyDepthSize = ivec2(uScreenSizeJitter.xy);
        ivec2 historyDepthTexel = clamp(
            ivec2(historyDepthUv * uScreenSizeJitter.xy),
            ivec2(0), historyDepthSize - 1);
        // Test the expected depth against the 3x3 history depth RANGE, not a
        // single texel. Sub-pixel foliage flips a texel between leaf and
        // background depth every frame under jitter, and grazing surfaces
        // (distant water/ground) change linear depth by whole meters per
        // texel, so a one-sample comparison misreads both as disocclusion
        // and permanently rejects valid history.
        float minHistoryDepth = 1.0;
        float maxHistoryDepth = 0.0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                ivec2 tapTexel = clamp(historyDepthTexel + ivec2(dx, dy),
                                       ivec2(0), historyDepthSize - 1);
                float tapDepth = texelFetch(uHistoryDepthTex, tapTexel, 0).r;
                minHistoryDepth = min(minHistoryDepth, tapDepth);
                maxHistoryDepth = max(maxHistoryDepth, tapDepth);
            }
        }
        float expectedLinearDepth = linearizeDepth(expectedHistoryDepth);
        float minLinearDepth = linearizeDepth(minHistoryDepth);
        float maxLinearDepth = linearizeDepth(maxHistoryDepth);
        float rangeDistance = max(
            max(minLinearDepth - expectedLinearDepth,
                expectedLinearDepth - maxLinearDepth),
            0.0);
        float relativeDepthDifference =
            rangeDistance / max(expectedLinearDepth, 0.1);
        depthDisocclusion = smoothstep(0.035, 0.28,
                                       relativeDepthDifference);
    }

    float blendWeight = 0.97;
    vec2 pixelVelocity = 1.0 -
        abs(fract(previousCoord * uScreenSizeJitter.xy) * 2.0 - 1.0);
    blendWeight *= sqrt(pixelVelocity.x * pixelVelocity.y) * 0.25 + 0.75;

    // Reactive and transparency masks describe pixels whose current color is
    // not a stable projection of the previous resolved image.
    float maskHistoryReject = clamp(max(reactiveMask, transparencyMask),
                                    0.0, 1.0);
    // Depth disocclusion attenuates history instead of zeroing it: the YCoCg
    // neighborhood clamp already bounds stale history, while sub-pixel
    // geometry (very sparse distant foliage) can never fully pass a depth
    // test, so a hard zero re-exposes raw raster flicker at distance.
    blendWeight *= (1.0 - maskHistoryReject) *
                   (1.0 - depthDisocclusion * 0.8);

    // Animated wet opaque surfaces need the existing ripple-specific rejection,
    // while transparent surfaces use their explicit temporal masks instead.
    if (!transparentSurface && uTemporalParams.y != 0.0 &&
        uTemporalParams.x > 1e-2) {
        if (currentDepth < 0.9999) {
            SurfaceMaterialAux aux = unpackGBufferMaterialAux(
                texelFetch(uMaterialAuxTex, sampleTexel, 0));
            TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
            if (!transMask.isTranslucent) {
                float wetHistoryReject = smoothstep(0.02, 0.25,
                                                    aux.wetnessMask) *
                                          clamp(uTemporalParams.x, 0.0, 1.0);
                float wetHistoryLimit = mix(0.97, 0.06, wetHistoryReject);
                blendWeight = min(blendWeight, wetHistoryLimit);
            }
        }
    }

    vec3 result = invReinhard(
        mix(reinhard(currentSample), reinhard(previousSample), blendWeight));
    float currentAlpha = texelFetch(uCurrentTex, sampleTexel, 0).a;
    float historyAlpha = texture(uHistoryTex, safeHistoryUv).a;
    float resultAlpha = mix(historyAlpha, currentAlpha, 1.0 - blendWeight);
    FragColor = vec4(max(result, vec3(0.0)), resultAlpha);
}
