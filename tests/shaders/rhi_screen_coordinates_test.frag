#version 450 core
#include "../../assets/shaders/rhi_screen_coordinates.glsl"

layout(location = 0) out vec4 FragColor;

void main() {
    const vec2 extent = vec2(16.0, 8.0);
    vec2 screenUv = rhiNativeFragCoordToScreenUv(gl_FragCoord.xy, extent);
    vec2 textureUv = rhiScreenUvToTextureUv(screenUv);
    vec2 clipUv = rhiScreenUvToClipUv(screenUv);
    ivec2 nativeTexel = rhiScreenUvToNativeTexel(screenUv, ivec2(extent));
    FragColor = vec4(textureUv + clipUv, vec2(nativeTexel) / extent);
}
