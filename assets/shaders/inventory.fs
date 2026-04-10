#version 330 core
in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uAtlas;
uniform vec4 uTintColor;

void main()
{
    vec4 texColor = texture(uAtlas, vUV);
    if (texColor.a < 0.1 )
        discard;
    FragColor = texColor * uTintColor;
}
