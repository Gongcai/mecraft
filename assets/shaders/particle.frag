#version 450 core
out vec4 FragColor;

in vec2 vUV;
in float vLayer;
in float vAlpha;
in float vBiomeTintFactor;

uniform sampler2DArray texArray;
uniform vec3 uBiomeTintColor;

void main() {
    vec4 texColor = texture(texArray, vec3(vUV, vLayer));
    if (texColor.a < 0.1)
        discard;
    if (vBiomeTintFactor > 0.5) {
        texColor.rgb *= uBiomeTintColor;
    }
    FragColor = vec4(texColor.rgb, texColor.a * vAlpha);
}
