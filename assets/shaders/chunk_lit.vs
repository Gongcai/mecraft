#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;     // 简单的面法线索引或方向
layout (location = 3) in float aSunlight;   // 0.0 - 15.0
layout (location = 4) in float aBlockLight; // 0.0 - 15.0
layout (location = 5) in float aAO;         // 0.0 - 3.0

uniform mat4 model;
uniform mat4 viewProj; // 通常是 projection * view

out vec2 vUV;
out float vLight;
out float vAO;
out float vNormal;

void main() {
    gl_Position = viewProj * model * vec4(aPos, 1.0);

    vUV = aUV;

    // 将光照强度标准化为 0.0 - 1.0
    float sun = aSunlight / 15.0;
    float block = aBlockLight / 15.0;
    vLight = max(sun, block);

    vAO = aAO;
    vNormal = aNormal;
}

