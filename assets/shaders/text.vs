#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

uniform vec2 uScreenSize;

out vec2 vUV;

void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
    vUV = aUV;
}

