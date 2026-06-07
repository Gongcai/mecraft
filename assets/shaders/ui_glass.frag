#version 330 core

out vec4 FragColor;

uniform sampler2D uBackdrop;
uniform vec2 uBackdropSize;
uniform vec4 uTint;
uniform float uOpacity;
uniform float uSaturation;
uniform float uDarken;

void main()
{
    vec2 uv = gl_FragCoord.xy / max(uBackdropSize, vec2(1.0));
    vec3 backdrop = texture(uBackdrop, uv).rgb;

    float luma = dot(backdrop, vec3(0.299, 0.587, 0.114));
    vec3 softened = mix(vec3(luma), backdrop, uSaturation);
    softened *= uDarken;

    vec3 tinted = mix(softened, uTint.rgb, clamp(uTint.a, 0.0, 1.0));
    FragColor = vec4(tinted, clamp(uOpacity, 0.0, 1.0));
}
