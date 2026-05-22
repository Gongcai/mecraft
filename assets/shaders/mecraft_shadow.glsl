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
};

uniform int uCsmCascadeCount;
uniform CsmCascade uCsmCascades[MECRAFT_CSM_CASCADE_COUNT];

#ifndef MECRAFT_SHADOW_NO_SAMPLER
uniform sampler2DArrayShadow uCsmShadowMap;       // shadowtex1: opaque-only depth (comparison)
uniform sampler2DArray uCsmShadowDepthRaw;         // shadowtex1: opaque-only depth (raw)
uniform sampler2DArrayShadow uCsmShadowDepthAll;   // shadowtex0: depth including water (comparison)
uniform sampler2DArray uCsmShadowDepthAllRaw;      // shadowtex0: depth including water (raw)
uniform sampler2DArray uCsmShadowColor0;           // shadowcolor0: RGB tint/caustics, A transparent flag
uniform sampler2DArray uCsmShadowColor1;           // shadowcolor1: RG normal, B skylight, A water height
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

// Cascade selection with transition blend factor.
// Returns: primary cascade index.
// Sets blendNext = 1.0 when sampling from the next cascade should be blended in.
// Sets nextCascade = the next cascade index (or -1 if none).
void selectCsmCascadeBlended(float viewDistance, out int cascadeIndex,
                             out int nextCascade, out float blendNext) {
    cascadeIndex = max(uCsmCascadeCount - 1, 0);
    for (int i = 0; i < MECRAFT_CSM_CASCADE_COUNT; ++i) {
        if (i >= uCsmCascadeCount) break;
        if (viewDistance <= uCsmCascades[i].splitFar) {
            cascadeIndex = i;
            break;
        }
    }

    nextCascade = -1;
    blendNext = 0.0;

    if (cascadeIndex < uCsmCascadeCount - 1) {
        float splitFar = uCsmCascades[cascadeIndex].splitFar;
        float splitNear = uCsmCascades[cascadeIndex].splitNear;
        float cascadeRange = max(splitFar - splitNear, 1.0);
        // Transition zone: last 20% of each cascade's range (wider to hide texel size differences)
        float transitionZone = cascadeRange * 0.20;
        float distFromEnd = splitFar - viewDistance;
        if (distFromEnd < transitionZone) {
            nextCascade = cascadeIndex + 1;
            blendNext = 1.0 - distFromEnd / transitionZone;
            blendNext = smoothstep(0.0, 1.0, blendNext);
        }
    }
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
    return texture(uCsmShadowMap, vec4(uv, float(cascadeIndex), refZ));
}

// Transparent shadow sampling (DerivativeMain shadowtex0/shadowcolor0/1 equivalent)
float sampleCsmDepthAllCompare(vec2 uv, int cascadeIndex, float refZ) {
    return texture(uCsmShadowDepthAll, vec4(uv, float(cascadeIndex), refZ));
}

float sampleCsmDepthAllRaw(vec2 uv, int cascadeIndex) {
    return texture(uCsmShadowDepthAllRaw, vec3(uv, float(cascadeIndex))).r;
}

float sampleCsmDepthRaw(vec2 uv, int cascadeIndex) {
    return texture(uCsmShadowDepthRaw, vec3(uv, float(cascadeIndex))).r;
}

vec4 sampleCsmShadowColor0(vec2 uv, int cascadeIndex) {
    return texture(uCsmShadowColor0, vec3(uv, float(cascadeIndex)));
}

vec4 sampleCsmShadowColor1(vec2 uv, int cascadeIndex) {
    return texture(uCsmShadowColor1, vec3(uv, float(cascadeIndex)));
}

ivec2 sampleCsmTexelCoord(vec2 uv, int cascadeIndex, sampler2DArray shadowTex) {
    ivec3 size = textureSize(shadowTex, 0);
    ivec2 dims = ivec2(max(size.x, 1), max(size.y, 1));
    return clamp(ivec2(floor(uv * vec2(dims))), ivec2(0), dims - ivec2(1));
}

float sampleCsmDepthRawTexel(vec2 uv, int cascadeIndex) {
    ivec2 texel = sampleCsmTexelCoord(uv, cascadeIndex, uCsmShadowDepthRaw);
    return texelFetch(uCsmShadowDepthRaw, ivec3(texel, cascadeIndex), 0).r;
}

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

float sampleCsmPcf3x3(vec2 uv, int cascadeIndex, float refZ, vec2 texelUv, float radius) {
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += sampleCsmDepthCompare(uv + vec2(x, y) * texelUv * radius,
                                         cascadeIndex,
                                         refZ);
        }
    }
    return lit * (1.0 / 9.0);
}

#ifdef MECRAFT_SHADOW_ENABLE_STANDARD_SAMPLE
// Cascade-specific PCF radius: near cascades get softer shadows, far cascades get sharper.
float cascadePcfRadius(int cascadeIndex, float baseSoftness) {
    // Cascade 0: 2.0x, Cascade 1: 1.5x, Cascade 2: 1.2x, Cascade 3: 1.0x
    float scale = max(1.0, 2.0 - float(cascadeIndex) * 0.35);
    return max(1.0, baseSoftness) * scale;
}

// ---- PCSS (Percentage Closer Soft Shadows) ----

float sampleCsmRawDepth(vec2 uv, int cascadeIndex) {
    return texture(uCsmShadowDepthRaw, vec3(uv, float(cascadeIndex))).r;
}

// Blocker search: find average depth of blockers in a neighborhood.
float pcssBlockerSearch(vec2 uv, int cascadeIndex, float blockerCompareZ,
                        vec2 texelUv, float searchRadius) {
    float avgBlockerDepth = 0.0;
    int blockerCount = 0;
    // 4x4 Poisson-like search for performance
    const vec2 offsets[8] = vec2[8](
        vec2(-0.7, -0.7), vec2( 0.7, -0.7), vec2(-0.7,  0.7), vec2( 0.7,  0.7),
        vec2(-1.0,  0.0), vec2( 1.0,  0.0), vec2( 0.0, -1.0), vec2( 0.0,  1.0)
    );
    for (int i = 0; i < 8; ++i) {
        vec2 sampleUv = uv + offsets[i] * texelUv * searchRadius;
        float depth = sampleCsmRawDepth(sampleUv, cascadeIndex);
        if (depth < blockerCompareZ) {
            avgBlockerDepth += depth;
            ++blockerCount;
        }
    }
    if (blockerCount == 0) return -1.0; // no blockers
    return avgBlockerDepth / float(blockerCount);
}

// Estimate penumbra width from blocker separation.
// CSM depth is linear orthographic depth in [0, 1], so convert the depth delta
// back into approximate world units before turning it into a texel radius.
float pcssPenumbraTexels(float receiverZ, float avgBlockerZ,
                         float depthExtent, float texelWorld,
                         float lightAngularScale) {
    float blockerGapWorld = max(receiverZ - avgBlockerZ, 0.0) * (2.0 * depthExtent);
    float penumbraWorld = blockerGapWorld * lightAngularScale;
    return clamp(1.0 + penumbraWorld / max(texelWorld, 0.0001), 1.0, 8.0);
}

float sampleCsmPcss(vec2 uv, int cascadeIndex,
                    float receiverZ, float refZ,
                    vec2 texelUv, float texelWorld, float depthExtent,
                    float lightAngularScale, float searchRadius,
                    out float outBlockerDepth) {
    // Step 1: Blocker search
    float blockerCompareZ = receiverZ - max(receiverZ - refZ, 2.0e-5) * 0.35;
    float avgBlocker = pcssBlockerSearch(uv, cascadeIndex, blockerCompareZ, texelUv, searchRadius);
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

    // Step 3: Variable PCF
    float lit = 0.0;
    // 4x4 rotated PCF kernel
    const vec2 offsets[16] = vec2[16](
        vec2(-1.5, -1.5), vec2(-0.5, -1.5), vec2( 0.5, -1.5), vec2( 1.5, -1.5),
        vec2(-1.5, -0.5), vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 1.5, -0.5),
        vec2(-1.5,  0.5), vec2(-0.5,  0.5), vec2( 0.5,  0.5), vec2( 1.5,  0.5),
        vec2(-1.5,  1.5), vec2(-0.5,  1.5), vec2( 0.5,  1.5), vec2( 1.5,  1.5)
    );
    for (int i = 0; i < 16; ++i) {
        lit += sampleCsmDepthCompare(uv + offsets[i] * texelUv * pcfRadius,
                                     cascadeIndex, refZ);
    }
    return lit * (1.0 / 16.0);
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
    vec2 texelUv = 1.0 / vec2(max(shadowSize.x, 1), max(shadowSize.y, 1));
    float projectionFade = csmProjectionFade(proj, texelUv);
    if (projectionFade <= 0.001) {
        return -1.0;
    }
    outProjectionFade = projectionFade;

    float bias = csmDepthBias(ndotl, viewDistance, cascadeIndex, shadowSize,
                              uShadowDistance, uShadowConstantBias, uShadowSlopeBias);
    float dither = shadowDither();
    float receiverZ = proj.z;
    float refZ = receiverZ - bias + dither * 1.5e-5;
    float lit;
    if (uSoftShadowsEnabled == 0) {
        lit = sampleCsmDepthCompare(proj.xy, cascadeIndex, refZ);
    } else if (uPcssShadowsEnabled != 0 && cascadeIndex == 0) {
        // PCSS for near cascade. Keep the sun angular scale conservative so
        // contact shadows remain crisp and only separated casters get soft.
        float radiusWorld = texelWorld * float(max(shadowSize.x, 1)) * 0.5;
        float depthExtent = max(uShadowDistance + radiusWorld * 3.0, 1.0);
        float strength = clamp(uShadowPcssStrength, 0.0, 1.5);
        // Reduced angular scale multiplier (0.035 vs 0.052) to keep near shadows crisper
        float lightAngularScale = 0.012 + strength * 0.035;
        float searchRadius = 2.0 + strength * 3.5;
        lit = sampleCsmPcss(proj.xy, cascadeIndex, receiverZ, refZ, texelUv,
                            texelWorld, depthExtent, lightAngularScale, searchRadius,
                            outBlockerDepth);
    } else {
        lit = sampleCsmPcf3x3(proj.xy, cascadeIndex, refZ, texelUv,
                              cascadePcfRadius(cascadeIndex, uShadowSoftness));
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

    // Select cascade with transition blend at split boundaries.
    int cascadeIndex, nextCascade;
    float blendNext;
    selectCsmCascadeBlended(viewDistance, cascadeIndex, nextCascade, blendNext);
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

    // Blend with next cascade at split boundary.
    if (nextCascade >= 0 && blendNext > 0.001) {
        float nextFade, nextBlocker;
        float litNext = sampleCsmCascadeLit(worldPos, normal, ndotl, viewDistance, nextCascade, nextFade, nextBlocker);
        if (litNext >= 0.0) {
            lit = mix(litPrimary, litNext, blendNext);
            projectionFade = mix(primaryFade, nextFade, blendNext);
            result.blockerDepth = mix(primaryBlocker, nextBlocker, blendNext);
            float nextSssWeight = (nextCascade == 0 && nextBlocker < -1.0e-5) ? 1.0 : 0.0;
            result.sssWeight = mix(result.sssWeight, nextSssWeight, blendNext);
        } else {
            // Cascade 0 is the only cascade that produces PCSS blocker depth.
            // Fade SSS out through the transition window even when the next
            // cascade sample is rejected, otherwise foliage can glow only at
            // the split distance.
            result.sssWeight *= oneMinus(blendNext);
        }
    }

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
