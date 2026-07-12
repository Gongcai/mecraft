#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vAnimationData;
layout(binding = 0) uniform sampler2DArray uTextureArray;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uAnimationTime;
};
void main() {
    float layer = vAnimationData.x;
    if (vAnimationData.w > 0.5 && vAnimationData.y > 1.0 && vAnimationData.z > 0.0) {
        layer += mod(floor(uAnimationTime.x * vAnimationData.z), vAnimationData.y);
    }
    if (textureLod(uTextureArray, vec3(vUv, layer), 0.0).a < 0.1) discard;
}
