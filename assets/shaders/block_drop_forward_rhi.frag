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
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLightingAnimation;
};
void main() {
    float layer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        layer += mod(floor(uLightingAnimation.w * vAnimationFps), vAnimationFrameCount);
    }
    vec4 texel = texture(uTextureArray, vec3(vUv, layer));
    if (texel.a < 0.1) discard;
    if (vTintKind == 1u) texel.rgb *= texture(uGrassColormap, vTintUv).rgb;
    if (vTintKind == 2u) texel.rgb *= texture(uFoliageColormap, vTintUv).rgb;
    float skyLight = uLightingAnimation.x * clamp(uLightingAnimation.z, 0.0, 1.0);
    float light = mix(0.08, 1.0, max(skyLight, uLightingAnimation.y));
    float ao = mix(0.72, 1.0, clamp(vAo / 3.0, 0.0, 1.0));
    fragColor = vec4(texel.rgb * light * ao, texel.a);
}
