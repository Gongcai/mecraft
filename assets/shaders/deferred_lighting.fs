#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uVoxelLightTex;
uniform sampler2D uDepthTex;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uShadowMap;
uniform sampler2D uSsaoTex;

uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform mat4 uShadowViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uSunLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uShadowTintColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform int uAerialPerspectiveEnabled;
uniform float uShadowTintStrength;
uniform float uDirectSunStrength;
uniform float uSkyAmbientStrength;
uniform float uMinimumAmbient;
uniform float uShadowMinLight;
uniform float uShadowContrast;
uniform float uBlockLightStrength;
uniform float uFakeBounceStrength;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform int uShadowsEnabled;
uniform int uSoftShadowsEnabled;
uniform int uContactShadowsEnabled;
uniform float uShadowSoftness;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
uniform float uShadowNormalOffset;
uniform float uContactShadowStrength;
uniform int uSsaoEnabled;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

const float kTwoPi = 6.28318530718;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

float computeFogFactor(float fogDistance) {
    if (uFogMode == 1) {
        return clamp(exp(-uFogDensity * fogDistance), 0.0, 1.0);
    }
    if (uFogMode == 2) {
        float d = uFogDensity * fogDistance;
        return clamp(exp(-(d * d)), 0.0, 1.0);
    }
    float linearRange = max(uFogEnd - uFogStart, 0.0001);
    return clamp((uFogEnd - fogDistance) / linearRange, 0.0, 1.0);
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

vec2 projectWorldToUv(vec3 worldPos, out float ndcDepth) {
    vec4 clip = uViewProj * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 0.00001);
    ndcDepth = ndc.z * 0.5 + 0.5;
    return ndc.xy * 0.5 + 0.5;
}

float compareShadowTexelAt(vec3 proj, ivec2 texelCoord, float bias) {
    ivec2 size = textureSize(uShadowMap, 0);
    if (texelCoord.x < 0 || texelCoord.y < 0 || texelCoord.x >= size.x || texelCoord.y >= size.y) {
        return 1.0;
    }
    float closest = texelFetch(uShadowMap, texelCoord, 0).r;
    return (proj.z - bias <= closest) ? 1.0 : 0.0;
}

float compareShadowTexel(vec3 proj, ivec2 offset, float bias) {
    ivec2 size = textureSize(uShadowMap, 0);
    ivec2 texelCoord = ivec2(floor(proj.xy * vec2(size))) + offset;
    return compareShadowTexelAt(proj, texelCoord, bias);
}

float compareShadowBilinear(vec3 proj, vec2 offsetTexels, float bias) {
    ivec2 size = textureSize(uShadowMap, 0);
    vec2 texelPos = proj.xy * vec2(size) - vec2(0.5) + offsetTexels;
    ivec2 base = ivec2(floor(texelPos));
    vec2 f = fract(texelPos);

    float s00 = compareShadowTexelAt(proj, base, bias);
    float s10 = compareShadowTexelAt(proj, base + ivec2(1, 0), bias);
    float s01 = compareShadowTexelAt(proj, base + ivec2(0, 1), bias);
    float s11 = compareShadowTexelAt(proj, base + ivec2(1, 1), bias);

    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

vec2 r2(float n) {
    return fract(vec2(n * 0.7548776662, n * 0.5698402909));
}

vec2 r2Disk(float n) {
    vec2 u = r2(n);
    float angle = u.x * kTwoPi;
    return vec2(cos(angle), sin(angle)) * sqrt(u.y);
}

float calculateShadowWarp(vec2 coord) {
    return length(coord * 1.169) * 0.85 + 0.15;
}

float shapeShadowVisibility(float lit) {
    lit = clamp(lit, 0.0, 1.0);
    float contrasted = pow(lit, max(uShadowContrast, 0.001));
    return mix(clamp(uShadowMinLight, 0.0, 0.8), 1.0, contrasted);
}

float shadowFactor(vec3 worldPos, vec3 normal) {
    if (uShadowsEnabled == 0) {
        return 1.0;
    }
    vec3 sunDir = normalize(uSunDirection);
    float viewDistanceForBias = length(worldPos - uCameraPos);
    float normalOffset = uShadowNormalOffset * (1.0 + clamp(viewDistanceForBias / 220.0, 0.0, 1.5)) *
                         (1.0 + 0.65 * (1.0 - max(dot(normal, sunDir), 0.0)));
    vec4 lightClip = uShadowViewProj * vec4(worldPos + normal * normalOffset, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.00001);
    float warpDensity = calculateShadowWarp(proj.xy);
    proj.xy /= warpDensity;
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.y < 0.0 || proj.x > 1.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    float ndotl = clamp(dot(normal, sunDir), 0.0, 1.0);
    float bias = uShadowConstantBias + uShadowSlopeBias * (1.0 - ndotl);

    if (uSoftShadowsEnabled == 0) {
        return shapeShadowVisibility(compareShadowTexel(proj, ivec2(0), bias));
    }

    float viewDistance = viewDistanceForBias;
    float distanceSoftness = smoothstep(18.0, 96.0, viewDistance);
    float grazingSoftness = 1.0 - ndotl;
    vec2 centeredShadow = proj.xy * 2.0 - 1.0;
    float filterWarpDensity = calculateShadowWarp(centeredShadow);
    float radius = clamp(max(uShadowSoftness, 0.1) * (1.20 + 0.42 * distanceSoftness + 0.22 * grazingSoftness) * filterWarpDensity,
                         2.0, 7.5);
    float lit = 0.0;
    for (int i = 0; i < 24; ++i) {
        lit += compareShadowBilinear(proj, r2Disk(float(i) + 0.37) * radius, bias);
    }
    return shapeShadowVisibility(lit / 24.0);
}

float contactShadow(vec3 worldPos, vec3 normal, vec2 voxelLight, float shadowVisibility) {
    if (uShadowsEnabled == 0 || uContactShadowsEnabled == 0 || uContactShadowStrength <= 0.001) {
        return 1.0;
    }
    vec3 sunDir = normalize(uSunDirection);
    float sunTerm = max(dot(normal, sunDir), 0.0);
    float skyTerm = clamp(voxelLight.r * uSkyIntensity, 0.0, 1.0);
    float litGate = smoothstep(0.55, 0.92, shadowVisibility);
    if (sunTerm * skyTerm * litGate <= 0.02) {
        return 1.0;
    }

    float occlusion = 0.0;
    const float maxDistance = 1.25;
    for (int i = 1; i <= 4; ++i) {
        float t = float(i) / 4.0;
        vec3 sampleWorld = worldPos + sunDir * (t * maxDistance);
        float sampleProjectedDepth = 1.0;
        vec2 sampleUv = projectWorldToUv(sampleWorld, sampleProjectedDepth);
        if (sampleUv.x <= 0.0 || sampleUv.y <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y >= 1.0) {
            continue;
        }
        float sceneDepth = texture(uDepthTex, sampleUv).r;
        float depthDelta = sampleProjectedDepth - sceneDepth;
        float hit = smoothstep(0.00030, 0.00120, depthDelta);
        occlusion += hit * (1.0 - t * 0.45);
    }
    occlusion = clamp(occlusion / 4.0, 0.0, 1.0);
    return 1.0 - occlusion * uContactShadowStrength * sunTerm * skyTerm * litGate;
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    if (depth >= 1.0) {
        discard;
    }

    vec4 albedoMaterial = texture(uAlbedoTex, vTexCoord);
    vec3 albedo = albedoMaterial.rgb;
    float emissiveHint = albedoMaterial.a;
    vec4 normalAo = texture(uNormalAoTex, vTexCoord);
    vec3 normal = normalize(normalAo.rgb * 2.0 - 1.0);
    float vertexAo = mix(0.72, 1.0, normalAo.a);
    vec2 voxelLight = texture(uVoxelLightTex, vTexCoord).rg;
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);

    vec2 lightmapUV = vec2(voxelLight.g, 1.0 - voxelLight.r);
    vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
    vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
    vec3 vanillaLight = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));
    float skyLightMask = clamp(voxelLight.r * uSkyIntensity, 0.0, 1.0);
    float blockLightMask = clamp(voxelLight.g, 0.0, 1.0);

    vec3 sunDir = normalize(uSunDirection);
    float ndotl = max(dot(normal, sunDir), 0.0);
    float diffuse = pow(ndotl, 0.82);
    float shadow = shadowFactor(worldPos, normal);
    shadow *= contactShadow(worldPos, normal, voxelLight, shadow);
    float ssao = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;

    vec3 directSun = uSunLightColor * diffuse * shadow * skyLightMask * uDirectSunStrength;
    vec3 skyAmbient = uSkyAmbientColor * (0.10 + 0.90 * skyLightMask) * uSkyAmbientStrength;
    skyAmbient *= mix(vec3(1.0), uShadowTintColor, (1.0 - shadow) * clamp(uShadowTintStrength, 0.0, 1.0));

    float minimumAmbientMask = mix(0.35, 1.0, skyLightMask);
    vec3 minimumAmbient = uShadowTintColor * uMinimumAmbient * minimumAmbientMask;

    float groundFacing = clamp(dot(normal, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 fakeBounce = uSunLightColor * uFakeBounceStrength * pow(skyLightMask, 4.0) * (0.35 + 0.65 * groundFacing);

    vec3 blockLightColor = mix(vec3(1.0, 0.70, 0.42), vanillaLight, 0.22);
    vec3 blockLight = blockLightColor * pow(blockLightMask, 2.2) * uBlockLightStrength;

    vec3 totalLight = directSun + skyAmbient + minimumAmbient + fakeBounce + blockLight;
    totalLight = mix(totalLight, vanillaLight, 0.07);

    vec3 color = albedo * totalLight * vertexAo * mix(1.0, ssao, 0.65);
    color += albedo * emissiveHint * emissiveHint * (0.35 + 0.45 * uBlockLightStrength);

    if (uFogEnabled != 0) {
        float fogDistance = length(worldPos - uCameraPos);
        float fogFactor = computeFogFactor(fogDistance);
        vec3 fogColor = srgbToLinear(uFogColor);
        if (uAerialPerspectiveEnabled != 0) {
            vec3 viewDir = normalize(worldPos - uCameraPos);
            float sunForward = pow(max(dot(viewDir, normalize(uSunDirection)), 0.0), 2.0);
            float horizon = pow(1.0 - clamp(abs(viewDir.y), 0.0, 1.0), 1.65);
            float heightFade = 1.0 - smoothstep(96.0, 192.0, worldPos.y);
            vec3 scatter = mix(fogColor, uHorizonScatterColor, horizon * clamp(uHorizonScatterStrength, 0.0, 2.0));
            scatter += uSunLightColor * sunForward * 0.26 * clamp(uHorizonScatterStrength, 0.0, 2.0);
            fogColor = mix(fogColor, scatter, clamp(uAerialStrength, 0.0, 2.0) * heightFade);
        }
        color = mix(fogColor, color, fogFactor);
    }

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
