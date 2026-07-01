#version 450 core
#include "gbuffer_contract.glsl"
#define MECRAFT_SHADOW_NO_SAMPLER
#include "mecraft_shadow.glsl"
#include "render_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uVoxelLightTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uDepthTex;
uniform sampler2D uShadowMapRaw;
uniform sampler2D uSsaoTex;
uniform sampler2D uSceneLightingTex;
uniform sampler2D uSceneCompositeTex;
uniform sampler2D uSceneResolvedTex;
uniform sampler2D uTemporalCurrentTex;
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
uniform sampler2D uNoiseTex;
uniform sampler2D uShadowColorTex;
uniform sampler2D uShadowNormalTex;
uniform sampler2D uSsgiTex;
uniform sampler2DArray uCsmShadowDepthTex;
uniform mat4 uShadowModelView;
uniform mat4 uShadowProjection;
uniform mat4 uShadowProjectionInverse;
uniform mat4 uInvViewProj;
uniform float uNearPlane;
uniform float uFarPlane;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uShadowLightDirection;
uniform float uShadowExtent;
uniform float uShadowTexelWorldSize;
uniform float uShadowMapSize;
uniform float uShadowDistance;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
uniform float uShadowNormalOffset;
uniform int uShadowLightMode;
uniform int uDebugViewMode;
uniform int uFrameIndex;
uniform int uFreezeBias;

// Lighting diagnostic uniforms (for debug view 45)
uniform vec3 uSunLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uHorizonScatterColor;
uniform vec3 uFogColor;

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

vec3 reconstructWorldPosition(vec2 uv, float depth);

float vfogLinearDepthFromDepth(float depth) {
    if (depth >= 0.9999) {
        return 1e6;
    }
    return (uNearPlane * uFarPlane) / (depth * (uNearPlane - uFarPlane) + uFarPlane);
}

float vfogViewDistanceFromDepthTexel(ivec2 texel) {
    ivec2 size = textureSize(uDepthTex, 0);
    ivec2 clampedTexel = clamp(texel, ivec2(0), size - ivec2(1));
    return vfogLinearDepthFromDepth(texelFetch(uDepthTex, clampedTexel, 0).r);
}

vec4 debugSpatialUpscaleVolumetric(vec2 uv) {
    vec2 fullCoord = gl_FragCoord.xy;
    float centerLinearDepth = vfogLinearDepthFromDepth(texture(uDepthTex, uv).r);
    ivec2 halfSize = textureSize(uVolumetricTex, 0);

    ivec2 bias = (uFreezeBias != 0)
        ? ivec2(floor(fullCoord)) & ivec2(1)
        : ivec2(fullCoord + float(uFrameIndex)) & ivec2(1);
    ivec2 baseTexel = ivec2(floor(fullCoord * 0.5)) + bias * 2;
    ivec2 offsets[4] = ivec2[](
        ivec2(-2, -2),
        ivec2(-2,  0),
        ivec2( 0,  0),
        ivec2( 0, -2)
    );

    float sigmaZ = 64.0 / max(centerLinearDepth, 1.0);
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;

    for (int i = 0; i < 4; ++i) {
        ivec2 sampleTexel = clamp(baseTexel + offsets[i], ivec2(0), halfSize - ivec2(1));
        float sampleLinearDepth = vfogViewDistanceFromDepthTexel(sampleTexel * 2);
        float weight = max(exp2(-abs(sampleLinearDepth - centerLinearDepth) * sigmaZ), 1e-6);
        sum += texelFetch(uVolumetricTex, sampleTexel, 0) * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 0.0001);
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 world = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return world.xyz / max(world.w, 0.0001);
}

float noise2D(vec2 uv) {
    return texture(uNoiseTex, uv).r;
}

float shadowDither() {
    return noise2D(gl_FragCoord.xy / 256.0);
}

// Single-map shadow helpers for debug views (cascade 0 mirror).

vec3 localShadowUvFromWorld(vec3 worldPos) {
    vec3 viewPos = mat3(uShadowModelView) * worldPos + uShadowModelView[3].xyz;
    vec3 clipPos = vec3(uShadowProjection[0].x, uShadowProjection[1].y, uShadowProjection[2].z) * viewPos + uShadowProjection[3].xyz;
    return clipPos * 0.5 + 0.5;
}

float localShadowWorldBias(float ndotl, float viewDistance) {
    return shadowWorldBias(ndotl, viewDistance, uShadowTexelWorldSize, uShadowDistance,
                           uShadowConstantBias, uShadowSlopeBias);
}

float localShadowNormalOffsetWorld(float ndotl, float viewDistance) {
    return shadowNormalOffsetWorld(ndotl, viewDistance, uShadowTexelWorldSize, uShadowDistance,
                                   uShadowNormalOffset);
}

float selectedShadowNormalOffsetWorld(vec3 cameraRelPos, float ndotl, float viewDistance) {
    return localShadowNormalOffsetWorld(ndotl, viewDistance);
}

float selectedShadowCompareBias(float ndotl, float viewDistance, float dither) {
    return max(localShadowWorldBias(ndotl, viewDistance) / max(uShadowDistance * 2.0, 1.0),
               6.0e-5);
}

float selectedShadowBiasWorld(float ndotl, float viewDistance, float dither) {
    return localShadowWorldBias(ndotl, viewDistance);
}

bool shadowUvOutOfBounds(vec3 shadowUv) {
    return shadowProjOutOfBounds(shadowUv);
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
        FragColor = vec4(vec3(texture(uShadowMapRaw, vTexCoord).r), 1.0);
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
        vec3 shadowUv = localShadowUvFromWorld(worldPos);

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
        vec3 lightDir = normalize(uShadowLightDirection);
        float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 cameraRelPos = worldPos - uCameraPos;
        float viewDistance = length(cameraRelPos);
        float offsetWorld = selectedShadowNormalOffsetWorld(cameraRelPos, ndotl, viewDistance);
        vec3 shadowUv = localShadowUvFromWorld(worldPos + normal * offsetWorld);

        if (shadowUvOutOfBounds(shadowUv)) {
            FragColor = vec4(0.95, 0.08, 0.02, 1.0);
            return;
        }

        float shadowDepth = texture(uShadowMapRaw, shadowUv.xy).r;
        float bias = selectedShadowCompareBias(ndotl, viewDistance, shadowDither());
        float lit = shadowUv.z - bias <= shadowDepth ? 1.0 : 0.0;
        float margin = (shadowDepth - (shadowUv.z - bias)) * uShadowDistance * 2.0;
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
        vec3 lightDir = normalize(uShadowLightDirection);
        float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 cameraRelPos = worldPos - uCameraPos;
        float viewDistance = length(cameraRelPos);
        float biasWorld = selectedShadowBiasWorld(ndotl, viewDistance, shadowDither());
        float offsetWorld = selectedShadowNormalOffsetWorld(cameraRelPos, ndotl, viewDistance);
        float biasTexels = biasWorld / max(uShadowTexelWorldSize, 0.0001);
        float offsetTexels = offsetWorld / max(uShadowTexelWorldSize, 0.0001);
        FragColor = vec4(heatmap(biasTexels / 4.0).r, heatmap(offsetTexels / 4.0).g, clamp(1.0 - ndotl, 0.0, 1.0), 1.0);
        return;
    }
    if (uDebugViewMode == 23) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }

        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        float viewDistance = length(worldPos - uCameraPos);
        int cascadeIndex = selectCsmCascade(viewDistance);
        vec3 proj = csmProjectWorld(worldPos, cascadeIndex);
        vec3 color = csmCascadeColor(cascadeIndex);
        float edge = min(min(proj.x, 1.0 - proj.x), min(proj.y, 1.0 - proj.y));
        float edgeWarning = 1.0 - smoothstep(0.015, 0.075, edge);
        if (shadowProjOutOfBounds(proj)) {
            color = vec3(0.95, 0.08, 0.02);
        } else {
            color = mix(color, vec3(1.0), edgeWarning * 0.45);
        }
        FragColor = vec4(color, 1.0);
        return;
    }
    if (uDebugViewMode == 24) {
        FragColor = vec4(tonemapPreview(texture(uReflectionTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 25) {
        vec4 cloud = texture(uCloudTex, vTexCoord);
        FragColor = vec4(tonemapPreview(cloud.rgb * 4.0), max(cloud.a, 1.0));
        return;
    }
    if (uDebugViewMode == 26) {
        SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
        FragColor = vec4(heatmap(aux.materialKind / MATERIAL_ID_MAX), 1.0);
        return;
    }
    if (uDebugViewMode == 27) {
        SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
        FragColor = vec4(aux.wetnessMask, aux.porosity, aux.metalness, 1.0);
        return;
    }
    if (uDebugViewMode == 28) {
        FragColor = vec4(tonemapPreview(texture(uHistoryReflectionTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 29) {
        vec4 cloud = texture(uHistoryCloudTex, vTexCoord);
        FragColor = vec4(tonemapPreview(cloud.rgb * 4.0), max(cloud.a, 1.0));
        return;
    }
    if (uDebugViewMode == 30) {
        float reflectionMask = texture(uReflectionTex, vTexCoord).a;
        FragColor = vec4(heatmap(reflectionMask), 1.0);
        return;
    }
    if (uDebugViewMode == 31) {
        FragColor = vec4(tonemapPreview(texture(uSceneResolvedTex, vTexCoord).rgb), 1.0);
        return;
    }

    // Debug 32: Shadow UV coordinates + warp density
    // R = shadowUv.x, G = shadowUv.y, B = warp density heatmap
    // Helps identify if ghosting correlates with specific shadow map regions or warp compression.
    if (uDebugViewMode == 32) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }

        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 shadowUv = localShadowUvFromWorld(worldPos);

        if (shadowUvOutOfBounds(shadowUv)) {
            FragColor = vec4(0.95, 0.08, 0.02, 1.0); // red = out of bounds
            return;
        }

        // CSM mode: show shadow UV + cascade index color
        float viewDist = length(worldPos - uCameraPos);
        int cascadeIdx = selectCsmCascade(viewDist);
        FragColor = vec4(shadowUv.x, shadowUv.y, float(cascadeIdx) / 3.0, 1.0);
        return;
    }

    // Debug 33: CSM cascade heatmap (blue=0, green=1, yellow=2, red=3)
    if (uDebugViewMode == 33) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        float viewDist = length(worldPos - uCameraPos);
        int cascadeIdx = selectCsmCascade(viewDist);
        FragColor = vec4(csmCascadeColor(cascadeIdx), 1.0);
        return;
    }

    // Debug 34: Shadow depth comparison
    // Shows receiver depth vs shadow map depth at each pixel.
    // Green = lit (receiver depth <= shadow depth), Red = shadowed
    // Brightness = depth margin (how much lit/shadowed)
    if (uDebugViewMode == 34) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }

        vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
        vec3 lightDir = normalize(uShadowLightDirection);
        float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 cameraRelPos = worldPos - uCameraPos;
        float viewDistance = length(cameraRelPos);
        float offsetWorld = selectedShadowNormalOffsetWorld(cameraRelPos, ndotl, viewDistance);
        vec3 shadowUv = localShadowUvFromWorld(worldPos + normal * offsetWorld);

        if (shadowUvOutOfBounds(shadowUv)) {
            FragColor = vec4(0.95, 0.08, 0.02, 1.0); // red = out of bounds
            return;
        }

        float shadowDepth = texture(uShadowMapRaw, shadowUv.xy).r;
        float bias = selectedShadowCompareBias(ndotl, viewDistance, shadowDither());
        float margin = shadowUv.z - bias - shadowDepth;
        // Green = lit (margin <= 0), Red = shadowed (margin > 0)
        float lit = margin <= 0.0 ? 1.0 : 0.0;
        float marginAbs = abs(margin) * uShadowDistance * 2.0;
        FragColor = vec4(
            mix(0.08, 0.95, 1.0 - lit),  // R: shadowed
            mix(0.08, 0.95, lit),          // G: lit
            heatmap(clamp(marginAbs / 2.0, 0.0, 1.0)).b,  // B: margin depth
            1.0
        );
        return;
    }

    // Debug 35: Shadow hit caster info.
    // For shadowed pixels, show the shadowcolor0 texel that caused the compare.
    // Blue tint = transparent caster marker, cyan = cleared/default texel.
    if (uDebugViewMode == 35) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }

        vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
        vec3 lightDir = normalize(uShadowLightDirection);
        float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 cameraRelPos = worldPos - uCameraPos;
        float viewDistance = length(cameraRelPos);
        float offsetWorld = selectedShadowNormalOffsetWorld(cameraRelPos, ndotl, viewDistance);
        vec3 shadowUv = localShadowUvFromWorld(worldPos + normal * offsetWorld);

        if (shadowUvOutOfBounds(shadowUv)) {
            FragColor = vec4(0.95, 0.08, 0.02, 1.0);
            return;
        }

        float bias = selectedShadowCompareBias(ndotl, viewDistance, shadowDither());
        float shadowDepth = texture(uShadowMapRaw, shadowUv.xy).r;
        float shadowed = (shadowUv.z - bias > shadowDepth) ? 1.0 : 0.0;
        vec4 casterColor = texture(uShadowColorTex, shadowUv.xy);
        vec4 casterNormal = texture(uShadowNormalTex, shadowUv.xy);
        float cleared = step(0.995, casterColor.r) * step(0.995, casterColor.g) *
                        step(0.995, casterColor.b) * step(0.995, casterColor.a);
        vec3 normalPreview = vec3(casterNormal.rg, casterNormal.b);
        vec3 colorPreview = mix(casterColor.rgb, vec3(0.1, 0.45, 1.0), (1.0 - casterColor.a) * 0.7);
        colorPreview = mix(colorPreview, vec3(0.0, 0.9, 0.9), cleared * 0.85);
        colorPreview = mix(colorPreview, normalPreview, 0.18);
        FragColor = vec4(mix(vec3(0.03), colorPreview, shadowed), 1.0);
        return;
    }

    // Debug 36-39: raw CSM depth array layers.
    if (uDebugViewMode >= 36 && uDebugViewMode <= 39) {
        int layer = clamp(uDebugViewMode - 36, 0, 3);
        float shadowDepth = texture(uCsmShadowDepthTex, vec3(vTexCoord, float(layer))).r;
        if (shadowDepth >= 0.9999) {
            FragColor = vec4(0.015, 0.025, 0.040, 1.0);
            return;
        }
        FragColor = vec4(heatmap(1.0 - shadowDepth), 1.0);
        return;
    }

    // Debug 41: Sky direction — raw sky radiance sampled by world direction.
    // For sky pixels (depth >= 0.9999), uses view ray direction. For solid geometry,
    // reconstructs world position and shows the sky radiance in that direction.
    if (uDebugViewMode == 41) {
        float depth = texture(uDepthTex, vTexCoord).r;
        vec3 worldDir;
        if (depth >= 0.9999) {
            // Sky pixel: reconstruct direction from screen UV
            vec4 farPoint = uInvViewProj * vec4(vTexCoord * 2.0 - 1.0, 1.0, 1.0);
            worldDir = normalize(farPoint.xyz / max(farPoint.w, 0.0001) - uCameraPos);
        } else {
            vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
            worldDir = normalize(worldPos - uCameraPos);
        }
        vec3 sky = sampleSkyRadiance(uSkyCaptureTex, worldDir);
        FragColor = vec4(tonemapPreview(sky), 1.0);
        return;
    }

    // Debug 42: Sky direction — cloudy sky radiance sampled by world direction.
    if (uDebugViewMode == 42) {
        float depth = texture(uDepthTex, vTexCoord).r;
        vec3 worldDir;
        if (depth >= 0.9999) {
            vec4 farPoint = uInvViewProj * vec4(vTexCoord * 2.0 - 1.0, 1.0, 1.0);
            worldDir = normalize(farPoint.xyz / max(farPoint.w, 0.0001) - uCameraPos);
        } else {
            vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
            worldDir = normalize(worldPos - uCameraPos);
        }
        vec3 sky = sampleSkyRadianceCloudy(uSkyCaptureTex, worldDir);
        FragColor = vec4(tonemapPreview(sky), 1.0);
        return;
    }

    // Debug 43: Sky direction — raw sky with 20x exposure to reveal dim regions.
    if (uDebugViewMode == 43) {
        float depth = texture(uDepthTex, vTexCoord).r;
        vec3 worldDir;
        if (depth >= 0.9999) {
            vec4 farPoint = uInvViewProj * vec4(vTexCoord * 2.0 - 1.0, 1.0, 1.0);
            worldDir = normalize(farPoint.xyz / max(farPoint.w, 0.0001) - uCameraPos);
        } else {
            vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
            worldDir = normalize(worldPos - uCameraPos);
        }
        vec3 sky = sampleSkyRadiance(uSkyCaptureTex, worldDir) * 20.0;
        FragColor = vec4(tonemapPreview(sky), 1.0);
        return;
    }

    // Debug 40: cascade info — shows cascade index (color) and texel world size (brightness).
    if (uDebugViewMode == 40) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.02, 0.03, 0.05, 1.0);
            return;
        }
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        float viewDistance = length(worldPos - uCameraPos);
        int cascadeIdx = selectCsmCascade(viewDistance);
        vec3 cascadeColor = csmCascadeColor(cascadeIdx);

        // Texel world size: brighter = larger texels = lower resolution
        float texelWorld = uCsmCascades[cascadeIdx].texelWorldSize;
        float texelBrightness = clamp(texelWorld / 4.0, 0.15, 1.0);

        // Split position within cascade: shows how far into the cascade we are
        float splitNear = uCsmCascades[cascadeIdx].splitNear;
        float splitFar = uCsmCascades[cascadeIdx].splitFar;
        float cascadeT = clamp((viewDistance - splitNear) / max(splitFar - splitNear, 1.0), 0.0, 1.0);

        // Near split boundary = brighter (highlighting transition zones)
        float edgeHighlight = 1.0 + 0.4 * (1.0 - cascadeT);

        FragColor = vec4(cascadeColor * texelBrightness * edgeHighlight, 1.0);
        return;
    }

    // Debug 44: SkyCapture atlas overview + GPU metadata.
    // Left half: raw sky equirectangular. Right half: cloudy sky equirectangular.
    // Bottom-right panel: metadata texels from GPU sky capture.
    //   Row 0: directIlluminance (sun+moon)
    //   Row 1: skyIlluminance
    //   Row 2: sunIlluminance
    //   Row 3: moonIlluminance
    //   Row 5: cloudDynamicWeather
    if (uDebugViewMode == 44) {
        vec2 uv = vTexCoord;
        // Metadata region: visible 6-row panel in the bottom-right corner.
        vec2 metaPanelSize = vec2(0.26, 0.24);
        if (uv.x > 1.0 - metaPanelSize.x && uv.y < metaPanelSize.y) {
            vec2 metaUv = vec2(
                (uv.x - (1.0 - metaPanelSize.x)) / metaPanelSize.x,
                uv.y / metaPanelSize.y
            );
            int row = int(metaUv.y * 6.0);
            row = clamp(row, 0, 5);
            vec3 val = texelFetch(uSkyCaptureTex, ivec2(skyCaptureRes.x, row), 0).rgb;

            // Label: different colors per metadata row
            vec3 labelColor = vec3(0.0);
            if (row == 0) labelColor = vec3(1.0, 0.9, 0.3);   // directIlluminance = yellow
            else if (row == 1) labelColor = vec3(0.3, 0.7, 1.0); // skyIlluminance = sky blue
            else if (row == 2) labelColor = vec3(1.0, 0.5, 0.1); // sunIlluminance = orange
            else if (row == 3) labelColor = vec3(0.6, 0.6, 0.9); // moonIlluminance = pale blue
            else if (row == 5) labelColor = vec3(0.3, 1.0, 0.5); // cloudDynamicWeather = green

            // Show value with label stripe on left edge and row separators.
            float labelStripe = step(metaUv.x, 0.15);
            float rowSeparator = step(fract(metaUv.y * 6.0), 0.04);
            vec3 display = mix(tonemapPreview(max(val, vec3(0.0))), labelColor, labelStripe * 0.7);
            display = mix(display, vec3(0.0), rowSeparator * 0.55);
            FragColor = vec4(display, 1.0);
            return;
        }

        // Split horizontally: left = raw sky, right = cloudy sky.
        // Use the same texel-center atlas contract as projectSky/projectSkyCloudy,
        // so the debug view itself does not introduce row-boundary bleeding.
        if (uv.x < 0.5) {
            // Left: raw sky (rows 0..257)
            vec2 rawUv = vec2(uv.x * 2.0, mix(rawSkyVMin, rawSkyVMax, uv.y));
            vec3 sky = texture(uSkyCaptureTex, clamp(rawUv, 0.0, 1.0)).rgb;
            FragColor = vec4(tonemapPreview(sky), 1.0);
        } else {
            // Right: cloudy sky (rows 258..513)
            vec2 cloudyUv = vec2((uv.x - 0.5) * 2.0, mix(cloudySkyVMin, cloudySkyVMax, uv.y));
            vec3 sky = texture(uSkyCaptureTex, clamp(cloudyUv, 0.0, 1.0)).rgb;
            FragColor = vec4(tonemapPreview(sky), 1.0);
        }
        return;
    }

    // Debug 45: Lighting balance diagnostic.
    // Shows CPU-side lighting colors as patches + SkyCapture metadata intensity bars.
    // Top-right: CPU sunLightColor, skyAmbientColor, horizonScatterColor, fogColor.
    // Bottom-right: SkyCapture illuminance bars (direct, sky, sun, moon).
    if (uDebugViewMode == 45) {
        vec2 uv = vTexCoord;

        // CPU lighting color patches (top-right, each 0.04 high, 0.12 wide)
        float patchX = 0.88;
        float patchW = 0.12;
        float patchH = 0.04;
        if (uv.x > patchX) {
            vec3 cpuColor = vec3(0.0);
            float labelStripe = 0.0;
            vec3 labelColor = vec3(0.0);
            if (uv.y > 0.96) {
                cpuColor = max(uSunLightColor, vec3(0.0));
                labelColor = vec3(1.0, 0.9, 0.3);
                labelStripe = step(uv.x, patchX + 0.015);
            } else if (uv.y > 0.92) {
                cpuColor = max(uSkyAmbientColor, vec3(0.0));
                labelColor = vec3(0.3, 0.7, 1.0);
                labelStripe = step(uv.x, patchX + 0.015);
            } else if (uv.y > 0.88) {
                cpuColor = max(uHorizonScatterColor, vec3(0.0));
                labelColor = vec3(0.8, 0.6, 0.9);
                labelStripe = step(uv.x, patchX + 0.015);
            } else if (uv.y > 0.84) {
                cpuColor = max(uFogColor, vec3(0.0));
                labelColor = vec3(0.6, 0.6, 0.6);
                labelStripe = step(uv.x, patchX + 0.015);
            } else {
                discard;
            }
            vec3 display = tonemapPreview(cpuColor);
            display = mix(display, labelColor, labelStripe * 0.7);
            FragColor = vec4(display, 1.0);
            return;
        }

        // SkyCapture illuminance intensity bars (bottom-right)
        float barX = 0.78;
        float barW = 0.22;
        float barH = 0.025;
        if (uv.x > barX && uv.y < barH * 4.0) {
            int barIndex = int(uv.y / barH);
            float barLocalY = mod(uv.y, barH) / barH;

            vec3 luxValue = vec3(0.0);
            vec3 barColor = vec3(0.0);
            if (barIndex == 0) {
                luxValue = max(getDirectIlluminance(uSkyCaptureTex), vec3(0.0));
                barColor = vec3(1.0, 0.85, 0.3);
            } else if (barIndex == 1) {
                luxValue = max(getSkyIlluminance(uSkyCaptureTex), vec3(0.0));
                barColor = vec3(0.3, 0.6, 1.0);
            } else if (barIndex == 2) {
                luxValue = max(getSunIlluminance(uSkyCaptureTex), vec3(0.0));
                barColor = vec3(1.0, 0.5, 0.1);
            } else {
                luxValue = max(getMoonIlluminance(uSkyCaptureTex), vec3(0.0));
                barColor = vec3(0.5, 0.5, 0.8);
            }

            float intensity = dot(luxValue, vec3(0.2126, 0.7152, 0.0722));
            float barFill = clamp(intensity / max(intensity + 1.0, 0.001), 0.0, 1.0);
            float fillEdge = step(uv.x - barX, barW * barFill);
            float border = step(barLocalY, 0.1) + step(0.9, barLocalY);
            float borderColor = border * 0.3;

            vec3 fillColor = barColor * fillEdge * 0.7;
            vec3 bg = vec3(0.05);
            FragColor = vec4(mix(bg, fillColor + borderColor, max(fillEdge, border)), 1.0);
            return;
        }

        // Default: scene lighting preview
        FragColor = vec4(tonemapPreview(texture(uSceneLightingTex, uv).rgb), 1.0);
        return;
    }

    // Debug 67: TAA current scratch (TemporalCurrent buffer).
    // Shows the pre-TAA scene that the temporal resolve reads as "current".
    if (uDebugViewMode == 67) {
        FragColor = vec4(tonemapPreview(texture(uTemporalCurrentTex, vTexCoord).rgb), 1.0);
        return;
    }

    // Debug 68: TAA current-vs-history delta.
    // Shows abs(current - history) amplified 4x. Bright = large divergence.
    // Helps identify where variance clip is rejecting history or where TAA fails to converge.
    if (uDebugViewMode == 68) {
        vec3 current = texture(uTemporalCurrentTex, vTexCoord).rgb;
        vec3 history = texture(uHistorySceneTex, vTexCoord).rgb;
        vec3 delta = abs(current - history) * 4.0;
        FragColor = vec4(clamp(delta, 0.0, 1.0), 1.0);
        return;
    }

    // Debug 69: Velocity field with sky/far-plane highlight.
    // Non-sky pixels: velocity direction as color, speed as brightness.
    // Sky pixels (depth >= 0.9999): red highlight to identify sky velocity issues.
    if (uDebugViewMode == 69) {
        float depth = texture(uDepthTex, vTexCoord).r;
        vec2 velocity = texture(uVelocityTex, vTexCoord).rg;
        float speed = length(velocity);
        if (depth >= 0.9999) {
            // Sky: show velocity magnitude with red tint
            FragColor = vec4(heatmap(speed * 100.0).r, heatmap(speed * 100.0).g * 0.3, 0.05, 1.0);
        } else {
            FragColor = vec4(heatmap(speed * 50.0), 1.0);
        }
        return;
    }

    // Debug 70: Raw half-res VFog texel preview.
    // Nearest half-res fetch, magnified to full screen without the depth-aware upscale.
    if (uDebugViewMode == 70) {
        ivec2 halfSize = textureSize(uVolumetricTex, 0);
        ivec2 halfTexel = clamp(ivec2(gl_FragCoord.xy * 0.5), ivec2(0), halfSize - ivec2(1));
        vec4 vfog = texelFetch(uVolumetricTex, halfTexel, 0);
        FragColor = vec4(tonemapPreview(max(vfog.rgb, vec3(0.0)) * 4.0), 1.0);
        return;
    }

    // Debug 71: Depth-aware upscaled VFog, matching volumetric_composite.fs.
    if (uDebugViewMode == 71) {
        vec4 vfog = debugSpatialUpscaleVolumetric(vTexCoord);
        FragColor = vec4(tonemapPreview(max(vfog.rgb, vec3(0.0)) * 4.0), 1.0);
        return;
    }

    // Debug 78: reflection contribution after scene composite.
    // Shows how much SceneComposite differs from SceneLighting before water/VFog/TAA.
    if (uDebugViewMode == 78) {
        vec3 lighting = texture(uSceneLightingTex, vTexCoord).rgb;
        vec3 composite = texture(uSceneCompositeTex, vTexCoord).rgb;
        FragColor = vec4(tonemapPreview(abs(composite - lighting) * 32.0), 1.0);
        return;
    }

    // Debug 79: scene TAA loss.
    // Shows the difference between unresolved current scene and resolved scene.
    if (uDebugViewMode == 79) {
        vec3 current = texture(uTemporalCurrentTex, vTexCoord).rgb;
        vec3 resolved = texture(uSceneResolvedTex, vTexCoord).rgb;
        FragColor = vec4(tonemapPreview(abs(current - resolved) * 32.0), 1.0);
        return;
    }

    // Debug 80: wet surface mask used by scene TAA rain-ripple history rejection.
    if (uDebugViewMode == 80) {
        float depth = texture(uDepthTex, vTexCoord).r;
        if (depth >= 0.9999) {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
        TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
        float wetHistoryReject = transMask.isTranslucent
            ? 0.0
            : smoothstep(0.02, 0.25, aux.wetnessMask);
        FragColor = vec4(vec3(wetHistoryReject), 1.0);
        return;
    }

    if (uDebugViewMode == 81) {
        FragColor = vec4(tonemapPreview(texture(uSsgiTex, vTexCoord).rgb), 1.0);
        return;
    }

    if (uDebugViewMode == 82) {
        FragColor = vec4(tonemapPreview(texture(uSsgiTex, vTexCoord).rgb * 8.0), 1.0);
        return;
    }

    if (uDebugViewMode == 83) {
        FragColor = vec4(vec3(texture(uSsgiTex, vTexCoord).a), 1.0);
        return;
    }

    // Debug 46-77: Volumetric fog / UW VL / shadow contract debug.
    // The volumetric fog pass outputs debug colors when uVolumetricDebugMode is active.
    if (uDebugViewMode >= 46 && uDebugViewMode <= 77) {
        vec4 vfog = texture(uVolumetricTex, vTexCoord);
        if (uDebugViewMode == 48 || uDebugViewMode == 49) {
            // Sky/Sun scattering: use tonemap to reveal small HDR values
            FragColor = vec4(tonemapPreview(max(vfog.rgb, vec3(0.0))), 1.0);
        } else if (uDebugViewMode == 51) {
            // Integration diagnostic: amplified values, direct display
            FragColor = vec4(clamp(vfog.rgb, 0.0, 1.0), 1.0);
        } else {
            // Density heatmap, transmittance, sun gates, sky ray coverage: direct display
            FragColor = vec4(vfog.rgb, 1.0);
        }
        return;
    }

    FragColor = vec4(tonemapPreview(texture(uSceneLightingTex, vTexCoord).rgb), 1.0);
}
