#version 450 core
// Forward vanilla entity fragment shader.
// Uses the legacy entity contract: texture + simple directional light only.

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 sunDir = normalize(vec3(0.3, 1.0, 0.5));
    float diffuse = max(dot(normalize(vNormal), sunDir), 0.0);
    float ambient = 0.55;
    float light = ambient + diffuse * 0.45;

    FragColor = vec4(texColor.rgb * light, texColor.a);
}
