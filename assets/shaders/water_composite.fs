#version 450 core
#include "gbuffer_contract.glsl"

// Varyings from chunk_lit.vs
in vec2 vUV;
in float vLight;
in float vSunlight;
in float vBlockLight;
in float vAO;
in float vNormal;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
in float vFogDist;
in vec3 vWorldPos;
flat in float vTintKind;
flat in float vMaterialKind;
in vec2 vTintUV;

uniform sampler2DArray texArray;
uniform sampler2D uOpaqueDepthTex;
uniform sampler2D uSceneColorTex;
uniform sampler2D uNoiseTex;
uniform sampler2D uReflectionTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uRippleNormalTex;
uniform sampler2D uVolumetricTex;
uniform sampler3D uAtmosphereLut;
uniform mat4 viewProj;
uniform mat4 uView;
uniform mat4 uInvViewProj;
uniform float uNearPlane;
uniform float uFarPlane;
uniform int uSkyCaptureEnabled;
uniform int uCompositeInputsEnabled;
uniform int uWaterCompositeEnabled;
uniform int uDepthSofteningEnabled;
uniform int uVolumetricFogActive;
uniform int uFrameIndex;
uniform int uFreezeBias;
uniform float uAnimationTime;
uniform float uTime;
uniform vec3 uWaterAbsorption;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uSkyAmbientColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uWeatherWetness;
uniform float uSkyWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uSurfaceWetness;
uniform float uWaterWaveHeight;
uniform float uWaterWaveSpeed;
uniform float uWaterIOR;
uniform int uIsEyeInWater;
uniform float uWaterStillFirstLayer;
uniform float uWaterStillLayerCount;
uniform float uWaterFlowFirstLayer;
uniform float uWaterFlowLayerCount;

out vec4 FragColor;

#include "lighting_environment.glsl"
#include "atmosphere_lut.glsl"

const float rPI = 1.0 / 3.14159265359;
const float kTwoPi = 6.28318530718;
const int kWaterSsrSteps = 16;
const bool kWaterRealSkyReflection = false;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float cube(float x) {
    return x * x * x;
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float linearDepthFromDepth(float depth) {
    if (depth >= 0.9999) {
        return 1e6;
    }
    return (uNearPlane * uFarPlane) / (depth * (uNearPlane - uFarPlane) + uFarPlane);
}

float viewDistanceFromDepthTexel(ivec2 texel) {
    ivec2 size = textureSize(uOpaqueDepthTex, 0);
    ivec2 clampedTexel = clamp(texel, ivec2(0), size - ivec2(1));
    return linearDepthFromDepth(texelFetch(uOpaqueDepthTex, clampedTexel, 0).r);
}

vec4 sampleDepthAwareVolumetric(vec2 uv) {
    float centerLinearDepth = linearDepthFromDepth(texture(uOpaqueDepthTex, uv).r);
    ivec2 halfSize = textureSize(uVolumetricTex, 0);
    // DerivativeMain: bias rotates with frameCounter for temporal variation.
    ivec2 bias = (uFreezeBias != 0)
        ? ivec2(floor(gl_FragCoord.xy)) & ivec2(1)
        : ivec2(gl_FragCoord.xy + float(uFrameIndex)) & ivec2(1);
    ivec2 baseTexel = ivec2(floor(gl_FragCoord.xy * 0.5)) + bias * 2;
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
        float sampleLinearDepth = viewDistanceFromDepthTexel(sampleTexel * 2);
        float weight = max(exp2(-abs(sampleLinearDepth - centerLinearDepth) * sigmaZ), 1e-6);
        sum += texelFetch(uVolumetricTex, sampleTexel, 0) * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 0.0001);
}

vec2 projectWorldUv(vec3 worldPos, out float projectedDepth) {
    vec4 clip = viewProj * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 0.00001);
    projectedDepth = ndc.z * 0.5 + 0.5;
    return ndc.xy * 0.5 + 0.5;
}

vec3 decodeFaceNormal(float face) {
    if (face > -2.5 && face < -0.5) return vec3(0.0, 1.0, 0.0);
    int idx = int(round(face));
    if (idx == 0) return vec3(0.0, 1.0, 0.0);
    if (idx == 1) return vec3(0.0, -1.0, 0.0);
    if (idx == 2) return vec3(0.0, 0.0, 1.0);
    if (idx == 3) return vec3(0.0, 0.0, -1.0);
    if (idx == 4) return vec3(-1.0, 0.0, 0.0);
    return vec3(1.0, 0.0, 0.0);
}

// ---- TBN construction (DerivativeMain Water.vert line 52-54) ----
// Constructs TBN matrix from face normal for axis-aligned block faces
// tbnMatrix[2] = normal, tbnMatrix[0] = tangent, tbnMatrix[1] = bitangent
mat3 buildTBN(vec3 faceNormal) {
    vec3 N = faceNormal;
    vec3 T, B;
    // Choose tangent based on face orientation (DerivativeMain uses at_tangent)
    // For axis-aligned faces, tangent is the horizontal direction
    if (abs(N.y) > 0.5) {
        // Top/bottom face: tangent = +X, bitangent = +Z
        T = vec3(1.0, 0.0, 0.0);
        B = vec3(0.0, 0.0, 1.0);
    } else if (abs(N.x) > 0.5) {
        // +X/-X face: tangent = +Z, bitangent = +Y
        T = vec3(0.0, 0.0, 1.0);
        B = vec3(0.0, 1.0, 0.0);
    } else {
        // +Z/-Z face: tangent = +X, bitangent = +Y
        T = vec3(1.0, 0.0, 0.0);
        B = vec3(0.0, 1.0, 0.0);
    }
    return mat3(T, B, N);
}

// ---- DerivativeMain WaterWave.glsl port ----

float curve(float x) { return x * x * (3.0 - 2.0 * x); }
vec2 curve2(vec2 x) { return x * x * (3.0 - 2.0 * x); }

// DerivativeMain WaterWave.glsl textureSmooth (exact port)
float textureSmooth(vec2 coord) {
    coord += 0.5;
    vec2 whole = floor(coord);
    vec2 part = curve2(coord - whole);
    coord = whole + part - 0.5;
    return texture(uNoiseTex, coord / 256.0).r;
}

// DerivativeMain WaterWave.glsl WaterHeight (exact port)
float WaterHeight(vec2 p) {
    float wavesTime = uTime * 1.2 * uWaterWaveSpeed;
    p.y *= 0.8; // DerivativeMain: squash Y axis

    float wave = 0.0;
    wave += textureSmooth((p + vec2(0.0, p.x - wavesTime)) * 0.8);
    wave += textureSmooth((p - vec2(-wavesTime, p.x)) * 1.6) * 0.5;
    wave += textureSmooth((p + vec2(wavesTime * 0.6, p.x - wavesTime)) * 2.4) * 0.2;
    wave += textureSmooth((p - vec2(wavesTime * 0.6, p.x - wavesTime)) * 3.6) * 0.1;

    // Distance attenuation (DerivativeMain: 80.0 / far)
    return wave / (0.8 + dot(abs(dFdx(p) + dFdy(p)), vec2(80.0 / 512.0)));
}

// DerivativeMain GetWavesNormal: central differences
// Returns tangent-space normal: XY = slope, Z = up
vec3 GetWavesNormal(vec2 position) {
    float wavesCenter = WaterHeight(position);
    float wavesLeft   = WaterHeight(position + vec2(0.04, 0.0));
    float wavesUp     = WaterHeight(position + vec2(0.0, 0.04));
    vec2 wavesNormal = vec2(wavesCenter - wavesLeft, wavesCenter - wavesUp);
    return normalize(vec3(wavesNormal * uWaterWaveHeight, 0.5));
}

vec3 GetReflectionWavesNormal(vec2 position) {
    vec3 primary = GetWavesNormal(position);
    vec3 secondary = GetWavesNormal(position * 1.75 + vec2(19.3, -7.1));
    vec2 slope = primary.xy * 2.25 + secondary.xy * 0.85;
    return normalize(vec3(slope, primary.z * 0.55));
}

// ---- DerivativeMain Water Parallax (lib/Surface/Parallax.glsl) ----
// Stepped parallax mapping for water surface
vec2 GetWaterParallaxCoord(vec3 worldPos, vec3 tangentViewDir) {
    vec3 stepSize = tangentViewDir * vec3(vec2(0.1 * uWaterWaveHeight), 1.0);
    stepSize *= 0.02 / max(abs(stepSize.z), 0.001);

    vec3 samplePos = vec3(worldPos.xz - worldPos.y, 1.0) + stepSize;
    float sampleHeight = WaterHeight(samplePos.xy);

    for (uint i = 0u; sampleHeight < samplePos.z && i < 60u; ++i) {
        samplePos += stepSize;
        sampleHeight = WaterHeight(samplePos.xy);
    }
    return samplePos.xy;
}

// DerivativeMain water reflection semantics: use screen-space color only on a real
// depth hit, otherwise fall back to the captured skybox.
bool traceWaterScreenSpaceReflection(vec3 worldPos,
                                     vec3 reflectedDir,
                                     vec3 normal,
                                     out vec3 hitColor) {
    hitColor = vec3(0.0);

    const float maxDistance = 48.0;
    const float stepLength = maxDistance / float(kWaterSsrSteps);
    vec3 rayOrigin = worldPos + normal * 0.025 + reflectedDir * 0.15;

    for (int i = 1; i <= kWaterSsrSteps; ++i) {
        vec3 sampleWorld = rayOrigin + reflectedDir * (float(i) * stepLength);
        float rayDepth;
        vec2 uv = projectWorldUv(sampleWorld, rayDepth);
        if (uv.x <= 0.001 || uv.x >= 0.999 || uv.y <= 0.001 || uv.y >= 0.999 ||
            rayDepth <= 0.0 || rayDepth >= 1.0) {
            return false;
        }

        float sceneDepth = texture(uOpaqueDepthTex, uv).r;
        if (sceneDepth >= 0.9999) {
            continue;
        }

        float thickness = mix(0.00025, 0.0045, clamp(float(i) / float(kWaterSsrSteps), 0.0, 1.0));
        if (rayDepth >= sceneDepth && rayDepth - sceneDepth < thickness) {
            hitColor = texture(uSceneColorTex, uv).rgb;
            return true;
        }
    }

    return false;
}

// ---- Rain ripple normal (DerivativeMain lib/Surface/RainEffect.glsl) ----
vec2 GetRainNormal(vec3 worldPos, float wetness) {
    float wet = clamp(wetness, 0.0, 1.0);
    if (wet <= 0.0) {
        return vec2(0.0);
    }

    vec2 atlasUv = worldPos.xz * 0.6;
    atlasUv.x = (atlasUv.x + floor(fract(uTime) * 64.0)) * (1.0 / 64.0);
    vec2 normal = texture(uRippleNormalTex, atlasUv).rg * 2.0 - 1.0;
    float lod = dot(abs(fwidth(worldPos)), vec3(5.0));
    normal /= 1.0 + lod;
    return normal * 0.75 * wet;
}

vec2 calculateDerivativeWaterRefractUv(vec2 screenUv,
                                        float waterDepthRaw,
                                        float opaqueDepthRaw,
                                        vec3 worldWaveNormal,
                                        out float refractedOpaqueDepthRaw) {
    refractedOpaqueDepthRaw = opaqueDepthRaw;
    if (opaqueDepthRaw >= 0.9999 || opaqueDepthRaw < waterDepthRaw) {
        return screenUv;
    }

    float waterDepth = linearDepthFromDepth(waterDepthRaw);
    float refractionDepth = linearDepthFromDepth(opaqueDepthRaw) - waterDepth;
    if (refractionDepth <= 0.0) {
        return screenUv;
    }

    vec3 wavesNormalView = normalize(mat3(uView) * worldWaveNormal);
    vec3 viewUp = normalize(uView[1].xyz);
    vec2 refractOffset = viewUp.xy - wavesNormalView.xy;
    refractOffset *= clamp(refractionDepth, 0.0, 1.0) * 0.5 / (waterDepth + 1e-4);

    vec2 refractUv = clamp(screenUv + refractOffset, vec2(0.0), vec2(1.0));
    refractedOpaqueDepthRaw = texture(uOpaqueDepthTex, refractUv).r;
    if (refractedOpaqueDepthRaw < waterDepthRaw) {
        refractedOpaqueDepthRaw = opaqueDepthRaw;
        return screenUv;
    }

    return refractUv;
}

// DerivativeMain FresnelDielectricN
float FresnelDielectricN(float cosTheta, float n) {
    float cosR = n * n + cosTheta * cosTheta - 1.0;
    if (cosR < 0.0) return 1.0;
    cosR = sqrt(cosR);
    float a = n * cosTheta;
    float b = n * cosR;
    float r1 = (a - cosR) / (a + cosR);
    float r2 = (b - cosTheta) / (b + cosTheta);
    return clamp(0.5 * (r1 * r1 + r2 * r2), 0.0, 1.0);
}

// DerivativeMain WaterFog (lib/Water/WaterFog.glsl)
// env: LightingEnvironment from SkyCapture — provides sky color + illuminance.
void WaterFog(inout vec3 color, float waterSkylight, float LdotV, float waterDepth,
              LightingEnvironment env) {
    // fogDensity = WATER_FOG_DENSITY * fma(0.1, wetness*skylight, 0.16) * waterDepth
    float fogDensity = 1.0 * (0.16 + 0.1 * uSkyWetness * waterSkylight) * waterDepth;

    // DerivativeMain WaterFog.glsl:6-8
    // Base fog color from skyIlluminance (hemisphere-integrated sky irradiance).
    vec3 fogColor = mix(env.skyIlluminance * 0.4,
                        vec3(luminance(env.skyIlluminance) * 0.1),
                        0.8 * uSkyWetness * waterSkylight) * rPI;

    // Sun scatter: 28.0 * directIlluminance * phase (DerivativeMain WaterFog.glsl:8)
    float scatter = atmHenyeyGreensteinPhase(LdotV, 0.65) + 0.1 * rPI;
    fogColor *= 1.0 + 28.0 * (1.0 - uSkyWetness * 0.8) * env.directIlluminance * scatter;

    // Beer-Lambert: fastExp(-(waterAbsorption * 8.0 + 0.03) * fogDensity)
    vec3 absorption = uWaterAbsorption * 8.0 + 0.03;
    vec3 transmittance = exp(-absorption * fogDensity);

    color *= transmittance;
    color += fogColor * waterSkylight * (1.0 - transmittance);
}

// DerivativeMain UnderwaterFog (lib/Water/WaterFog.glsl line 19-32, exact port)
void UnderwaterFog(inout vec3 color, float waterDepth, LightingEnvironment env) {
    float skylight = cube(clamp(vSunlight, 0.0, 1.0));
    float fogDensity = 1.0 * (0.1 + 0.05 * uSkyWetness * skylight) * waterDepth;

    vec3 skyFogBase = mix(env.skyHorizonAvg, env.skyZenith, 0.3);
    vec3 fogColor = mix(skyFogBase * 0.4,
                        vec3(luminance(skyFogBase) * 0.1),
                        0.8 * uSkyWetness * skylight) * rPI;

    vec3 absorption = uWaterAbsorption * 8.0 + 0.03;
    vec3 transmittance = exp(-absorption * max(fogDensity, 2.0) + 0.4);

    color *= transmittance;
    color += fogColor * clamp(skylight + 0.2, 0.0, 1.0) * (1.0 - transmittance);
}

// DerivativeMain water fallback reflection samples the sky capture (colortex5),
// rather than re-querying the atmosphere LUT from a fragment/camera altitude.
vec3 sampleSkyReflection(vec3 dir, vec3 normal, float skylight, LightingEnvironment env) {
    float skyWeight = smoothstep(0.15, 0.85, skylight);
    float nDotUp = clamp((dot(normal, vec3(0.0, 1.0, 0.0)) + 0.7) * 2.0, 0.0, 1.0) * 0.75 + 0.25;
    vec3 fallbackSky = mix(env.skyHorizonAvg, env.skyZenith, 0.3);
    vec3 skybox = (uSkyCaptureEnabled != 0)
        ? sampleEnvironmentCloudySky(uSkyCaptureTex, dir)
        : fallbackSky;
    vec3 blendedSky = mix(fallbackSky, skybox, skyWeight);
    return max(blendedSky * nDotUp, vec3(0.0));
}

vec3 renderSunReflection(vec3 rayDir, LightingEnvironment env) {
    vec3 sunDir = normalize(uSunDirection);
    float cosTheta = clamp(dot(normalize(rayDir), sunDir), -1.0, 1.0);
    const float sunReflectionRadius = 0.05;
    if (cosTheta < cos(sunReflectionRadius)) {
        return vec3(0.0);
    }

    float centerToEdge = clamp(acos(cosTheta) / sunReflectionRadius, 0.0, 1.0);
    const vec3 alpha = vec3(0.429, 0.522, 0.614);
    vec3 limbDark = pow(vec3(1.0 - centerToEdge * centerToEdge), alpha * 0.5);
    // DerivativeMain Atmosphere.glsl:784 uses hardcoded solar disk luminance:
    //   sunIlluminance = solar_irradiance * 126.6e3 / coneAngleToSolidAngle(0.05)
    // which is ~vec3(186600,234200,242000), then clamped to 2000.
    // We use env.sunIlluminance (LUT transmittance * solar constant) scaled to
    // approximate the visible disk luminance. The clamp caps overexposure.
    vec3 solarDiskLuminance = env.sunIlluminance * 80000.0;
    return min(solarDiskLuminance * limbDark, vec3(2000.0)) * clamp(uSkyIntensity, 0.0, 1.0);
}

bool layerInRange(float layer, float firstLayer, float layerCount) {
    return layerCount > 0.5 && layer >= firstLayer - 0.5 && layer < firstLayer + layerCount - 0.5;
}

void main() {
    bool isWater = layerInRange(vLayer, uWaterStillFirstLayer, uWaterStillLayerCount) ||
                   layerInRange(vLayer, uWaterFlowFirstLayer, uWaterFlowLayerCount);
    if (!isWater) discard;

    // --- Lighting environment from SkyCapture ---
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);

    vec2 screenUv = gl_FragCoord.xy / vec2(textureSize(uSceneColorTex, 0));

    // ---- DerivativeMain water depth ----
    // DerivativeMain measures water fog through the water volume:
    // distance(ScreenToViewSpace(water depth), ScreenToViewSpace(opaque depth)).
    float depthGap = 0.0;
    float sceneDepth = texelFetch(uOpaqueDepthTex, ivec2(gl_FragCoord.xy), 0).r;
    if (uDepthSofteningEnabled != 0 && sceneDepth > gl_FragCoord.z) {
        vec3 waterSurfacePos = reconstructWorldPosition(screenUv, gl_FragCoord.z);
        vec3 opaquePos = reconstructWorldPosition(screenUv, sceneDepth);
        depthGap = clamp(distance(waterSurfacePos, opaquePos), 0.0, 512.0);
    }

    vec3 faceNormal = decodeFaceNormal(vNormal);

    // ---- DerivativeMain Water.frag line 224-249 ----
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    mat3 tbnMatrix = buildTBN(faceNormal);

    // Compute wave normal with parallax (DerivativeMain line 225-229)
    vec3 waveNormalTangent;
    vec3 tangentViewDir = normalize(viewDir * tbnMatrix); // viewDir in tangent space
    // Parallax mapping: find UV offset from height field intersection
    vec2 parallaxPos = GetWaterParallaxCoord(vWorldPos, tangentViewDir);
    waveNormalTangent = GetWavesNormal(parallaxPos);
    vec3 baseWaveNormalTangent = waveNormalTangent;

    // Add rain ripple normals (DerivativeMain RainEffect.glsl)
    if (uSurfaceWetness > 0.01) {
        float skylightFactor = clamp(vSunlight * 10.0 - 9.0, 0.0, 1.0);
        vec2 rainNormal = GetRainNormal(vWorldPos, uSurfaceWetness * skylightFactor);
        waveNormalTangent.xy += rainNormal;
        waveNormalTangent = normalize(waveNormalTangent);
    }

    // Transform to world space using TBN (DerivativeMain line 249)
    vec3 reflectionWaveNormalTangent = GetReflectionWavesNormal(parallaxPos);
    if (uSurfaceWetness > 0.01) {
        float skylightFactor = clamp(vSunlight * 10.0 - 9.0, 0.0, 1.0);
        vec2 rainNormal = GetRainNormal(vWorldPos, uSurfaceWetness * skylightFactor);
        reflectionWaveNormalTangent.xy += rainNormal * 1.35;
        reflectionWaveNormalTangent = normalize(reflectionWaveNormalTangent);
    }

    vec3 normal = normalize(tbnMatrix * waveNormalTangent);
    vec3 reflectionNormal = normalize(tbnMatrix * reflectionWaveNormalTangent);
    vec3 refractionNormal = normalize(tbnMatrix * baseWaveNormalTangent);

    // ---- Underwater normal flip (must precede Fresnel so wave ripples are visible) ----
    if (uIsEyeInWater == 1) {
        normal = -normal;
        reflectionNormal = -reflectionNormal;
    }

    // ---- Fresnel (DerivativeMain BRDF.glsl FresnelDielectricN) ----
    float NdotV = max(1e-6, dot(normal, viewDir));
    float fresnel = FresnelDielectricN(NdotV, uWaterIOR);

    // ---- Refraction (DerivativeMain Refraction.glsl line 87-108) ----
    float refractDepthTex = sceneDepth;
    vec2 refractUv = calculateDerivativeWaterRefractUv(screenUv, gl_FragCoord.z, sceneDepth,
                                                       refractionNormal, refractDepthTex);
    vec3 sceneColor = texture(uSceneColorTex, refractUv).rgb;

    // ---- Water fog (DerivativeMain WaterFog.glsl, applied BEFORE reflection) ----
    float waterSkylight = cube(clamp(vSunlight, 0.0, 1.0));
    float LdotV = dot(normalize(uSunDirection), -viewDir);
    float fogDist = depthGap;
    if (uDepthSofteningEnabled != 0 && refractDepthTex > gl_FragCoord.z && refractDepthTex < 0.9999) {
        vec3 waterSurfacePos = reconstructWorldPosition(screenUv, gl_FragCoord.z);
        vec3 opaquePos = reconstructWorldPosition(refractUv, refractDepthTex);
        fogDist = clamp(distance(waterSurfacePos, opaquePos), 0.0, 512.0);
    }
    if (uIsEyeInWater == 1) {
        UnderwaterFog(sceneColor, length(uCameraPos - vWorldPos), env);
    } else {
        WaterFog(sceneColor, waterSkylight, LdotV, fogDist, env);
    }

    // ---- Reflection (DerivativeMain CalculateSpecularReflections) ----
    vec3 reflDir = reflect(-viewDir, reflectionNormal);
    vec3 skyRefl = sampleSkyReflection(reflDir, reflectionNormal, waterSkylight, env);
    vec3 ssrRefl = vec3(0.0);
    bool ssrHit = traceWaterScreenSpaceReflection(vWorldPos, reflDir, reflectionNormal, ssrRefl);
    vec3 reflection = ssrHit ? ssrRefl : skyRefl;

    // Underwater reflection: use sky reflection driven by flipped wave normal
    // (DerivativeMain line 103 — keeps wave variation visible instead of flat constant)
    if (uIsEyeInWater == 1) {
        float uwSkyWeight = clamp(uSkyIntensity, 0.0, 1.0) * 0.3;
        reflection = max(skyRefl, vec3(0.02, 0.14, 0.2)) * uwSkyWeight;
    }

    // DerivativeMain/program/Gbuffers/Water.frag keeps REAL_SKY_REFLECTION
    // disabled by default. In that path, water falls back to the captured sky
    // and does not add a separate high-energy sun disk to reflectionData.
    // Mecraft adaptation: keep the dormant path available for future opt-in
    // parity testing, but avoid feeding a 2e3 sun disk into threshold-free bloom.
    if (kWaterRealSkyReflection) {
        reflection += renderSunReflection(reflDir, env) * waterSkylight;
    }

    // Moon reflection: Mecraft intentional divergence from DerivativeMain.
    // DerivativeMain RenderMoonReflection() returns vec3(disc * 4.0) — a hardcoded luminance
    // independent of moonIlluminance/NIGHT_BRIGHTNESS. Mecraft scales by env.moonIlluminance
    // so the reflection brightness tracks the actual moon phase and night brightness setting.
    float cosThetaMoon = dot(reflDir, normalize(-uMoonDirection));
    float moonSize = 5e-3;
    float moonHardness = 2e2;
    float moonDisc = pow(clamp((cosThetaMoon - 1.0 + moonSize) * moonHardness, 0.0, 1.0), 2.0);
    reflection += vec3(moonDisc) * env.moonIlluminance;

    // ---- Combine (DerivativeMain composite1.fsh line 117, premultiplied alpha) ----
    // reflectionData = vec4(reflection * specular, 1.0 - specular)
    // sceneData = sceneData * reflectionData.a + reflectionData.rgb
    // = sceneColor * (1-fresnel) + reflection * fresnel
    vec3 color = sceneColor * (1.0 - fresnel) + reflection * fresnel;

    // Mecraft adaptation: post-TAA fallback path consumes the already computed
    // volumetric result. In the DerivativeMain-parity path, water runs before
    // VFog and uVolumetricFogActive is disabled so fog is applied once later.
    float fogTransmittance = 1.0;
    if (uVolumetricFogActive != 0) {
        vec4 volumetric = sampleDepthAwareVolumetric(screenUv);
        fogTransmittance = clamp(volumetric.a, 0.0, 1.0);
        color = color * fogTransmittance + volumetric.rgb;
    }

    FragColor = vec4(max(color, vec3(0.0)), fogTransmittance);
}
