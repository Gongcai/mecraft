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
uniform float uSkyIntensity;
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

float compareShadow(vec3 proj, vec2 offset, float bias) {
    float closest = texture(uShadowMap, proj.xy + offset).r;
    return (proj.z - bias <= closest) ? 1.0 : 0.0;
}

float shadowFactor(vec3 worldPos, vec3 normal) {
    if (uShadowsEnabled == 0) {
        return 1.0;
    }
    vec3 sunDir = normalize(uSunDirection);
    vec4 lightClip = uShadowViewProj * vec4(worldPos + normal * uShadowNormalOffset, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.00001);
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.y < 0.0 || proj.x > 1.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float ndotl = clamp(dot(normal, sunDir), 0.0, 1.0);
    float bias = uShadowConstantBias + uShadowSlopeBias * (1.0 - ndotl);

    if (uSoftShadowsEnabled == 0) {
        return mix(0.42, 1.0, compareShadow(proj, vec2(0.0), bias));
    }

    const vec2 poisson[12] = vec2[](
        vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
        vec2(-0.203,  0.621), vec2( 0.285,  0.350), vec2( 0.709,  0.129),
        vec2( 0.522, -0.542), vec2( 0.185, -0.893), vec2(-0.118, -0.168),
        vec2( 0.064,  0.078), vec2( 0.398, -0.181), vec2(-0.461,  0.012)
    );
    float receiverDistance = clamp((proj.z - texture(uShadowMap, proj.xy).r) * 80.0, 0.0, 1.0);
    float radius = max(uShadowSoftness, 0.1) * mix(0.75, 1.35, receiverDistance);
    float lit = 0.0;
    for (int i = 0; i < 12; ++i) {
        lit += compareShadow(proj, poisson[i] * texel * radius, bias);
    }
    return mix(0.42, 1.0, lit / 12.0);
}

float contactShadow(vec3 worldPos, vec3 normal, vec2 voxelLight) {
    if (uShadowsEnabled == 0 || uContactShadowsEnabled == 0 || uContactShadowStrength <= 0.001) {
        return 1.0;
    }
    vec3 sunDir = normalize(uSunDirection);
    float sunTerm = clamp(dot(normal, sunDir) * 0.5 + 0.5, 0.0, 1.0);
    float skyTerm = clamp(voxelLight.r * uSkyIntensity, 0.0, 1.0);
    if (sunTerm * skyTerm <= 0.02) {
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
        if (sceneDepth < sampleProjectedDepth - 0.00035) {
            occlusion += 1.0 - t * 0.45;
        }
    }
    occlusion = clamp(occlusion / 4.0, 0.0, 1.0);
    return 1.0 - occlusion * uContactShadowStrength * sunTerm * skyTerm;
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
    vec3 lightColor = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));
    float skyLightMask = clamp(voxelLight.r * uSkyIntensity, 0.0, 1.0);
    lightColor *= mix(vec3(1.0), uSunLightColor, skyLightMask * 0.35);

    float sunFacing = clamp(dot(normal, normalize(uSunDirection)) * 0.45 + 0.55, 0.25, 1.0);
    float shadow = shadowFactor(worldPos, normal);
    shadow *= contactShadow(worldPos, normal, voxelLight);
    float ssao = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;
    vec3 color = albedo * lightColor * vertexAo * mix(1.0, ssao, 0.65);
    color *= mix(1.0, sunFacing * shadow, skyLightMask * 0.45);
    color += albedo * emissiveHint * emissiveHint * 0.35;

    if (uFogEnabled != 0) {
        float fogDistance = length(worldPos - uCameraPos);
        float fogFactor = computeFogFactor(fogDistance);
        color = mix(srgbToLinear(uFogColor), color, fogFactor);
    }

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
