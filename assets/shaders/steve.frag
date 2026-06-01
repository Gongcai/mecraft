// Steve arm fragment shader — Mecraft Phase 5.4 enhanced.
// Player/mob arm with CSM shadow sampling and correct sun direction.
// Phase 5.4: replaces hardcoded sun direction with uSunDirection uniform,
// adds CSM shadow via held_item_shadow.glsl.

#version 450 core
#include "held_item_shadow.glsl"

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform float uAmbientStrength;
uniform float uSkyIntensity;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;

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
    float shadow = sampleHeldItemShadow(vWorldPos, normal);
    float skyLight = uHeldSunlight * clamp(uSkyIntensity, 0.0, 1.0);
    float localLight = max(skyLight, uHeldBlockLight);
    float ambient = mix(0.08, uAmbientStrength, localLight);
    float light = ambient + ndotl * shadow * skyLight * (1.0 - ambient);

    FragColor = vec4(srgbToLinear(texColor.rgb) * light, texColor.a);
}
