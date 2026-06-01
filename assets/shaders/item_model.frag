// Item model fragment shader — Mecraft Phase 5.4 enhanced.
// Held item sprites with CSM shadow sampling and directional lighting.
// Phase 5.4: replaces pure texture*shade with directional light + shadow.
// vShade is used as ambient fallback when shadows are disabled.

#version 450 core
#include "held_item_shadow.glsl"

in vec2 vUV;
in float vShade;
in vec3 vWorldPos;
in vec3 vNormal;

uniform sampler2D uAtlas;
uniform float uAmbientStrength;
uniform float uSkyIntensity;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;

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
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uSunDirection);
    float ndotl = max(dot(normal, lightDir), 0.0);
    float shadow = sampleHeldItemShadow(vWorldPos, normal);
    float skyLight = uHeldSunlight * clamp(uSkyIntensity, 0.0, 1.0);
    float localLight = max(skyLight, uHeldBlockLight);
    float ambient = mix(0.08, uAmbientStrength, localLight);
    float light = ambient + ndotl * shadow * skyLight * (1.0 - ambient);

    FragColor = vec4(albedo * light, texColor.a);
}
