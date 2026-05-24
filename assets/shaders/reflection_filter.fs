#version 450 core
#include "gbuffer_contract.glsl"
#include "derivative_shadow.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uReflectionTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;

uniform vec2 uScreenSize;
uniform float uFilterStrength;
uniform float uSurfaceWetness;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform float uNearPlane;
uniform float uFarPlane;

vec3 reconstructNormal(vec3 packedNormal) {
    return normalize(packedNormal * 2.0 - 1.0);
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float linearDepthFromDepth(float depth) {
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane) /
           max(uFarPlane + uNearPlane - ndc * (uFarPlane - uNearPlane), 0.0001);
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

    ivec2 maxTexel = ivec2(uScreenSize) - ivec2(1);
    for (int i = 0; i < 8; ++i) {
        ivec2 sampleTexel = clamp(texel + ivec2((vec2(offsets[i]) + dither) * coordOffset),
                                  ivec2(0), maxTexel);

        vec4 sampleData = texelFetch(uReflectionTex, sampleTexel, 0);
        vec3 sampleNormal = reconstructNormal(texelFetch(uNormalAoTex, sampleTexel, 0).rgb);
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
    vec4 reflection = texture(uReflectionTex, vTexCoord);
    float depth = texture(uDepthTex, vTexCoord).r;

    // Sky pixels: pass through
    if (depth >= 0.9999) {
        FragColor = reflection;
        return;
    }

    SurfaceMaterialAux centerAux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
    TranslucentMask centerTransMask = decodeTranslucentMask(centerAux.materialKind);
    if (centerTransMask.isTranslucent) {
        FragColor = reflection;
        return;
    }

    vec3 centerNormal = reconstructNormal(texture(uNormalAoTex, vTexCoord).rgb);
    SurfaceMaterial centerMaterial = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
    float centerRoughness = clamp(centerMaterial.roughness, 0.0, 1.0);
    float centerWetness = clamp(centerAux.wetnessMask, 0.0, 1.0);
    bool centerCanReceiveRain = !centerTransMask.isTranslucent &&
                                materialKindId(centerAux.materialKind) != MATERIAL_SKIN;
    if (centerCanReceiveRain && centerWetness > 1e-4 && uSurfaceWetness > 1e-2) {
        float wetRoughnessScale = oneMinus(clamp(uSurfaceWetness, 0.0, 1.0) * 0.3);
        centerRoughness = min(sqr(sqrt(centerRoughness) * wetRoughnessScale),
                              sqr(oneMinus(centerWetness) * wetRoughnessScale));
    }

    float reflectionMask = max(0.625 - centerRoughness, 0.0) + centerAux.metalness;
    bool materialHasReflections = reflectionMask > 0.005;
    bool materialIsRough = centerRoughness > 0.005;
    if (!materialHasReflections || reflection.a <= 1e-3 || !materialIsRough || uFilterStrength <= 1e-4) {
        FragColor = reflection;
        return;
    }

    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec3 viewDir = normalize(worldPos - uCameraPos);
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec4 filtered = derivativeReflectionFilter(texel, reflection, centerRoughness,
                                               centerNormal, viewDir, 1.0, vec2(0.0));
    // DerivativeMain's rain rings are already written into the G-buffer normal
    // before reflection. In Mecraft the separate reflection filter can otherwise
    // blur that animated normal contrast back into a soft puddle blob.
    float wetRipplePreserve = (centerCanReceiveRain ? smoothstep(0.08, 0.55, centerWetness) : 0.0) *
                              clamp(uSurfaceWetness, 0.0, 1.0);
    float filterStrength = clamp(uFilterStrength, 0.0, 1.0) * mix(1.0, 0.18, wetRipplePreserve);
    FragColor = mix(reflection, filtered, filterStrength);
}
