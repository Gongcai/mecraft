#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneLightingTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uSkyCaptureTex;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform float uWeatherWetness;
uniform float uTime;

const float kTwoPi = 6.28318530718;

vec2 directionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / kTwoPi + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    vec4 packedMaterial = texture(uMaterialTex, vTexCoord);
    SurfaceMaterial material = unpackGBufferMaterial(packedMaterial);
    SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));

    if (depth >= 0.9999) {
        vec3 sky = texture(uSkyCaptureTex, vTexCoord).rgb;
        FragColor = vec4(sky, 0.0);
        return;
    }

    vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec3 viewDir = normalize(worldPos - uCameraPos);
    vec3 reflectedDir = reflect(viewDir, normal);
    vec3 skyReflection = texture(uSkyCaptureTex, directionToSkyCaptureUv(reflectedDir)).rgb;

    // Placeholder resolve: keeps the target meaningful until SSR/hit-mask tracing is dropped in.
    vec3 sceneFallback = texture(uSceneLightingTex, vTexCoord).rgb;
    float smoothness = 1.0 - clamp(material.roughness, 0.0, 1.0);
    float wetBoost = clamp(uWeatherWetness, 0.0, 1.0) * aux.wetnessMask * clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float reflectance = clamp(material.f0 * 2.4 + smoothness * 0.22 + wetBoost * (0.14 + aux.porosity * 0.12), 0.0, 1.0);
    vec3 color = mix(sceneFallback * 0.08, skyReflection, 0.70 + smoothness * 0.22);

    FragColor = vec4(color, reflectance);
}
