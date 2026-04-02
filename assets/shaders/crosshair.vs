#version 330 core
layout (location = 0) in vec2 aPos;

uniform vec2 uScreenSize;

void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
