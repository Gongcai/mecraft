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

uniform mat4 uInvViewProj;
uniform mat4 uShadowViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform float uSkyIntensity;
uniform int uShadowsEnabled;
uniform int uSsaoEnabled;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

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

float shadowFactor(vec3 worldPos, vec3 normal) {
    if (uShadowsEnabled == 0) {
        return 1.0;
    }
    vec4 lightClip = uShadowViewProj * vec4(worldPos + normal * 0.015, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.00001);
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.y < 0.0 || proj.x > 1.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float bias = max(0.0015 * (1.0 - dot(normal, normalize(uSunDirection))), 0.0005);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float closest = texture(uShadowMap, proj.xy + vec2(x, y) * texel).r;
            lit += (proj.z - bias <= closest) ? 1.0 : 0.0;
        }
    }
    return mix(0.42, 1.0, lit / 9.0);
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
    vec3 dayLight = texture(uLightmapDay, lightmapUV).rgb;
    vec3 nightLight = texture(uLightmapNight, lightmapUV).rgb;
    vec3 lightColor = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));

    float sunFacing = clamp(dot(normal, normalize(uSunDirection)) * 0.45 + 0.55, 0.25, 1.0);
    float shadow = shadowFactor(worldPos, normal);
    float ssao = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;
    vec3 color = albedo * lightColor * vertexAo * mix(1.0, ssao, 0.65);
    color *= mix(1.0, sunFacing * shadow, clamp(voxelLight.r * uSkyIntensity, 0.0, 1.0) * 0.45);
    color += albedo * emissiveHint * emissiveHint * 0.35;

    if (uFogEnabled != 0) {
        float fogDistance = length(worldPos - uCameraPos);
        float fogFactor = computeFogFactor(fogDistance);
        color = mix(uFogColor, color, fogFactor);
    }

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
