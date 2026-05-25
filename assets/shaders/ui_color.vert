#version 330 core
layout (location = 0) in vec2 aPos;

uniform vec2 uScreenSize;

void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
}
