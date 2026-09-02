#version 450 core

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D uTexture;
layout(binding = 1) uniform sampler2D uLightmapDay;
layout(binding = 2) uniform sampler2D uLightmapNight;

layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLightingParams; // (skyLight, blockLight, skyIntensity, animationTime)
};

void main() {
    vec4 texel = texture(uTexture, vUv);
    if (texel.a < 0.1) {
        discard;
    }
    // Vanilla lightmap shading, identical to forward_basic_terrain.frag so the
    // held arm matches forward terrain colors exactly (gamma space, raw blit).
    vec2 lightmapUv = vec2(uLightingParams.y, 1.0 - uLightingParams.x);
    vec3 dayLight = texture(uLightmapDay, lightmapUv).rgb;
    vec3 nightLight = texture(uLightmapNight, lightmapUv).rgb;
    vec3 lightColor = mix(nightLight, dayLight, clamp(uLightingParams.z, 0.0, 1.0));
    fragColor = vec4(texel.rgb * lightColor, texel.a);
}
