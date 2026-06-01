#version 450 core
// Forward vanilla entity fragment shader.
// Uses the legacy entity contract: texture + simple directional light only.

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform float uSkyIntensity;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 sunDir = normalize(vec3(0.3, 1.0, 0.5));
    float diffuse = max(dot(normalize(vNormal), sunDir), 0.0);
    float skyLight = uHeldSunlight * clamp(uSkyIntensity, 0.0, 1.0);
    float localLight = max(skyLight, uHeldBlockLight);
    float ambient = mix(0.08, 0.55, localLight);
    float light = ambient + diffuse * skyLight * (1.0 - ambient);

    FragColor = vec4(texColor.rgb * light, texColor.a);
}
