#version 450 core
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;

out vec4 FragColor;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    // Simple directional lighting
    vec3 sunDir = normalize(vec3(0.3, 1.0, 0.5));
    float diffuse = max(dot(normalize(vNormal), sunDir), 0.0);
    float ambient = 0.55;
    float light = ambient + diffuse * 0.45;

    FragColor = vec4(srgbToLinear(texColor.rgb) * light, texColor.a);
}
