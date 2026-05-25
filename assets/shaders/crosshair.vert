#version 330 core
layout (location = 0) in vec2 aPos;

uniform vec2 uScreenSize;
uniform vec2 uOffset;

void main()
{
    // aPos is in pixel coords with origin at bottom-left (same as inventory/text/ui_color shaders)
    vec2 worldPos = aPos + uOffset;
    vec2 ndc = (worldPos / uScreenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
