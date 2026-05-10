#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uVolumetricTex;

void main() {
    vec3 scene = texture(uSceneTex, vTexCoord).rgb;
    vec4 volumetric = texture(uVolumetricTex, vTexCoord);
    FragColor = vec4(scene * volumetric.a + volumetric.rgb, 1.0);
}
