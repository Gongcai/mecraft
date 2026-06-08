#version 450 core
// DerivativeMain-style temporal resolve. Parity with Temporal.frag:
// - Variance clip (mean +/- 1.25 * stddev) instead of AABB expansion
// - Fixed 0.97 blend weight with sub-pixel coverage modulation
// - Reinhard luminance-weighted tonemapping
// - taaOffset * 0.5 applied to current sample coordinate

#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uCurrentTex;
uniform sampler2D uHistoryTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uDepthTex;
uniform sampler2D uMaterialAuxTex;

uniform vec2 uScreenSize;
uniform vec2 uJitter;
uniform float uSurfaceWetness;
uniform int uRainWetSurfacesEnabled;

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

bool badVec2(vec2 v) {
    return any(isnan(v)) || any(isinf(v));
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
// Sharper than bilinear for history sampling, reduces variance clip rejection.
vec4 catmullRomFast(sampler2D tex, vec2 coord) {
    vec2 pxSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 position = uScreenSize * coord;
    vec2 centerPosition = floor(position - 0.5) + 0.5;
    vec2 f = position - centerPosition;
    vec2 f2 = f * f;
    vec2 f3 = f * f2;

    const float sharpness = 0.7; // DerivativeMain TAA_SHARPNESS
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
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 screenCoord = gl_FragCoord.xy * texelSize;

    vec2 velocity = texelFetch(uVelocityTex, texel, 0).rg;
    vec2 previousCoord = screenCoord - velocity;

    // Bad motion vectors make texture() with NaN/Inf coordinates undefined,
    // which appears as stable-shaped regions filled with drifting history.
    if (badVec2(velocity) || badVec2(previousCoord) ||
        any(greaterThan(abs(velocity), vec2(1.0)))) {
        FragColor = texelFetch(uCurrentTex, texel, 0);
        return;
    }

    // Out-of-bounds history: use current frame
    if (previousCoord.x < 0.0 || previousCoord.x > 1.0 ||
        previousCoord.y < 0.0 || previousCoord.y > 1.0) {
        FragColor = texelFetch(uCurrentTex, texel, 0);
        return;
    }

    // DerivativeMain: apply taaOffset * 0.5 to current sample coordinate.
    // Convert UV to pixel, apply jitter in pixel space, clamp.
    vec2 samplePixel = screenCoord * uScreenSize + uJitter * uScreenSize * 0.5;
    ivec2 sampleTexel = clamp(ivec2(samplePixel), ivec2(0), ivec2(uScreenSize) - 1);

    vec3 currentSample = texelFetch(uCurrentTex, sampleTexel, 0).rgb;

    // DerivativeMain: no sky special case. All pixels, including sky and
    // VFog, go through the same variance clip + 0.97 history blend.
    // Sky velocity comes from far-plane reprojection, not zero.

    // 3x3 neighborhood in YCoCgR for variance clip, all around sampleTexel.
    ivec2 clampedSize = ivec2(uScreenSize) - 1;
    vec3 col0 = RGBtoYCoCgR(currentSample);
    vec3 col1 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2(-1,  1), ivec2(0), clampedSize), 0).rgb);
    vec3 col2 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 0,  1), ivec2(0), clampedSize), 0).rgb);
    vec3 col3 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 1,  1), ivec2(0), clampedSize), 0).rgb);
    vec3 col4 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2(-1,  0), ivec2(0), clampedSize), 0).rgb);
    vec3 col5 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 1,  0), ivec2(0), clampedSize), 0).rgb);
    vec3 col6 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2(-1, -1), ivec2(0), clampedSize), 0).rgb);
    vec3 col7 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 0, -1), ivec2(0), clampedSize), 0).rgb);
    vec3 col8 = RGBtoYCoCgR(texelFetch(uCurrentTex, clamp(sampleTexel + ivec2( 1, -1), ivec2(0), clampedSize), 0).rgb);

    // DerivativeMain variance clip: mean +/- 1.25 * stddev
    vec3 clipAvg = (col0 + col1 + col2 + col3 + col4 + col5 + col6 + col7 + col8) / 9.0;
    vec3 sqrVar = (col0*col0 + col1*col1 + col2*col2 + col3*col3 + col4*col4 + col5*col5 + col6*col6 + col7*col7 + col8*col8) / 9.0;
    vec3 variance = sqrt(abs(sqrVar - clipAvg * clipAvg));
    vec3 clipMin = clipAvg - variance * 1.25;
    vec3 clipMax = clipAvg + variance * 1.25;

    // Sample and clip history — CatmullRom for sharper reconstruction.
    // DerivativeMain Temporal.frag: textureCatmullRomFast with TAA_SHARPNESS=0.7.
    vec2 safeHistoryUv = clamp(previousCoord, texelSize * 0.5, 1.0 - texelSize * 0.5);
    vec3 previousSample = RGBtoYCoCgR(catmullRomFast(uHistoryTex, safeHistoryUv).rgb);
    previousSample = clipAABB(clipMin, clipMax, previousSample);
    previousSample = YCoCgRtoRGB(previousSample);

    // DerivativeMain blend: fixed 0.97 with sub-pixel coverage modulation.
    // When the reprojected coordinate lands near a texel center (coverage ~1),
    // history gets full weight. Near texel edges, weight drops slightly.
    float blendWeight = 0.97;
    vec2 pixelVelocity = 1.0 - abs(fract(previousCoord * uScreenSize) * 2.0 - 1.0);
    blendWeight *= sqrt(pixelVelocity.x * pixelVelocity.y) * 0.25 + 0.75;

    // Mecraft adaptation: DerivativeMain writes rain ripple normals into the
    // G-buffer before its TAA pass. In this renderer the animated wet-reflection
    // signal is much smaller relative to the lit scene, so a full 0.97 history
    // average can converge the moving RippleNormal contribution away. Keep the
    // base Temporal.frag path for ordinary pixels, but make puddle pixels
    // current-frame dominant so the DerivativeMain ripple source survives resolve.
    if (uRainWetSurfacesEnabled != 0 && uSurfaceWetness > 1e-2) {
        float depth = texelFetch(uDepthTex, sampleTexel, 0).r;
        if (depth < 0.9999) {
            SurfaceMaterialAux aux = unpackGBufferMaterialAux(texelFetch(uMaterialAuxTex, sampleTexel, 0));
            TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
            if (!transMask.isTranslucent) {
                float wetHistoryReject = smoothstep(0.02, 0.25, aux.wetnessMask) *
                                         clamp(uSurfaceWetness, 0.0, 1.0);
                blendWeight = mix(blendWeight, 0.06, wetHistoryReject);
            }
        }
    }

    // DerivativeMain: Reinhard luminance-weighted blend
    vec3 result = invReinhard(mix(reinhard(currentSample), reinhard(previousSample), blendWeight));

    // Preserve fog transmittance in alpha
    float currentAlpha = texelFetch(uCurrentTex, sampleTexel, 0).a;
    float historyAlpha = texture(uHistoryTex, safeHistoryUv).a;
    float resultAlpha = mix(historyAlpha, currentAlpha, 1.0 - blendWeight);

    FragColor = vec4(max(result, vec3(0.0)), resultAlpha);
}
