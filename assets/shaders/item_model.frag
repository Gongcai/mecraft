// Item model fragment shader — Mecraft Phase 5.4 enhanced.
// Held item sprites use lightmap color plus a soft deferred accent.

#version 450 core
#include "held_item_shadow.glsl"

in vec2 vUV;
in float vShade;
in vec3 vWorldPos;
in vec3 vNormal;

uniform sampler2D uAtlas;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform float uSkyIntensity;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;
uniform float uHeldSceneHdrScale;

out vec4 FragColor;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(uAtlas, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 albedo = srgbToLinear(texColor.rgb);
    vec2 lightmapUV = vec2(uHeldBlockLight, 1.0 - uHeldSunlight);
    vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
    vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
    vec3 lightColor = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));

    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uSunDirection);
    float ndotl = max(dot(normal, lightDir), 0.0);
    float shadow = sampleHeldItemShadow(vWorldPos, normal);
    float shadowVisibility = mix(0.74, 1.0, shadow);
    float directionalShape = mix(0.92, 1.12, ndotl) * shadowVisibility;
    float sunAmount = clamp(uHeldSunlight * uSkyIntensity, 0.0, 1.0);
    float deferredAccent = mix(1.0, directionalShape, 0.55 * sunAmount);

    FragColor = vec4(albedo * lightColor * clamp(vShade, 0.0, 1.0) * deferredAccent * uHeldSceneHdrScale, texColor.a);
}
