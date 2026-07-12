#version 450 core

#define A_GPU 1
#define A_GLSL 1
#include "ffx-fsr/ffx_a.h"

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uInputTex;

layout(push_constant) uniform RhiPushConstants {
    vec4 uCon0;
    vec4 uCon1;
    vec4 uCon2;
    vec4 uCon3;
};

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return textureGather(uInputTex, p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(uInputTex, p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(uInputTex, p, 2); }
#include "ffx-fsr/ffx_fsr1.h"

void main() {
    AF3 color;
    FsrEasuF(color,
             AU2(gl_FragCoord.xy),
             floatBitsToUint(uCon0),
             floatBitsToUint(uCon1),
             floatBitsToUint(uCon2),
             floatBitsToUint(uCon3));
    FragColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
