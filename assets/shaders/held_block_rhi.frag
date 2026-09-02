#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in float vAo;
layout(location = 2) in float vLayer;
layout(location = 3) in float vAnimationFrameCount;
layout(location = 4) in float vAnimationFps;
layout(location = 5) in float vAnimated;
layout(location = 6) flat in uint vTintKind;
layout(location = 7) in vec2 vTintUv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2DArray uTextureArray;
layout(binding = 1) uniform sampler2D uGrassColormap;
layout(binding = 2) uniform sampler2D uFoliageColormap;
layout(binding = 3) uniform sampler2D uLightmapDay;
layout(binding = 4) uniform sampler2D uLightmapNight;

layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLightingParams; // (skyLight, blockLight, skyIntensity, animationTime)
};

// Ambient occlusion levels, identical to forward_basic_terrain.frag.
const float aoLevels[4] = float[](0.62, 0.75, 0.87, 1.0);

void main() {
    float layer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        layer += mod(floor(uLightingParams.w * vAnimationFps), vAnimationFrameCount);
    }
    vec4 texel = texture(uTextureArray, vec3(vUv, layer));
    if (texel.a < 0.1) {
        discard;
    }
    if (vTintKind == 1u) texel.rgb *= texture(uGrassColormap, vTintUv).rgb;
    if (vTintKind == 2u) texel.rgb *= texture(uFoliageColormap, vTintUv).rgb;

    float aoIdx = clamp(vAo, 0.0, 3.0);
    int aoLow = int(aoIdx);
    int aoHigh = min(aoLow + 1, 3);
    float aoFactor = mix(aoLevels[aoLow], aoLevels[aoHigh], fract(aoIdx));

    // Vanilla lightmap shading, identical to forward_basic_terrain.frag so the
    // held block matches forward terrain colors exactly (gamma space, raw blit).
    vec2 lightmapUv = vec2(uLightingParams.y, 1.0 - uLightingParams.x);
    vec3 dayLight = texture(uLightmapDay, lightmapUv).rgb;
    vec3 nightLight = texture(uLightmapNight, lightmapUv).rgb;
    vec3 lightColor = mix(nightLight, dayLight, clamp(uLightingParams.z, 0.0, 1.0));
    fragColor = vec4(texel.rgb * lightColor * aoFactor, texel.a);
}
