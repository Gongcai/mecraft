#version 330 core
in vec2 vUV;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D uFont;

void main()
{
    float coverage = texture(uFont, vUV).r;
    if (coverage <= 0.001)
        discard;
    FragColor = vec4(vColor.rgb, vColor.a * coverage);
}

