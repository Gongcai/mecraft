#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 iModel0;
layout(location = 4) in vec4 iModel1;
layout(location = 5) in vec4 iModel2;
layout(location = 6) in vec4 iModel3;
layout(location = 7) in vec2 iLight;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vLight;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    vec4 uLighting;
};
void main() {
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    gl_Position = uViewProj * model * vec4(aPosition, 1.0);
    vUv = aUv;
    vNormal = normalize(mat3(model) * aNormal);
    vLight = iLight;
}
