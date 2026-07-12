#version 450 core
layout(location = 0) in vec3 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform samplerCube uSkybox;
void main() {
    fragColor = texture(uSkybox, vTexCoord);
}
