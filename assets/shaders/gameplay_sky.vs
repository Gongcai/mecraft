#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

out vec3 vWorldDir;
out vec2 vUV;
out vec4 vColor;

uniform int uMode;
uniform mat4 uProjection;
uniform mat4 uView;
uniform mat4 uModel;
uniform vec2 uUvMin;
uniform vec2 uUvMax;
void main() {
    vUV = mix(uUvMin, uUvMax, aUV);
    vColor = aColor;

    if (uMode == 0) {
        vec2 clip = aPos.xy;
        vec4 viewNear = inverse(uProjection) * vec4(clip, 1.0, 1.0);
        vec3 viewDir = normalize(viewNear.xyz / viewNear.w);
        vWorldDir = mat3(transpose(uView)) * viewDir;
        gl_Position = vec4(clip, 0.0, 1.0);
        return;
    }

    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldDir = normalize(worldPos.xyz);
    if (uMode == 3) {
        vColor = vec4(vec3(aUV.x), 1.0);
    }
    vec4 pos = uProjection * uView * worldPos;
    gl_Position = (uMode == 3) ? pos : pos.xyww;
}
