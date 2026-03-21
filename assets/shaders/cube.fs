#version 330 core

in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uMainTex;
uniform float uNormalVizMix;

void main()
{
    vec3 normalColor = normalize(vNormal) * 0.5 + 0.5;
    vec3 texColor = texture(uMainTex, vTexCoord).rgb;

    // Keep texture as the base output while still allowing normal debug overlay.
    vec3 color = mix(texColor, normalColor, uNormalVizMix);
    FragColor = vec4(color, 1.0);
}

