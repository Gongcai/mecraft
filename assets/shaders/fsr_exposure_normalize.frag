#version 450 core

layout(location = 0) out float FragExposure;

layout(binding = 0) uniform sampler2D uSceneExposure;
layout(push_constant) uniform RhiPushConstants {
    vec4 pPreExposure;
};

void main() {
    float sceneExposure = texelFetch(uSceneExposure, ivec2(0), 0).r;
    FragExposure = sceneExposure / pPreExposure.x;
}
