#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uVoxelLightTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uDepthTex;
uniform sampler2D uShadowMap;
uniform sampler2D uSsaoTex;
uniform sampler2D uSceneLightingTex;
uniform sampler2D uSceneCompositeTex;
uniform sampler2D uSceneResolvedTex;
uniform sampler2D uTransparentCompositeTex;
uniform sampler2D uTransparentCompositeDepthTex;
uniform sampler2D uVolumetricTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uHistorySceneTex;
uniform sampler2D uHistoryDepthTex;
uniform sampler2D uReflectionTex;
uniform sampler2D uCloudTex;
uniform sampler2D uHistoryReflectionTex;
uniform sampler2D uHistoryCloudTex;
uniform mat4 uShadowViewProj;
uniform mat4 uShadowProjectionInverse;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform float uShadowExtent;
uniform float uShadowTexelWorldSize;
uniform float uShadowMapSize;
uniform float uShadowDistance;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
uniform float uShadowNormalOffset;
uniform int uShadowLightMode;
uniform int uShadowWarpMode;
uniform int uDebugViewMode;

vec3 tonemapPreview(vec3 color) {
    color = max(color, vec3(0.0));
    return color / (color + vec3(1.0));
}

vec3 heatmap(float v) {
    v = clamp(v, 0.0, 1.0);
    vec3 a = mix(vec3(0.02, 0.04, 0.18), vec3(0.05, 0.35, 0.95), smoothstep(0.0, 0.35, v));
    vec3 b = mix(vec3(0.05, 0.35, 0.95), vec3(0.95, 0.86, 0.18), smoothstep(0.35, 0.72, v));
    vec3 c = mix(vec3(0.95, 0.86, 0.18), vec3(1.0, 0.08, 0.02), smoothstep(0.72, 1.0, v));
    return v < 0.35 ? a : (v < 0.72 ? b : c);
}

float linearizeDepthPreview(float depth) {
    if (depth >= 0.9999) {
        return 1.0;
    }
    float ndc = depth * 2.0 - 1.0;
    float nearPlane = 0.05;
    float farPlane = 512.0;
    float linearDepth = (2.0 * nearPlane * farPlane) / max(farPlane + nearPlane - ndc * (farPlane - nearPlane), 0.0001);
    return clamp(linearDepth / 192.0, 0.0, 1.0);
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 world = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return world.xyz / max(world.w, 0.0001);
}

float calculateShadowWarp(vec2 coord) {
    if (uShadowWarpMode == 2) {
        return 1.0;
    }
    if (uShadowWarpMode == 1) {
        vec2 scaled = coord * 1.165;
        float quarticLength = pow(dot(scaled * scaled, scaled * scaled), 0.25);
        return quarticLength * 0.9 + 0.1;
    }
    return length(coord * 1.169) * 0.9 + 0.1;
}

vec3 shadowUvFromWorld(vec3 worldPos) {
    vec4 shadowClip = uShadowViewProj * vec4(worldPos, 1.0);
    vec3 clip = shadowClip.xyz / max(shadowClip.w, 0.0001);
    if (uShadowWarpMode != 2) {
        float warp = calculateShadowWarp(clip.xy);
        clip.xy /= warp;
        clip.z *= 0.2;
    }
    return clip * 0.5 + 0.5;
}

float shadowDepthWorldScale() {
    float scale = max(abs(uShadowProjectionInverse[2][2]) * 2.0, 1.0);
    return (uShadowWarpMode != 2) ? scale / 0.2 : scale;
}

float shadowDepthBiasFromWorld(float worldUnits) {
    return worldUnits / shadowDepthWorldScale();
}

float derivativeMinimumShadowBias() {
    return (uShadowWarpMode != 2) ? 1.2e-4 : 6.0e-5;
}

float shadowWorldBias(float ndotl, float viewDistance) {
    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    float slope = 1.0 - clamp(ndotl, 0.0, 1.0);
    float receiverScale = 1.0 + 0.25 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    return texelWorld * receiverScale *
           (uShadowConstantBias * 48.0 + uShadowSlopeBias * 64.0 * slope);
}

float shadowNormalOffsetWorld(float ndotl, float viewDistance) {
    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    float grazing = 1.0 - clamp(ndotl, 0.0, 1.0);
    float distanceScale = 1.0 + 0.35 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    float requestedTexels = max(uShadowNormalOffset, 0.0) / 0.09375;
    float texelOffset = texelWorld * requestedTexels * distanceScale * (1.0 + 0.85 * grazing);
    float derivativeScale = max(uShadowNormalOffset, 0.0) / 0.035;
    float derivativeOffset = (viewDistance * viewDistance * 8e-5 + 3e-2) *
                             (2.0 - clamp(ndotl, 0.0, 1.0)) *
                             derivativeScale;
    return max(texelOffset, derivativeOffset);
}

bool shadowUvOutOfBounds(vec3 shadowUv) {
    return shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
           shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
           shadowUv.z < 0.0 || shadowUv.z > 1.0;
}

void main() {
    if (uDebugViewMode == 1) {
        FragColor = vec4(texture(uAlbedoTex, vTexCoord).rgb, 1.0);
        return;
    }
    if (uDebugViewMode == 2) {
        FragColor = vec4(texture(uNormalAoTex, vTexCoord).rgb, 1.0);
        return;
    }
    if (uDebugViewMode == 3) {
        float ao = texture(uNormalAoTex, vTexCoord).a;
        FragColor = vec4(vec3(ao), 1.0);
        return;
    }
    if (uDebugViewMode == 4) {
        vec2 light = texture(uVoxelLightTex, vTexCoord).rg;
        FragColor = vec4(light.r, light.g, 0.0, 1.0);
        return;
    }
    if (uDebugViewMode == 5) {
        SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
        FragColor = vec4(material.roughness, material.f0 * 3.0, material.emission, 1.0);
        return;
    }
    if (uDebugViewMode == 6) {
        SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
        FragColor = vec4(heatmap(material.sss), 1.0);
        return;
    }
    if (uDebugViewMode == 7) {
        float depth = texture(uDepthTex, vTexCoord).r;
        FragColor = vec4(heatmap(1.0 - linearizeDepthPreview(depth)), 1.0);
        return;
    }
    if (uDebugViewMode == 8) {
        FragColor = vec4(vec3(texture(uShadowMap, vTexCoord).r), 1.0);
        return;
    }
    if (uDebugViewMode == 9) {
        FragColor = vec4(vec3(texture(uSsaoTex, vTexCoord).r), 1.0);
        return;
    }
    if (uDebugViewMode == 10) {
        FragColor = vec4(tonemapPreview(texture(uSceneLightingTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 11) {
        FragColor = vec4(tonemapPreview(texture(uSceneCompositeTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 12) {
        FragColor = vec4(tonemapPreview(texture(uTransparentCompositeTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 13) {
        float depth = texture(uTransparentCompositeDepthTex, vTexCoord).r;
        FragColor = vec4(heatmap(1.0 - linearizeDepthPreview(depth)), 1.0);
        return;
    }
    if (uDebugViewMode == 14) {
        vec4 volumetric = texture(uVolumetricTex, vTexCoord);
        FragColor = vec4(tonemapPreview(volumetric.rgb * 4.0), 1.0);
        return;
    }
    if (uDebugViewMode == 15) {
        float transmittance = texture(uVolumetricTex, vTexCoord).a;
        FragColor = vec4(vec3(transmittance), 1.0);
        return;
    }
    if (uDebugViewMode == 16) {
        FragColor = vec4(tonemapPreview(texture(uSkyCaptureTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 17) {
        vec2 velocity = texture(uVelocityTex, vTexCoord).rg;
        float speed = length(velocity);
        FragColor = vec4(heatmap(speed * 50.0), 1.0);
        return;
    }
    if (uDebugViewMode == 18) {
        FragColor = vec4(tonemapPreview(texture(uHistorySceneTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 19) {
        float depth = texture(uHistoryDepthTex, vTexCoord).r;
        FragColor = vec4(heatmap(1.0 - linearizeDepthPreview(depth)), 1.0);
        return;
    }
    if (uDebugViewMode == 20) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }

        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 shadowUv = shadowUvFromWorld(worldPos);

        vec3 outOfBounds = vec3(0.0);
        if (shadowUvOutOfBounds(shadowUv))
            outOfBounds = vec3(1.0, 0.0, 0.0);

        float texelDensity = uShadowTexelWorldSize > 0.0
            ? uShadowTexelWorldSize
            : (uShadowExtent * 2.0) / max(uShadowMapSize, 1.0);
        float densityHeat = clamp((texelDensity - 0.025) / 0.20, 0.0, 1.0);
        float edge = min(min(shadowUv.x, 1.0 - shadowUv.x), min(shadowUv.y, 1.0 - shadowUv.y));
        float edgeWarning = 1.0 - smoothstep(0.015, 0.075, edge);

        vec3 coverageColor = heatmap(densityHeat);
        coverageColor = mix(coverageColor, vec3(1.0, 0.55, 0.05), edgeWarning * 0.65);
        FragColor = vec4(mix(coverageColor, outOfBounds, 0.75), 1.0);
        return;
    }
    if (uDebugViewMode == 21) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }

        vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
        vec3 lightDir = normalize(uShadowLightMode == 1 ? uMoonDirection : uSunDirection);
        float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        float viewDistance = length(worldPos - uCameraPos);
        float offsetWorld = shadowNormalOffsetWorld(ndotl, viewDistance);
        vec3 shadowUv = shadowUvFromWorld(worldPos + normal * offsetWorld);

        if (shadowUvOutOfBounds(shadowUv)) {
            FragColor = vec4(0.95, 0.08, 0.02, 1.0);
            return;
        }

        float shadowDepth = texture(uShadowMap, shadowUv.xy).r;
        float bias = max(shadowDepthBiasFromWorld(shadowWorldBias(ndotl, viewDistance)),
                         derivativeMinimumShadowBias());
        float lit = shadowUv.z - bias <= shadowDepth ? 1.0 : 0.0;
        float margin = (shadowDepth - (shadowUv.z - bias)) * shadowDepthWorldScale();
        float nearAcne = 1.0 - smoothstep(0.0, max(uShadowTexelWorldSize * 1.25, 0.0001), abs(margin));
        vec3 litColor = mix(vec3(0.08, 0.12, 0.25), vec3(0.88, 0.92, 1.0), lit);
        FragColor = vec4(mix(litColor, vec3(1.0, 0.58, 0.04), nearAcne * 0.65), 1.0);
        return;
    }
    if (uDebugViewMode == 22) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }

        vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
        vec3 lightDir = normalize(uShadowLightMode == 1 ? uMoonDirection : uSunDirection);
        float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        float viewDistance = length(worldPos - uCameraPos);
        float biasWorld = max(shadowWorldBias(ndotl, viewDistance),
                              derivativeMinimumShadowBias() * shadowDepthWorldScale());
        float offsetWorld = shadowNormalOffsetWorld(ndotl, viewDistance);
        float biasTexels = biasWorld / max(uShadowTexelWorldSize, 0.0001);
        float offsetTexels = offsetWorld / max(uShadowTexelWorldSize, 0.0001);
        FragColor = vec4(heatmap(biasTexels / 4.0).r, heatmap(offsetTexels / 4.0).g, clamp(1.0 - ndotl, 0.0, 1.0), 1.0);
        return;
    }
    if (uDebugViewMode == 23) {
        FragColor = vec4(tonemapPreview(texture(uReflectionTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 24) {
        vec4 cloud = texture(uCloudTex, vTexCoord);
        FragColor = vec4(tonemapPreview(cloud.rgb * 4.0), max(cloud.a, 1.0));
        return;
    }
    if (uDebugViewMode == 25) {
        SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
        FragColor = vec4(heatmap(aux.materialKind / MATERIAL_ID_MAX), 1.0);
        return;
    }
    if (uDebugViewMode == 26) {
        SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
        FragColor = vec4(aux.wetnessMask, aux.porosity, aux.metalness, 1.0);
        return;
    }
    if (uDebugViewMode == 27) {
        FragColor = vec4(tonemapPreview(texture(uHistoryReflectionTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 28) {
        vec4 cloud = texture(uHistoryCloudTex, vTexCoord);
        FragColor = vec4(tonemapPreview(cloud.rgb * 4.0), max(cloud.a, 1.0));
        return;
    }
    if (uDebugViewMode == 29) {
        float reflectionMask = texture(uReflectionTex, vTexCoord).a;
        FragColor = vec4(heatmap(reflectionMask), 1.0);
        return;
    }
    if (uDebugViewMode == 30) {
        FragColor = vec4(tonemapPreview(texture(uSceneResolvedTex, vTexCoord).rgb), 1.0);
        return;
    }

    FragColor = vec4(tonemapPreview(texture(uSceneLightingTex, vTexCoord).rgb), 1.0);
}
