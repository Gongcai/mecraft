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
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uSceneColorTex;
uniform sampler2D uNoiseTex;
uniform sampler2D uReflectionTex;
uniform sampler3D uAtmosphereLut;
uniform int uSkyCaptureEnabled;
uniform int uCompositeInputsEnabled;
uniform int uWaterCompositeEnabled;
uniform int uDepthSofteningEnabled;
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
uniform float uWaterWaveHeight;
uniform float uWaterWaveSpeed;
uniform float uWaterIOR;
uniform int uIsEyeInWater;
uniform float uWaterStillFirstLayer;
uniform float uWaterStillLayerCount;
uniform float uWaterFlowFirstLayer;
uniform float uWaterFlowLayerCount;

out vec4 FragColor;

#include "atmosphere_lut.glsl"

const float rPI = 1.0 / 3.14159265359;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
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

// ---- DerivativeMain SSR (lib/Surface/ScreenSpaceReflections.glsl) ----
// Simplified screen-space ray trace
bool ScreenSpaceRayTrace(vec3 viewPos, vec3 rayDir, float dither, int steps, inout vec3 hitPos) {
    vec3 rayStep = rayDir * (20.0 / float(steps));
    vec3 samplePos = viewPos + rayStep * dither;

    for (int i = 0; i < steps; ++i) {
        samplePos += rayStep;
        // Project to screen
        vec4 clipPos = vec4(samplePos, 1.0); // viewPos is already in a projection-compatible space
        // For our setup, we need to project to screen UV
        // Simplified: use depth comparison
        if (samplePos.z > 0.0) return false; // Behind camera
    }
    return false;
}

// ---- Rain ripple normal (DerivativeMain lib/Surface/RainEffect.glsl) ----
vec2 GetRainNormal(vec3 worldPos, float wetness) {
    vec2 p = worldPos.xz * 0.6;
    float frame = fract(uTime * 0.05) * 64.0;
    p.x = (p.x + floor(frame)) / 64.0;
    vec2 normal = texture(uNoiseTex, p).rg * 2.0 - 1.0;
    float lod = length(fwidth(worldPos.xz));
    normal /= 1.0 + lod;
    return normal * 0.75 * wetness;
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

// DerivativeMain WaterFog (lib/Water/WaterFog.glsl line 2-17, exact port)
// DerivativeMain waterAbsorption = vec3(0.45, 0.02, 0.01) — pure water coefficients
// Our uWaterAbsorption = vec3(1.0, 0.45, 0.28) — different scale
// We need to use the SAME formula: absorption * 8.0 + 0.03
void WaterFog(inout vec3 color, float waterSkylight, float LdotV, float waterDepth) {
    // fogDensity = WATER_FOG_DENSITY * fma(0.1, wetness*skylight, 0.16) * waterDepth
    float fogDensity = 1.0 * (0.16 + 0.1 * uWeatherWetness * waterSkylight) * waterDepth;

    // Fog color: skyIlluminance * 0.4 * rPI
    vec3 fogColor = uSkyAmbientColor * 0.4 * rPI;

    // Scatter: 28.0 * oneMinus(wetness*0.8) * directIlluminance * scatter
    float scatter = atmHenyeyGreensteinPhase(LdotV, 0.65) + 0.1 * rPI;
    fogColor *= 1.0 + 28.0 * (1.0 - uWeatherWetness * 0.8) * uSunLightColor * scatter;

    // Beer-Lambert: fastExp(-(waterAbsorption * 8.0 + 0.03) * fogDensity)
    // Use corrected absorption: smaller values for transparent water
    vec3 absorption = vec3(0.45, 0.02, 0.01) * 8.0 + 0.03; // = vec3(3.63, 0.19, 0.11)
    vec3 transmittance = exp(-absorption * fogDensity);

    color *= transmittance;
    color += fogColor * waterSkylight * (1.0 - transmittance);
}

// DerivativeMain UnderwaterFog (lib/Water/WaterFog.glsl line 19-32, exact port)
void UnderwaterFog(inout vec3 color, float waterDepth) {
    float skylight = clamp(vLight, 0.0, 1.0);
    float fogDensity = 1.0 * (0.1 + 0.05 * uWeatherWetness * skylight) * waterDepth;

    vec3 fogColor = uSkyAmbientColor * 0.4 * rPI;

    vec3 absorption = vec3(0.45, 0.02, 0.01) * 8.0 + 0.03;
    vec3 transmittance = exp(-absorption * max(fogDensity, 2.0) + 0.4);

    color *= transmittance;
    color += fogColor * clamp(skylight + 0.2, 0.0, 1.0) * (1.0 - transmittance);
}

vec3 sampleSkyReflection(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / 6.28318530718 + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return texture(uSkyCaptureTex, vec2(fract(u), clamp(v, 0.0, 1.0))).rgb;
}

bool layerInRange(float layer, float firstLayer, float layerCount) {
    return layerCount > 0.5 && layer >= firstLayer - 0.5 && layer < firstLayer + layerCount - 0.5;
}

void main() {
    bool isWater = layerInRange(vLayer, uWaterStillFirstLayer, uWaterStillLayerCount) ||
                   layerInRange(vLayer, uWaterFlowFirstLayer, uWaterFlowLayerCount);
    if (!isWater) discard;

    // ---- Depth gap ----
    float depthGap = 0.0;
    float sceneDepth = texelFetch(uOpaqueDepthTex, ivec2(gl_FragCoord.xy), 0).r;
    if (uDepthSofteningEnabled != 0) {
        float near = 0.05, far = 512.0;
        float linearFrag = (2.0 * near) / (far + near - (gl_FragCoord.z * 2.0 - 1.0) * (far - near));
        float linearScene = (2.0 * near) / (far + near - (sceneDepth * 2.0 - 1.0) * (far - near));
        depthGap = max(0.0, linearScene - linearFrag);
    }

    vec2 screenUv = gl_FragCoord.xy / vec2(textureSize(uSceneColorTex, 0));
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

    // Add rain ripple normals (DerivativeMain RainEffect.glsl)
    if (uWeatherWetness > 0.01) {
        float skylightFactor = clamp(vLight * 10.0 - 9.0, 0.0, 1.0);
        vec2 rainNormal = GetRainNormal(vWorldPos, uWeatherWetness * skylightFactor);
        waveNormalTangent.xy += rainNormal;
        waveNormalTangent = normalize(waveNormalTangent);
    }

    // Transform to world space using TBN (DerivativeMain line 249)
    vec3 normal = normalize(tbnMatrix * waveNormalTangent);

    // ---- Fresnel (DerivativeMain BRDF.glsl FresnelDielectricN) ----
    float NdotV = max(1e-6, dot(normal, viewDir));
    float fresnel = FresnelDielectricN(NdotV, uWaterIOR);

    // ---- Refraction (DerivativeMain Refraction.glsl line 87-97, exact port) ----
    // DerivativeMain: mat3(gbufferModelView) * wavesNormal → view space
    // nv = normalize(gbufferModelView[1].xyz) → view up
    // refractCoord = nv.xy - wavesNormalView.xy
    vec3 normalView = normal; // for our forward pass, normal is in world space
    // Approximate view-space up vector in screen: project world up to screen
    vec3 viewUp = vec3(0.0, 1.0, 0.0); // world up
    // Refraction offset: use normal's XZ slope as screen offset (tangent space approach)
    vec2 refractOffset = -waveNormalTangent.xy; // tangent-space slope = screen-space offset
    float refractionDepth = depthGap * 50.0;
    refractOffset *= clamp(refractionDepth, 0.0, 1.0) * 0.5 / (depthGap * 50.0 + 1e-4);
    vec2 refractUv = screenUv + refractOffset;
    // Depth rejection (DerivativeMain line 108)
    float refractDepthTex = texture(uOpaqueDepthTex, refractUv).r;
    if (refractDepthTex < gl_FragCoord.z) refractUv = screenUv;
    refractUv = clamp(refractUv, vec2(0.0), vec2(1.0));
    vec3 sceneColor = texture(uSceneColorTex, refractUv).rgb;

    // ---- Water fog (DerivativeMain WaterFog.glsl, applied BEFORE reflection) ----
    float LdotV = dot(normalize(uSunDirection), viewDir);
    float fogDist = length(uCameraPos - vWorldPos);
    if (uIsEyeInWater == 1) {
        normal = -normal;
        UnderwaterFog(sceneColor, fogDist);
    } else {
        WaterFog(sceneColor, vLight, LdotV, fogDist);
    }

    // ---- Reflection (DerivativeMain CalculateSpecularReflections) ----
    vec3 reflDir = reflect(-viewDir, normal);
    vec3 skyRefl = sampleSkyReflection(reflDir);
    vec3 ssrRefl = texture(uReflectionTex, screenUv).rgb;
    float ssrLuma = dot(ssrRefl, vec3(0.2126, 0.7152, 0.0722));
    vec3 reflection = (ssrLuma > 0.01) ? ssrRefl : skyRefl;

    // Underwater reflection (DerivativeMain line 103)
    if (uIsEyeInWater == 1) {
        reflection = vec3(0.05, 0.7, 1.0) * 0.3 * clamp(uSkyIntensity, 0.0, 1.0);
    }

    // Sun/moon specular (DerivativeMain uses separate specular highlight)
    // Use tighter exponent for noon, looser for sunset
    float sunAngle = clamp(uSunDirection.y, 0.0, 1.0); // 1.0 at noon, 0.0 at horizon
    float sunExponent = mix(64.0, 1024.0, sunAngle); // tighter at noon
    float sunSpec = pow(clamp(dot(normal, normalize(uSunDirection + viewDir)), 0.0, 1.0), sunExponent);
    sunSpec *= clamp(uSkyIntensity, 0.0, 1.0);
    float moonSpec = pow(clamp(dot(normal, normalize(uMoonDirection + viewDir)), 0.0, 1.0), 64.0);
    moonSpec *= clamp(uMoonVisibility, 0.0, 1.0);

    // Sun reflection (DerivativeMain Atmosphere.glsl RenderSunReflection, exact port)
    vec3 sunDirNorm = normalize(uSunDirection);
    float cosThetaSun = dot(reflDir, sunDirNorm);
    float sunAngularSize = 0.05; // radians (~2.86 degrees)
    if (cosThetaSun > cos(sunAngularSize)) {
        float centerToEdge = clamp(acos(cosThetaSun) / sunAngularSize, 0.0, 1.0);
        vec3 alpha = vec3(0.429, 0.522, 0.614); // AP1 primaries limb darkening
        vec3 limbDark = pow(vec3(1.0 - centerToEdge * centerToEdge), alpha * 0.5);
        vec3 sunLuminance = vec3(1.474, 1.850, 1.912) * 50.0 * limbDark;
        reflection += sunLuminance * clamp(uSkyIntensity, 0.0, 1.0) * (1.0 - centerToEdge * centerToEdge);
    }

    // Moon reflection (DerivativeMain Atmosphere.glsl RenderMoonReflection)
    float cosThetaMoon = dot(reflDir, normalize(-uMoonDirection));
    float moonSize = 5e-3;
    float moonHardness = 2e2;
    float moonDisc = pow(clamp((cosThetaMoon - 1.0 + moonSize) * moonHardness, 0.0, 1.0), 2.0);
    reflection += vec3(moonDisc * 4.0) * clamp(uMoonVisibility, 0.0, 1.0);

    // ---- Combine (DerivativeMain composite1.fsh line 117, premultiplied alpha) ----
    // reflectionData = vec4(reflection * specular, 1.0 - specular)
    // sceneData = sceneData * reflectionData.a + reflectionData.rgb
    // = sceneColor * (1-fresnel) + reflection * fresnel
    vec3 color = sceneColor * (1.0 - fresnel) + reflection * fresnel;

    // Foam: shoreline depth edges
    float foamEdge = smoothstep(0.005, 0.085, depthGap);
    color += vec3(0.85, 0.92, 0.97) * foamEdge * 0.15;

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
