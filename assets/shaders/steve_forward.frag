#version 450 core

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform float uSkyIntensity;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;
uniform float uHurtFlash;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    float skyLight = uHeldSunlight * clamp(uSkyIntensity, 0.0, 1.0);
    float localLight = max(skyLight, uHeldBlockLight);
    float ambient = mix(0.08, 0.55, localLight);
    float diffuse = max(dot(normalize(vNormal), normalize(vec3(0.3, 1.0, 0.5))), 0.0);
    float light = ambient + diffuse * skyLight * (1.0 - ambient);
    vec3 skinColor = mix(texColor.rgb, vec3(1.0, 0.22, 0.22),
                         clamp(uHurtFlash, 0.0, 1.0) * 0.70);
    FragColor = vec4(skinColor * light, texColor.a);
}
