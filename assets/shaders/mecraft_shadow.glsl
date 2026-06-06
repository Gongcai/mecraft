// Mecraft shadow contract.
//
// Formal scene shadows use a linear cascaded shadow map that is compatible with
// Mecraft's greedy terrain meshes. Derivative/Radial warp helpers live in
// derivative_shadow.glsl only for historical debug paths and shared math.

#ifndef MECRAFT_SHADOW_GLSL
#define MECRAFT_SHADOW_GLSL

#include "derivative_shadow.glsl"

#ifndef MECRAFT_CSM_CASCADE_COUNT
#define MECRAFT_CSM_CASCADE_COUNT 4
#endif

struct CsmCascade {
    mat4 viewProj;
    float splitNear;
    float splitFar;
    float texelWorldSize;
    float resolutionScale;
};

uniform int uCsmCascadeCount;
uniform CsmCascade uCsmCascades[MECRAFT_CSM_CASCADE_COUNT];

#ifndef MECRAFT_SHADOW_NO_SAMPLER
uniform sampler2DArrayShadow uCsmShadowMap;       // shadowtex1: opaque-only depth (comparison)
uniform sampler2DArray uCsmShadowDepthRaw;         // shadowtex1: opaque-only depth (raw)
#ifndef MECRAFT_SHADOW_OPAQUE_ONLY
uniform sampler2DArrayShadow uCsmShadowDepthAll;   // shadowtex0: depth including water (comparison)
uniform sampler2DArray uCsmShadowDepthAllRaw;      // shadowtex0: depth including water (raw)
uniform sampler2DArray uCsmShadowColor0;           // shadowcolor0: RGB tint/caustics, A transparent flag
uniform sampler2DArray uCsmShadowColor1;           // shadowcolor1: RG normal, B skylight, A water height
#endif
#endif

struct ShadowSample {
    float visibility;
    int cascadeIndex;
    float fade;
    float blockerDepth; // signed receiver-blocker delta from PCSS (negative = blocker present); 0 when unavailable
    float sssWeight;    // contribution weight for PCSS-derived SSS; fades across cascade/projection edges
};

int selectCsmCascade(float viewDistance) {
    int cascadeIndex = max(uCsmCascadeCount - 1, 0);
    for (int i = 0; i < MECRAFT_CSM_CASCADE_COUNT; ++i) {
        if (i >= uCsmCascadeCount) break;
        if (viewDistance <= uCsmCascades[i].splitFar) {
            cascadeIndex = i;
            break;
        }
    }
    return cascadeIndex;
}

// Dithered cascade selection: no double-sampling, relies on TAA to clean up.
int selectCsmCascadeDithered(float viewDistance, float dither) {
    int cascadeIndex = max(uCsmCascadeCount - 1, 0);
    for (int i = 0; i < MECRAFT_CSM_CASCADE_COUNT; ++i) {
        if (i >= uCsmCascadeCount) break;
        float splitFar = uCsmCascades[i].splitFar;
        float splitNear = uCsmCascades[i].splitNear;
        float transitionWidth = (splitFar - splitNear) * 0.08;
        float threshold = splitFar - transitionWidth * dither;
        if (viewDistance <= threshold) {
            cascadeIndex = i;
            break;
        }
    }
    return cascadeIndex;
}

vec3 csmProjectWorld(vec3 worldPos, int cascadeIndex) {
    vec4 shadowClip = uCsmCascades[cascadeIndex].viewProj * vec4(worldPos, 1.0);
    return shadowClip.xyz / max(abs(shadowClip.w), 1.0e-6) * 0.5 + 0.5;
}

float csmProjectionFade(vec3 proj, vec2 texelUv) {
    vec2 edgeDistance = min(proj.xy, vec2(1.0) - proj.xy);
    return smoothstep(texelUv.x * 2.0, texelUv.x * 12.0,
                      min(edgeDistance.x, edgeDistance.y));
}

float csmDepthBias(float ndotl,
                   float viewDistance,
                   int cascadeIndex,
                   ivec3 shadowSize,
                   float shadowDistance,
                   float constantBias,
                   float slopeBias) {
    float texelWorld = max(uCsmCascades[cascadeIndex].texelWorldSize, 0.0001);
    float radiusWorld = texelWorld * float(max(shadowSize.x, 1)) * 0.5;
    float depthExtent = max(shadowDistance + radiusWorld * 3.0, 1.0);
    float biasWorld = shadowWorldBias(ndotl, viewDistance, texelWorld,
                                      shadowDistance, constantBias, slopeBias);
    return max(biasWorld / (2.0 * depthExtent), 4.0e-5);
}

#ifndef MECRAFT_SHADOW_NO_SAMPLER
float sampleCsmDepthCompare(vec2 uv, int cascadeIndex, float refZ) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    return texture(uCsmShadowMap, vec4(uv * scale, float(cascadeIndex), refZ));
}

// Transparent shadow sampling (DerivativeMain shadowtex0/shadowcolor0/1 equivalent)
#ifndef MECRAFT_SHADOW_OPAQUE_ONLY
float sampleCsmDepthAllCompare(vec2 uv, int cascadeIndex, float refZ) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    return texture(uCsmShadowDepthAll, vec4(uv * scale, float(cascadeIndex), refZ));
}

float sampleCsmDepthAllRaw(vec2 uv, int cascadeIndex) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    return texture(uCsmShadowDepthAllRaw, vec3(uv * scale, float(cascadeIndex))).r;
}

float sampleCsmDepthRaw(vec2 uv, int cascadeIndex) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    return texture(uCsmShadowDepthRaw, vec3(uv * scale, float(cascadeIndex))).r;
}

vec4 sampleCsmShadowColor0(vec2 uv, int cascadeIndex) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    return texture(uCsmShadowColor0, vec3(uv * scale, float(cascadeIndex)));
}

vec4 sampleCsmShadowColor1(vec2 uv, int cascadeIndex) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    return texture(uCsmShadowColor1, vec3(uv * scale, float(cascadeIndex)));
}
#endif

ivec2 sampleCsmTexelCoord(vec2 uv, int cascadeIndex, sampler2DArray shadowTex) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    ivec3 size = textureSize(shadowTex, 0);
    ivec2 dims = ivec2(max(size.x, 1), max(size.y, 1));
    return clamp(ivec2(floor(uv * scale * vec2(dims))), ivec2(0), dims - ivec2(1));
}

float sampleCsmDepthRawTexel(vec2 uv, int cascadeIndex) {
    ivec2 texel = sampleCsmTexelCoord(uv, cascadeIndex, uCsmShadowDepthRaw);
    return texelFetch(uCsmShadowDepthRaw, ivec3(texel, cascadeIndex), 0).r;
}

#ifndef MECRAFT_SHADOW_OPAQUE_ONLY
float sampleCsmDepthAllRawTexel(vec2 uv, int cascadeIndex) {
    ivec2 texel = sampleCsmTexelCoord(uv, cascadeIndex, uCsmShadowDepthAllRaw);
    return texelFetch(uCsmShadowDepthAllRaw, ivec3(texel, cascadeIndex), 0).r;
}

vec4 sampleCsmShadowColor0RawTexel(vec2 uv, int cascadeIndex) {
    ivec2 texel = sampleCsmTexelCoord(uv, cascadeIndex, uCsmShadowColor0);
    return texelFetch(uCsmShadowColor0, ivec3(texel, cascadeIndex), 0);
}

vec4 sampleCsmShadowColor1RawTexel(vec2 uv, int cascadeIndex) {
    ivec2 texel = sampleCsmTexelCoord(uv, cascadeIndex, uCsmShadowColor1);
    return texelFetch(uCsmShadowColor1, ivec3(texel, cascadeIndex), 0);
}
#endif

vec2 csmRotateOffset(vec2 offset, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(offset.x * c - offset.y * s,
                offset.x * s + offset.y * c);
}

float csmKernelAngle(vec2 uv, int cascadeIndex, vec2 texelUv) {
    return (0.125 + float(cascadeIndex) * 0.173205) * TAU;
}

float sampleCsmPcfPoisson(vec2 uv, int cascadeIndex, float refZ, vec2 texelUv, float radius) {
    const vec2 offsets[12] = vec2[12](
        vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
        vec2(-0.203,  0.621), vec2( 0.962, -0.195), vec2( 0.473, -0.480),
        vec2( 0.519,  0.767), vec2( 0.185, -0.893), vec2( 0.507,  0.064),
        vec2( 0.896,  0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598)
    );

    float angle = csmKernelAngle(uv, cascadeIndex, texelUv);
    float lit = sampleCsmDepthCompare(uv, cascadeIndex, refZ) * 2.0;
    float weight = 2.0;
    for (int i = 0; i < 12; ++i) {
        vec2 offset = csmRotateOffset(offsets[i], angle) * texelUv * radius;
        lit += sampleCsmDepthCompare(uv + offset, cascadeIndex, refZ);
        weight += 1.0;
    }
    return lit / weight;
}

#ifdef MECRAFT_SHADOW_ENABLE_STANDARD_SAMPLE
// Cascade-specific PCF radius: near cascades get softer shadows, far cascades get sharper.
float cascadePcfRadius(int cascadeIndex, float baseSoftness) {
    float softness = clamp(baseSoftness, 0.0, 3.0);
    float cascadeScale = max(0.85, 1.30 - float(cascadeIndex) * 0.13);
    return max(0.75, (0.70 + softness * 0.45) * cascadeScale);
}

// ---- PCSS (Percentage Closer Soft Shadows) ----

float sampleCsmRawDepth(vec2 uv, int cascadeIndex) {
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    return texture(uCsmShadowDepthRaw, vec3(uv * scale, float(cascadeIndex))).r;
}

// Blocker search: find average depth of blockers in a neighborhood.
float pcssBlockerSearch(vec2 uv, int cascadeIndex, float blockerCompareZ,
                        vec2 texelUv, float searchRadius,
                        out int blockerCount) {
    float avgBlockerDepth = 0.0;
    float blockerWeight = 0.0;
    blockerCount = 0;
    const vec2 offsets[12] = vec2[12](
        vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
        vec2(-0.203,  0.621), vec2( 0.962, -0.195), vec2( 0.473, -0.480),
        vec2( 0.519,  0.767), vec2( 0.185, -0.893), vec2( 0.507,  0.064),
        vec2( 0.896,  0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598)
    );
    float angle = csmKernelAngle(uv, cascadeIndex, texelUv) + 0.618;
    for (int i = 0; i < 12; ++i) {
        vec2 kernel = csmRotateOffset(offsets[i], angle);
        vec2 sampleUv = uv + kernel * texelUv * searchRadius;
        float depth = sampleCsmRawDepth(sampleUv, cascadeIndex);
        if (depth < blockerCompareZ && depth < 0.9999) {
            float weight = mix(1.0, 0.65, saturate(dotSelf(kernel)));
            avgBlockerDepth += depth * weight;
            blockerWeight += weight;
            ++blockerCount;
        }
    }
    if (blockerCount == 0 || blockerWeight <= 0.0) return -1.0; // no blockers
    return avgBlockerDepth / blockerWeight;
}

// Estimate penumbra width from blocker separation.
// CSM depth is linear orthographic depth in [0, 1], so convert the depth delta
// back into approximate world units before turning it into a texel radius.
float pcssPenumbraTexels(float receiverZ, float avgBlockerZ,
                         float depthExtent, float texelWorld,
                         float lightAngularScale) {
    float blockerGapWorld = max(receiverZ - avgBlockerZ, 0.0) * (2.0 * depthExtent);
    float penumbraWorld = blockerGapWorld * lightAngularScale;
    return clamp(0.85 + penumbraWorld / max(texelWorld, 0.0001), 0.85, 6.0);
}

float sampleCsmPcss(vec2 uv, int cascadeIndex,
                    float receiverZ, float refZ,
                    vec2 texelUv, float texelWorld, float depthExtent,
                    float lightAngularScale, float searchRadius,
                    out float outBlockerDepth) {
    // Step 1: Blocker search
    // Search more conservatively than the final PCF compare. Otherwise the
    // receiver's own depth can become a near blocker after normal offset and
    // PCSS amplifies that into dirty gray patches on lit flat surfaces.
    float receiverBias = max(receiverZ - refZ, 2.0e-5);
    float blockerCompareZ = receiverZ - receiverBias * 1.35;
    int blockerCount = 0;
    float avgBlocker = pcssBlockerSearch(uv, cascadeIndex, blockerCompareZ, texelUv, searchRadius, blockerCount);
    if (avgBlocker < 0.0) {
        // No blockers — fully lit
        outBlockerDepth = 0.0;
        return 1.0;
    }
    // Signed world-space delta: negative when the blocker is closer to the
    // light than the receiver. CalculateSubsurfaceScattering expects the
    // DerivativeMain BlockerSearch.y convention after shadowProjectionInverse
    // scaling, not a normalized CSM depth delta. Feeding normalized [0, 1]
    // depth made foliage SSS barely attenuate and caused shadowed cutouts to
    // glow near the camera.
    float blockerGapWorld = max((receiverZ - avgBlocker) * (2.0 * depthExtent), 0.0);
    float minSssGapWorld = max(texelWorld * 2.0, 0.10);
    outBlockerDepth = (blockerGapWorld > minSssGapWorld)
        ? -clamp(blockerGapWorld, 0.0, 64.0)
        : 0.0;

    // Step 2: Penumbra estimation
    float pcfRadius = pcssPenumbraTexels(receiverZ, avgBlocker, depthExtent,
                                         texelWorld, lightAngularScale);
    float blockerConfidence = smoothstep(1.0, 5.0, float(blockerCount));
    pcfRadius = mix(0.85, pcfRadius, blockerConfidence);

    // Step 3: Variable PCF
    float lit = 0.0;
    float angle = csmKernelAngle(uv + vec2(0.137, 0.071), cascadeIndex, texelUv);
    const vec2 offsets[16] = vec2[16](
        vec2(-0.942, -0.399), vec2( 0.946, -0.769), vec2(-0.094, -0.929), vec2( 0.345,  0.294),
        vec2(-0.916,  0.458), vec2(-0.816, -0.879), vec2(-0.383,  0.276), vec2( 0.974,  0.756),
        vec2( 0.443, -0.975), vec2( 0.537, -0.474), vec2(-0.264, -0.418), vec2( 0.792,  0.190),
        vec2(-0.242,  0.998), vec2(-0.814,  0.914), vec2( 0.200,  0.786), vec2( 0.143, -0.142)
    );
    lit += sampleCsmDepthCompare(uv, cascadeIndex, refZ) * 1.5;
    float weight = 1.5;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = csmRotateOffset(offsets[i], angle) * texelUv * pcfRadius;
        lit += sampleCsmDepthCompare(uv + offset,
                                     cascadeIndex, refZ);
        weight += 1.0;
    }
    return lit / weight;
}

float sampleCsmCascadeLit(vec3 worldPos, vec3 normal, float ndotl,
                           float viewDistance, int cascadeIndex,
                           out float outProjectionFade, out float outBlockerDepth) {
    outProjectionFade = 0.0;
    outBlockerDepth = 0.0;
    float texelWorld = max(uCsmCascades[cascadeIndex].texelWorldSize, 0.0001);
    float normalOffset = shadowNormalOffsetWorld(ndotl, viewDistance, texelWorld,
                                                 uShadowDistance, uShadowNormalOffset);
    vec3 proj = csmProjectWorld(worldPos + normal * normalOffset, cascadeIndex);
    if (shadowProjOutOfBounds(proj)) {
        return -1.0;
    }

    ivec3 shadowSize = textureSize(uCsmShadowMap, 0);
    float scale = uCsmCascades[cascadeIndex].resolutionScale;
    vec2 texelUv = (1.0 / vec2(max(shadowSize.x, 1), max(shadowSize.y, 1))) / scale;
    float projectionFade = csmProjectionFade(proj, texelUv);
    if (projectionFade <= 0.001) {
        return -1.0;
    }
    outProjectionFade = projectionFade;

    float bias = csmDepthBias(ndotl, viewDistance, cascadeIndex, shadowSize,
                              uShadowDistance, uShadowConstantBias, uShadowSlopeBias);
    float receiverZ = proj.z;
    float refZ = receiverZ - bias;
    float lit;
    if (uSoftShadowsEnabled == 0) {
        lit = sampleCsmDepthCompare(proj.xy, cascadeIndex, refZ);
    } else if (uPcssShadowsEnabled != 0 && cascadeIndex == 0) {
        // PCSS early-out: if center sample is fully lit, skip expensive blocker search.
        float centerLit = sampleCsmDepthCompare(proj.xy, cascadeIndex, refZ);
        if (centerLit > 0.999) {
            outBlockerDepth = 0.0;
            lit = 1.0;
        } else {
            // PCSS for near cascade. Keep the sun angular scale conservative so
            // contact shadows remain crisp and only separated casters get soft.
            float radiusWorld = texelWorld * float(max(shadowSize.x, 1)) * 0.5;
            float depthExtent = max(uShadowDistance + radiusWorld * 3.0, 1.0);
            float strength = clamp(uShadowPcssStrength, 0.0, 1.5);
            float lightAngularScale = 0.010 + strength * 0.028;
            float searchRadius = 1.75 + strength * 2.75;
            lit = sampleCsmPcss(proj.xy, cascadeIndex, receiverZ, refZ, texelUv,
                                texelWorld, depthExtent, lightAngularScale, searchRadius,
                                outBlockerDepth);
        }
    } else {
        if (cascadeIndex >= 2) {
            // Far cascades: 4-tap PCF (texels already coarse, 12-tap is wasted)
            float r = cascadePcfRadius(cascadeIndex, uShadowSoftness) * 0.7;
            float angle = csmKernelAngle(proj.xy, cascadeIndex, texelUv);
            lit = sampleCsmDepthCompare(proj.xy, cascadeIndex, refZ);
            lit += sampleCsmDepthCompare(proj.xy + csmRotateOffset(vec2(-0.7, 0.0), angle) * texelUv * r, cascadeIndex, refZ);
            lit += sampleCsmDepthCompare(proj.xy + csmRotateOffset(vec2(0.7, 0.0), angle) * texelUv * r, cascadeIndex, refZ);
            lit += sampleCsmDepthCompare(proj.xy + csmRotateOffset(vec2(0.0, 0.7), angle) * texelUv * r, cascadeIndex, refZ);
            lit *= 0.25;
        } else {
            lit = sampleCsmPcfPoisson(proj.xy, cascadeIndex, refZ, texelUv,
                                      cascadePcfRadius(cascadeIndex, uShadowSoftness));
        }
    }
    return clamp(lit, 0.0, 1.0);
}

ShadowSample sampleCsmShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    ShadowSample result;
    result.visibility = 1.0;
    result.cascadeIndex = max(uCsmCascadeCount - 1, 0);
    result.fade = 0.0;
    result.blockerDepth = 0.0;
    result.sssWeight = 0.0;

    lightDir = normalize(lightDir);
    normal = normalize(normal);

    vec3 cameraRelPos = worldPos - uCameraPos;
    float viewDistance = length(cameraRelPos);
    // pow4 for softer distance fade (original pow16 = x^8 was too aggressive at far distances)
    float distanceFade = saturate(pow(rcp(uShadowDistance * uShadowDistance) * dotSelf(cameraRelPos), 4.0));
    if (distanceFade >= 0.999) {
        return result;
    }

    float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 1.0e-3) {
        return result;
    }

    // Select cascade with dithered selection.
    float dither = fract(dot(floor(gl_FragCoord.xy), vec2(0.754877669, 0.569840296)));
    int cascadeIndex = selectCsmCascadeDithered(viewDistance, dither);
    result.cascadeIndex = cascadeIndex;

    // Sample primary cascade.
    float primaryFade, primaryBlocker;
    float litPrimary = sampleCsmCascadeLit(worldPos, normal, ndotl, viewDistance, cascadeIndex, primaryFade, primaryBlocker);
    if (litPrimary < 0.0) {
        return result;
    }
    result.blockerDepth = primaryBlocker;
    result.sssWeight = (cascadeIndex == 0 && primaryBlocker < -1.0e-5) ? 1.0 : 0.0;

    float lit = litPrimary;
    float projectionFade = primaryFade;

    // Distance fade for the last cascade.
    if (cascadeIndex == uCsmCascadeCount - 1) {
        projectionFade *= oneMinus(distanceFade);
    }

    result.visibility = clamp(lit * projectionFade, 0.0, 1.0);
    result.fade = projectionFade;
    result.sssWeight *= projectionFade;
    return result;
}
#endif
#endif

vec3 csmCascadeColor(int cascadeIndex) {
    if (cascadeIndex == 0) return vec3(0.18, 0.58, 1.0);
    if (cascadeIndex == 1) return vec3(0.20, 0.86, 0.36);
    if (cascadeIndex == 2) return vec3(1.0, 0.74, 0.22);
    return vec3(1.0, 0.22, 0.42);
}

#endif // MECRAFT_SHADOW_GLSL
