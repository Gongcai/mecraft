#version 330 core
in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uFont;
uniform vec4 uTintColor;

void main()
{
    vec4 texColor = texture(uFont, vUV);
    if (texColor.a < 0.1)
        discard;
    FragColor = texColor * uTintColor;
}

