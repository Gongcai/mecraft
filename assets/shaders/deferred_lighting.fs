#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uVoxelLightTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uDepthTex;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uShadowMap;
uniform sampler2D uSsaoTex;
uniform sampler2D uSkyCaptureTex;

uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform mat4 uShadowViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uShadowTintColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform int uAerialPerspectiveEnabled;
uniform float uShadowTintStrength;
uniform float uDirectSunStrength;
uniform float uSkyAmbientStrength;
uniform float uMinimumAmbient;
uniform float uShadowMinLight;
uniform float uShadowContrast;
uniform float uBlockLightStrength;
uniform float uFakeBounceStrength;
uniform float uAlbedoDesaturation;
uniform float uSunWarmth;
uniform float uSkyCoolness;
uniform float uShadowDesaturation;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform float uWeatherMist;
uniform float uWeatherWetness;
uniform float uWeatherStorm;
uniform float uAerialReduction;
uniform int uShadowsEnabled;
uniform int uSoftShadowsEnabled;
uniform int uPcssShadowsEnabled;
uniform int uContactShadowsEnabled;
uniform int uShadowWarpMode;
uniform int uShadowLightMode;
uniform float uShadowDistance;
uniform float uShadowSoftness;
uniform float uShadowPcssStrength;
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
const float kPi = 3.14159265359;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 desaturateLinear(vec3 color, float amount) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(color, vec3(luma), clamp(amount, 0.0, 1.0));
}

float ggxDistribution(float ndoth, float roughness) {
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 0.00001);
}

float smithG1(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.00001);
}

vec3 fresnelSchlick(float vdoth, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
}

vec3 blackbodyApprox(float temperatureKelvin) {
    float t = clamp(temperatureKelvin, 1000.0, 12000.0) / 1000.0;
    vec3 color;
    if (t <= 6.6) {
        color.r = 1.0;
        color.g = clamp(0.39008158 * log(t) - 0.63184144, 0.0, 1.0);
    } else {
        color.r = clamp(1.29293619 * pow(t - 6.0, -0.13320476), 0.0, 1.0);
        color.g = clamp(1.12989086 * pow(t - 6.0, -0.07551485), 0.0, 1.0);
    }
    color.b = t >= 6.6 ? 1.0 : (t <= 1.9 ? 0.0 : clamp(0.54320679 * log(t - 1.0) - 1.19625409, 0.0, 1.0));
    return color;
}

vec2 directionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / kTwoPi + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 sampleSkyCapture(vec3 dir) {
    return texture(uSkyCaptureTex, directionToSkyCaptureUv(dir)).rgb;
}

vec3 sampleSkyIrradiance(vec3 normal) {
    normal = normalize(normal);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 north = normalize(vec3(0.0, 0.45, -1.0));
    vec3 south = normalize(vec3(0.0, 0.45, 1.0));
    vec3 east = normalize(vec3(1.0, 0.45, 0.0));
    vec3 west = normalize(vec3(-1.0, 0.45, 0.0));

    float wUp = 0.34 + 0.34 * max(dot(normal, up), 0.0);
    float wNorth = 0.16 + 0.20 * max(dot(normal, north), 0.0);
    float wSouth = 0.16 + 0.20 * max(dot(normal, south), 0.0);
    float wEast = 0.16 + 0.20 * max(dot(normal, east), 0.0);
    float wWest = 0.16 + 0.20 * max(dot(normal, west), 0.0);
    float weightSum = wUp + wNorth + wSouth + wEast + wWest;

    vec3 irradiance = sampleSkyCapture(up) * wUp;
    irradiance += sampleSkyCapture(north) * wNorth;
    irradiance += sampleSkyCapture(south) * wSouth;
    irradiance += sampleSkyCapture(east) * wEast;
    irradiance += sampleSkyCapture(west) * wWest;
    return irradiance / max(weightSum, 0.0001);
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

float sampleShadowDepthAt(vec3 proj, vec2 offsetTexels) {
    ivec2 size = textureSize(uShadowMap, 0);
    vec2 uv = proj.xy + offsetTexels / vec2(size);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        return 1.0;
    }
    return texture(uShadowMap, uv).r;
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
    if (uShadowWarpMode == 2) {
        return 1.0;
    }
    if (uShadowWarpMode == 1) {
        vec2 scaled = coord * 1.165;
        float quarticLength = pow(dot(scaled * scaled, scaled * scaled), 0.25);
        return quarticLength * 0.85 + 0.15;
    }
    return length(coord * 1.169) * 0.85 + 0.15;
}

float shapeShadowVisibility(float lit) {
    lit = clamp(lit, 0.0, 1.0);
    float contrasted = pow(lit, max(uShadowContrast, 0.001));
    return mix(clamp(uShadowMinLight, 0.0, 0.8), 1.0, contrasted);
}

float shadowProjectionFade(vec3 proj) {
    vec2 edgeDistance = min(proj.xy, vec2(1.0) - proj.xy);
    float edgeFade = smoothstep(0.010, 0.055, min(edgeDistance.x, edgeDistance.y));
    float farFade = 1.0 - smoothstep(0.965, 0.998, proj.z);
    return clamp(edgeFade * farFade, 0.0, 1.0);
}

float pcssFilterRadius(vec3 proj, float baseRadius, float bias) {
    if (uPcssShadowsEnabled == 0 || uShadowPcssStrength <= 0.001) {
        return baseRadius;
    }

    float blockerDepthSum = 0.0;
    float blockerCount = 0.0;
    float searchRadius = clamp(baseRadius * 0.78, 1.0, 5.5);
    for (int i = 0; i < 8; ++i) {
        float blockerDepth = sampleShadowDepthAt(proj, r2Disk(float(i) + 6.13) * searchRadius);
        float isBlocker = step(blockerDepth, proj.z - bias);
        blockerDepthSum += blockerDepth * isBlocker;
        blockerCount += isBlocker;
    }

    if (blockerCount < 0.5) {
        return 1.15;
    }

    float averageBlockerDepth = blockerDepthSum / blockerCount;
    float receiverToBlocker = max(proj.z - averageBlockerDepth, 0.0);
    float penumbra = clamp(receiverToBlocker * 260.0 * uShadowPcssStrength, 0.0, 1.0);
    return mix(1.15, baseRadius, penumbra);
}

float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    if (uShadowsEnabled == 0) {
        return 1.0;
    }
    lightDir = normalize(lightDir);
    float viewDistanceForBias = length(worldPos - uCameraPos);
    float distanceFade = 1.0 - smoothstep(uShadowDistance * 0.58, uShadowDistance * 0.82, viewDistanceForBias);
    if (distanceFade <= 0.001) {
        return 1.0;
    }
    float normalOffset = uShadowNormalOffset * (1.0 + clamp(viewDistanceForBias / 220.0, 0.0, 1.5)) *
                         (1.0 + 0.65 * (1.0 - max(dot(normal, lightDir), 0.0)));
    vec4 lightClip = uShadowViewProj * vec4(worldPos + normal * normalOffset, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.00001);
    float warpDensity = calculateShadowWarp(proj.xy);
    proj.xy /= warpDensity;
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.y < 0.0 || proj.x > 1.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = uShadowConstantBias + uShadowSlopeBias * (1.0 - ndotl);

    float projectionFade = shadowProjectionFade(proj);
    if (projectionFade <= 0.001) {
        return 1.0;
    }

    if (uSoftShadowsEnabled == 0) {
        float hardShadow = shapeShadowVisibility(compareShadowTexel(proj, ivec2(0), bias));
        return mix(1.0, hardShadow, projectionFade * distanceFade);
    }

    float viewDistance = viewDistanceForBias;
    float distanceSoftness = smoothstep(18.0, 96.0, viewDistance);
    float grazingSoftness = 1.0 - ndotl;
    vec2 centeredShadow = proj.xy * 2.0 - 1.0;
    float filterWarpDensity = calculateShadowWarp(centeredShadow);
    float radius = clamp(max(uShadowSoftness, 0.1) * (1.20 + 0.42 * distanceSoftness + 0.22 * grazingSoftness) * filterWarpDensity,
                         2.0, 7.5);
    radius = pcssFilterRadius(proj, radius, bias);
    float lit = 0.0;
    for (int i = 0; i < 24; ++i) {
        lit += compareShadowBilinear(proj, r2Disk(float(i) + 0.37) * radius, bias);
    }
    return mix(1.0, shapeShadowVisibility(lit / 24.0), projectionFade * distanceFade);
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

vec3 aerialFogColor(vec3 baseFogColor, vec3 viewDir, float horizon, vec3 warmSunColor) {
    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float dayFactor = clamp(uSkyIntensity, 0.0, 1.0);
    float nightFactor = 1.0 - dayFactor;
    float sunVisibility = smoothstep(-0.08, 0.18, sunDir.y) * dayFactor;
    float moonVisibility = clamp(uMoonVisibility, 0.0, 1.0);

    vec3 captureDir = normalize(vec3(viewDir.x, viewDir.y * 0.32, viewDir.z));
    vec3 capturedFog = sampleSkyCapture(captureDir);
    vec3 skyFog = mix(capturedFog, uHorizonScatterColor, horizon * clamp(uHorizonScatterStrength, 0.0, 2.0));
    vec3 fogColor = mix(baseFogColor, skyFog, 0.34 + 0.18 * nightFactor);

    float sunForwardWide = pow(max(dot(viewDir, sunDir), 0.0), 5.0);
    float sunForwardCore = pow(max(dot(viewDir, sunDir), 0.0), 36.0);
    float moonForward = pow(max(dot(viewDir, moonDir), 0.0), 8.0);
    fogColor += warmSunColor * (sunForwardWide * 0.14 + sunForwardCore * 0.22) *
                sunVisibility * clamp(uHorizonScatterStrength, 0.0, 2.0);
    fogColor += uMoonLightColor * moonForward * moonVisibility * nightFactor *
                (0.10 + 0.10 * clamp(uHorizonScatterStrength, 0.0, 2.0));
    return max(fogColor, vec3(0.0));
}

vec3 applyAerialPerspective(vec3 sceneColor,
                            vec3 worldPos,
                            float fogDistance,
                            float outdoorSkyMask,
                            vec3 warmSunColor) {
    vec3 viewDir = normalize(worldPos - uCameraPos);
    float horizon = pow(1.0 - clamp(abs(viewDir.y), 0.0, 1.0), 1.55);
    float distanceTransmittance = computeFogFactor(fogDistance);
    float distanceFogOpacity = 1.0 - distanceTransmittance;

    vec3 baseFogColor = srgbToLinear(uFogColor);
    if (uAerialPerspectiveEnabled == 0) {
        return mix(baseFogColor, sceneColor, distanceTransmittance);
    }

    float outdoorMask = smoothstep(0.05, 0.65, outdoorSkyMask);
    float heightDensity = (1.0 - smoothstep(96.0, 220.0, worldPos.y)) * (0.68 + 0.42 * horizon);
    float weatherHaze = 0.55 * uWeatherMist + 0.35 * uWeatherWetness + 0.65 * uWeatherStorm;
    float clearAirScale = mix(clamp(uAerialReduction, 0.0, 1.0), 0.82, clamp(weatherHaze, 0.0, 1.0));
    float airDensity = (0.00048 + 0.00105 * horizon) *
                       clamp(uAerialStrength, 0.0, 2.0) *
                       clearAirScale *
                       (1.0 + weatherHaze * 0.85);
    float aerialOpacity = (1.0 - exp(-fogDistance * airDensity)) * outdoorMask * heightDensity;
    float fogOpacity = clamp(max(distanceFogOpacity * mix(0.55, 0.95, clamp(weatherHaze, 0.0, 1.0)),
                                 aerialOpacity * 0.48),
                             0.0,
                             0.88);

    vec3 fogColor = aerialFogColor(baseFogColor, viewDir, horizon, warmSunColor);
    return mix(sceneColor, fogColor, fogOpacity);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    if (depth >= 1.0) {
        discard;
    }

    vec4 albedoMaterial = texture(uAlbedoTex, vTexCoord);
    vec3 albedo = albedoMaterial.rgb;
    albedo = desaturateLinear(albedo, uAlbedoDesaturation);
    float emissiveHint = albedoMaterial.a;
    vec4 normalAo = texture(uNormalAoTex, vTexCoord);
    vec3 normal = normalize(normalAo.rgb * 2.0 - 1.0);
    float vertexAo = mix(0.72, 1.0, normalAo.a);
    vec2 voxelLight = texture(uVoxelLightTex, vTexCoord).rg;
    vec4 material = texture(uMaterialTex, vTexCoord);
    float roughness = clamp(material.r, 0.03, 1.0);
    float f0Scalar = clamp(material.g, 0.02, 0.35);
    float materialEmission = clamp(material.b, 0.0, 1.0);
    float sss = clamp(material.a, 0.0, 1.0);
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);

    vec2 lightmapUV = vec2(voxelLight.g, 1.0 - voxelLight.r);
    vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
    vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
    vec3 vanillaLight = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));
    float skyLightMask = clamp(voxelLight.r * uSkyIntensity, 0.0, 1.0);
    float nightSkyMask = clamp(voxelLight.r * uMoonVisibility, 0.0, 1.0);
    float outdoorSkyMask = max(skyLightMask, nightSkyMask);
    float blockLightMask = clamp(voxelLight.g, 0.0, 1.0);

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    vec3 viewDir = normalize(uCameraPos - worldPos);
    vec3 halfDir = normalize(sunDir + viewDir);
    float ndotl = max(dot(normal, sunDir), 0.0);
    float ndotm = max(dot(normal, moonDir), 0.0);
    float ndotv = max(dot(normal, viewDir), 0.04);
    float ndoth = max(dot(normal, halfDir), 0.0);
    float vdoth = max(dot(viewDir, halfDir), 0.0);
    float diffuse = pow(ndotl, 0.82);
    float ssao = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;
    vec3 shadowLightDir = (uShadowLightMode == 1) ? moonDir : sunDir;
    float shadow = shadowFactor(worldPos, normal, shadowLightDir);
    shadow *= contactShadow(worldPos, normal, voxelLight, shadow);
    float sunShadow = (uShadowLightMode == 0) ? shadow : 1.0;
    float moonShadow = (uShadowLightMode == 1) ? mix(1.0, shadow, 0.82) : 1.0;

    vec3 warmSunColor = mix(uSunLightColor, uSunLightColor * vec3(1.22, 1.04, 0.78), clamp(uSunWarmth, 0.0, 1.5));
    vec3 coolSkyColor = mix(uSkyAmbientColor, uSkyAmbientColor * vec3(0.78, 0.92, 1.18), clamp(uSkyCoolness, 0.0, 1.0));
    vec3 capturedZenith = sampleSkyCapture(vec3(0.0, 1.0, 0.0));
    vec3 capturedNormalSky = sampleSkyIrradiance(normal);
    float skyCaptureInfluence = mix(0.18, 0.46, 1.0 - clamp(uSkyIntensity, 0.0, 1.0));
    coolSkyColor = mix(coolSkyColor, mix(capturedZenith, capturedNormalSky, 0.55), skyCaptureInfluence);
    vec3 directSun = warmSunColor * diffuse * sunShadow * skyLightMask * uDirectSunStrength;
    float moonMask = nightSkyMask;
    vec3 moonFill = uMoonLightColor * moonMask * (0.030 + 0.060 * uSkyAmbientStrength);
    vec3 directMoon = uMoonLightColor * pow(ndotm, 0.68) * moonShadow * moonMask * (0.46 + 0.22 * uSkyAmbientStrength);
    vec3 f0 = vec3(f0Scalar);
    vec3 specF = fresnelSchlick(vdoth, f0);
    float specD = ggxDistribution(ndoth, roughness);
    float specG = smithG1(ndotl, roughness) * smithG1(ndotv, roughness);
    vec3 directSpecular = warmSunColor * specF * (specD * specG / max(4.0 * ndotl * ndotv, 0.0001));
    directSpecular *= ndotl * sunShadow * skyLightMask * uDirectSunStrength;
    directSpecular *= mix(1.0, 0.28, roughness);
    vec3 skyAmbient = coolSkyColor * (0.10 + 0.90 * outdoorSkyMask) * uSkyAmbientStrength + moonFill;
    skyAmbient *= mix(vec3(1.0), uShadowTintColor, (1.0 - shadow) * clamp(uShadowTintStrength, 0.0, 1.0));
    vec3 skySpecular = coolSkyColor * specF * pow(1.0 - roughness, 2.2) * (0.025 + 0.075 * outdoorSkyMask);

    float minimumAmbientMask = mix(0.35, 1.0, outdoorSkyMask);
    vec3 minimumAmbient = uShadowTintColor * uMinimumAmbient * minimumAmbientMask;

    float groundFacing = clamp(dot(normal, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 fakeBounce = warmSunColor * uFakeBounceStrength * pow(skyLightMask, 4.0) * (0.35 + 0.65 * groundFacing);

    float blockLightCurve = pow(blockLightMask, 2.05);
    vec3 warmBlockLight = vec3(1.0, 0.84, 0.58);
    vec3 blockLightColor = mix(warmBlockLight, vanillaLight, 0.18);
    vec3 blockLight = blockLightColor * blockLightCurve * uBlockLightStrength;

    vec3 totalLight = directSun + directMoon + skyAmbient + minimumAmbient + fakeBounce + blockLight;
    totalLight = mix(totalLight, vanillaLight, 0.07);

    vec3 color = albedo * totalLight * vertexAo * mix(1.0, ssao, 0.65);
    float backScatter = pow(max(dot(-normal, sunDir), 0.0), 1.35) * skyLightMask * sunShadow;
    color += albedo * warmSunColor * backScatter * sss * 0.34;
    float moonBackScatter = pow(max(dot(-normal, moonDir), 0.0), 1.45) * moonMask;
    color += albedo * uMoonLightColor * moonBackScatter * sss * 0.10;
    float specularSurfaceMask = smoothstep(0.025, 0.14, f0Scalar) * (1.0 - roughness * 0.45);
    color += (directSpecular + skySpecular) * vertexAo * mix(1.0, ssao, 0.35) * (0.72 + 0.58 * specularSurfaceMask);
    float shadowMask = (1.0 - shadow) * outdoorSkyMask;
    color = desaturateLinear(color, shadowMask * uShadowDesaturation);
    float emissionStrength = max(emissiveHint * emissiveHint, materialEmission);
    float emissionLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    vec3 emissionTint = vec3(1.0, 0.88, 0.64);
    vec3 emissionColor = mix(albedo, emissionTint * max(emissionLuma, 0.45), 0.42);
    color += emissionColor * emissionStrength * (0.55 + 0.82 * uBlockLightStrength);

    if (uFogEnabled != 0) {
        float fogDistance = length(worldPos - uCameraPos);
        color = applyAerialPerspective(color, worldPos, fogDistance, outdoorSkyMask, warmSunColor);
    }

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
