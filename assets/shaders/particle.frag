#version 450 core
out vec4 FragColor;

in vec2 vUV;
in float vLayer;
in float vAlpha;
in float vBiomeTintFactor;

uniform sampler2DArray texArray;
uniform vec3 uBiomeTintColor;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(texArray, vec3(vUV, vLayer));
    if (texColor.a < 0.1)
        discard;
    vec3 albedo = srgbToLinear(texColor.rgb);
    if (vBiomeTintFactor > 0.5) {
        albedo *= srgbToLinear(uBiomeTintColor);
    }
    FragColor = vec4(albedo, texColor.a * vAlpha);
}
