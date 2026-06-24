// Steve arm fragment shader — Mecraft Phase 5.4 enhanced.
// First-person arm lighting follows the held-item lightmap contract and adds
// a soft deferred accent from CSM visibility.

#version 450 core
#include "held_item_shadow.glsl"

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform float uSkyIntensity;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;
uniform float uHeldSceneHdrScale;
uniform float uHurtFlash;

out vec4 FragColor;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uSunDirection);
    float ndotl = max(dot(normal, lightDir), 0.0);
    float shade = mix(0.72, 1.0, ndotl);

    vec2 lightmapUV = vec2(uHeldBlockLight, 1.0 - uHeldSunlight);
    vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
    vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
    vec3 lightColor = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));

    float shadow = sampleHeldItemShadow(vWorldPos, normal);
    float shadowVisibility = mix(0.76, 1.0, shadow);
    float sunAmount = clamp(uHeldSunlight * uSkyIntensity, 0.0, 1.0);
    float deferredAccent = mix(1.0, shadowVisibility, 0.45 * sunAmount);

    vec3 skinColor = mix(texColor.rgb, vec3(1.0, 0.22, 0.22), clamp(uHurtFlash, 0.0, 1.0) * 0.70);
    FragColor = vec4(srgbToLinear(skinColor) * lightColor * shade * deferredAccent * uHeldSceneHdrScale, texColor.a);
}
