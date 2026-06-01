#version 450 core
// Forward vanilla item model fragment shader.
// No CSM shadow sampling, no held_item_shadow.glsl contract.
// Matches the legacy item shader contract: texture color * baked vertex shade.

in vec2 vUV;
in float vShade;
in vec3 vWorldPos;
in vec3 vNormal;

uniform sampler2D uAtlas;
uniform float uSkyIntensity;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uAtlas, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    float skyLight = uHeldSunlight * clamp(uSkyIntensity, 0.0, 1.0);
    float localLight = max(skyLight, uHeldBlockLight);
    float light = mix(0.08, 1.0, localLight);
    FragColor = vec4(texColor.rgb * vShade * light, texColor.a);
}
