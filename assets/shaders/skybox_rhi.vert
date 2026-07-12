#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 0) out vec3 vTexCoord;
layout(push_constant) uniform RhiPushConstants {
    mat4 uProjection;
    mat4 uView;
};
void main() {
    vTexCoord = aPosition;
    vec4 position = uProjection * uView * vec4(aPosition, 1.0);
    gl_Position = position.xyww;
}
