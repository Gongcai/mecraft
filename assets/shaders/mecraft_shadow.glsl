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
uniform sampler2DArrayShadow uCsmShadowMap;
#endif

struct ShadowSample {
    float visibility;
    int cascadeIndex;
    float fade;
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
ShadowSample sampleCsmShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    ShadowSample result;
    result.visibility = 1.0;
    result.cascadeIndex = max(uCsmCascadeCount - 1, 0);
    result.fade = 0.0;

    lightDir = normalize(lightDir);
    normal = normalize(normal);

    vec3 cameraRelPos = worldPos - uCameraPos;
    float viewDistance = length(cameraRelPos);
    float distanceFade = saturate(pow16(rcp(uShadowDistance * uShadowDistance) * dotSelf(cameraRelPos)));
    if (distanceFade >= 0.999) {
        return result;
    }

    float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 1.0e-3) {
        return result;
    }

    int cascadeIndex = selectCsmCascade(viewDistance);
    result.cascadeIndex = cascadeIndex;

    float texelWorld = max(uCsmCascades[cascadeIndex].texelWorldSize, 0.0001);
    float normalOffset = shadowNormalOffsetWorld(ndotl, viewDistance, texelWorld,
                                                 uShadowDistance, uShadowNormalOffset);
    vec3 proj = csmProjectWorld(worldPos + normal * normalOffset, cascadeIndex);
    if (shadowProjOutOfBounds(proj)) {
        return result;
    }

    ivec3 shadowSize = textureSize(uCsmShadowMap, 0);
    vec2 texelUv = 1.0 / vec2(max(shadowSize.x, 1), max(shadowSize.y, 1));
    float projectionFade = csmProjectionFade(proj, texelUv);
    if (cascadeIndex == uCsmCascadeCount - 1) {
        projectionFade *= oneMinus(distanceFade);
    }
    if (projectionFade <= 0.001) {
        return result;
    }

    float bias = csmDepthBias(ndotl, viewDistance, cascadeIndex, shadowSize,
                              uShadowDistance, uShadowConstantBias, uShadowSlopeBias);
    float refZ = proj.z - bias + shadowDither() * 1.5e-5;
    float lit = 0.0;
    if (uSoftShadowsEnabled == 0) {
        lit = sampleCsmDepthCompare(proj.xy, cascadeIndex, refZ);
    } else {
        lit = sampleCsmPcf3x3(proj.xy, cascadeIndex, refZ, texelUv,
                              max(1.0, uShadowSoftness) * 1.15);
    }

    result.visibility = clamp(lit, 0.0, 1.0);
    result.fade = projectionFade;
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
