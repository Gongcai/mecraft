#version 450 core
#include "gbuffer_contract.glsl"
#include "derivative_shadow.glsl"
#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 1) in vec2 vClipUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uReflectionTex;
layout(binding = 1) uniform sampler2D uDepthTex;
layout(binding = 2) uniform sampler2D uNormalAoTex;
layout(binding = 3) uniform sampler2D uMaterialTex;
layout(binding = 4) uniform sampler2D uMaterialAuxTex;

layout(push_constant) uniform RhiPushConstants {
    mat4 uInvViewProj;
    vec4 uScreenFilterParams;
    vec4 uCameraNearPlane;
    vec4 uFarPlanePadding;
    vec4 uFilterPadding;
};

vec3 reconstructNormal(vec4 packedNormalAo) {
    return unpackGBufferNormal(packedNormalAo);
}

vec3 reconstructWorldPosition(vec2 clipUv, float depth) {
    vec4 clip = vec4(clipUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float linearDepthFromDepth(float depth) {
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * uCameraNearPlane.w * uFarPlanePadding.x) /
           max(uFarPlanePadding.x + uCameraNearPlane.w -
               ndc * (uFarPlanePadding.x - uCameraNearPlane.w), 0.0001);
}

vec4 derivativeReflectionFilter(ivec2 texel,
                                vec4 reflectionData,
                                float roughness,
                                vec3 normal,
                                vec3 viewDir,
                                float size,
                                vec2 dither) {
    // DerivativeMain/lib/Surface/ReflectionFilter.glsl, adapted from view-space
    // depth/normals to Mecraft's world-space G-buffer contract.
    const ivec2 offsets[8] = ivec2[](
        ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1),
        ivec2(-1,  0),              ivec2(1,  0),
        ivec2(-1,  1), ivec2(0,  1), ivec2(1,  1)
    );

    float smoothness = 1.0 - sqrt(roughness);
    float linearDepth = linearDepthFromDepth(texelFetch(uDepthTex, texel, 0).r);
    float nDotV = saturate(dot(-viewDir, normal));

    float coordOffset = 8.0 * size * min(roughness * 20.0, 1.0) *
                        oneMinus(fastExp(-sqrt(reflectionData.a) * 50.0));
    coordOffset *= reflectionData.a * 0.8 + 0.2;

    float sharpenWeight = reflectionData.a * 0.475 + 0.025;
    float roughnessInv = 1e2 / max(roughness, 1e-5);
    float safeCoordOffset = max(coordOffset, 1e-4);

    vec4 filtered = reflectionData;
    filtered.rgb = pow(dotSelf(filtered.rgb), 0.5 * sharpenWeight) *
                   normalize(max(filtered.rgb, vec3(1e-6)));
    float sumWeight = 1.0;

    ivec2 maxTexel = ivec2(uScreenFilterParams.xy) - ivec2(1);
    for (int i = 0; i < 8; ++i) {
        ivec2 sampleTexel = clamp(texel + ivec2((vec2(offsets[i]) + dither) * coordOffset),
                                  ivec2(0), maxTexel);

        vec4 sampleData = texelFetch(uReflectionTex, sampleTexel, 0);
        vec3 sampleNormal = reconstructNormal(texelFetch(uNormalAoTex, sampleTexel, 0));
        float sampleLinearDepth = linearDepthFromDepth(texelFetch(uDepthTex, sampleTexel, 0).r);

        float weight = pow(max(dot(normal, sampleNormal), 1e-6), roughnessInv) *
                       fastExp(-abs(reflectionData.a - sampleData.a) * smoothness) *
                       fastExp(-abs(sampleLinearDepth - linearDepth) * 2.0 *
                               nDotV * inversesqrt(safeCoordOffset));

        vec3 sampleRgb = pow(dotSelf(sampleData.rgb), 0.5 * sharpenWeight) *
                         normalize(max(sampleData.rgb, vec3(1e-6)));
        filtered += vec4(sampleRgb, sampleData.a) * weight;
        sumWeight += weight;
    }

    if (sumWeight < 1e-3) {
        return reflectionData;
    }

    filtered /= sumWeight;
    filtered.rgb = pow(dotSelf(filtered.rgb), 0.5 / sharpenWeight) *
                   normalize(max(filtered.rgb, vec3(1e-6)));
    return filtered;
}

void main() {
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);
    vec4 reflection = texture(uReflectionTex, textureUv);
    float depth = texture(uDepthTex, textureUv).r;

    // Sky pixels: pass through
    if (depth >= 0.9999) {
        FragColor = reflection;
        return;
    }

    SurfaceMaterialAux centerAux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, textureUv));
    TranslucentMask centerTransMask = decodeTranslucentMask(centerAux.materialKind);
    if (centerTransMask.isTranslucent) {
        FragColor = reflection;
        return;
    }

    vec3 centerNormal = reconstructNormal(texture(uNormalAoTex, textureUv));
    SurfaceMaterial centerMaterial = unpackGBufferMaterial(texture(uMaterialTex, textureUv));
    float centerRoughness = clamp(centerMaterial.roughness, 0.0, 1.0);
    float centerWetness = clamp(centerAux.wetnessMask, 0.0, 1.0);
    bool centerCanReceiveRain = !centerTransMask.isTranslucent &&
                                materialKindId(centerAux.materialKind) != MATERIAL_SKIN;
    if (centerCanReceiveRain && centerWetness > 1e-4 && uScreenFilterParams.w > 1e-2) {
        float wetRoughnessScale = oneMinus(clamp(uScreenFilterParams.w, 0.0, 1.0) * 0.3);
        centerRoughness = min(sqr(sqrt(centerRoughness) * wetRoughnessScale),
                              sqr(oneMinus(centerWetness) * wetRoughnessScale));
    }

    float reflectionMask = max(0.625 - centerRoughness, 0.0) + centerAux.metalness;
    bool materialHasReflections = reflectionMask > 0.005;
    bool materialIsRough = centerRoughness > 0.005;
    if (!materialHasReflections || reflection.a <= 1e-3 || !materialIsRough || uScreenFilterParams.z <= 1e-4) {
        FragColor = reflection;
        return;
    }

    vec3 worldPos = reconstructWorldPosition(vClipUv, depth);
    vec3 viewDir = normalize(worldPos - uCameraNearPlane.xyz);
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec4 filtered = derivativeReflectionFilter(texel, reflection, centerRoughness,
                                               centerNormal, viewDir, 1.0, vec2(0.0));
    // DerivativeMain's rain rings are already written into the G-buffer normal
    // before reflection. In Mecraft the separate reflection filter can otherwise
    // blur that animated normal contrast back into a soft puddle blob.
    float wetRipplePreserve = (centerCanReceiveRain ? smoothstep(0.08, 0.55, centerWetness) : 0.0) *
                              clamp(uScreenFilterParams.w, 0.0, 1.0);
    float filterStrength = clamp(uScreenFilterParams.z, 0.0, 1.0) * mix(1.0, 0.18, wetRipplePreserve);
    FragColor = mix(reflection, filtered, filterStrength);
}
