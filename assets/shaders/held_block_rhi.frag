#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in float vAo;
layout(location = 2) flat in uint vLayer;
layout(location = 3) flat in uint vTintKind;
layout(location = 4) in vec2 vTintUv;
layout(location = 5) in vec3 vNormal;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2DArray uTextureArray;
layout(binding = 1) uniform sampler2D uGrassColormap;
layout(binding = 2) uniform sampler2D uFoliageColormap;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uAmbientRadiance;
    vec4 uDirectRadiance;
};

// Textures store sRGB-encoded texels; the HDR scene color buffer is linear.
vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

// Scene-calibrated lighting: uAmbientRadiance/uDirectRadiance are linear
// radiance factors computed on the CPU from the same environment data the
// terrain shaders use. uDirectRadiance.w reproduces the deferred pipeline's
// extra albedo multiply on the direct sun term.
vec3 evaluateHeldRadiance(vec3 albedo, vec3 normal) {
    float diffuse = max(dot(normalize(normal), normalize(vec3(0.3, 1.0, 0.5))), 0.0);
    vec3 direct = uDirectRadiance.rgb * diffuse * mix(vec3(1.0), albedo, uDirectRadiance.w);
    return albedo * uAmbientRadiance.rgb + albedo * direct;
}

void main() {
    vec4 texel = texture(uTextureArray, vec3(vUv, float(vLayer)));
    if (texel.a < 0.1) {
        discard;
    }
    vec3 albedo = srgbToLinear(texel.rgb);
    if (vTintKind == 1u) albedo *= srgbToLinear(texture(uGrassColormap, vTintUv).rgb);
    if (vTintKind == 2u) albedo *= srgbToLinear(texture(uFoliageColormap, vTintUv).rgb);
    float ao = mix(0.72, 1.0, clamp(vAo / 3.0, 0.0, 1.0));
    fragColor = vec4(evaluateHeldRadiance(albedo, vNormal) * ao, texel.a);
}
