#version 330 core
out vec4 FragColor;

in vec2 vUV;
in float vLight;
in float vAO;
in float vNormal;

uniform sampler2D texAtlas;

// Ambient Occlusion 的四个亮度等级
const float aoLevels[4] = float[](0.4, 0.65, 0.85, 1.0);

// Minimum ambient light to prevent complete darkness (adjustable)
const float AMBIENT_LIGHT = 0.04;

void main() {
    vec4 texColor = texture(texAtlas, vUV);

    // 如果是透明通道（例如树叶或空气），丢弃像素
    if(texColor.a < 0.1)
        discard;

    // AO 处理：映射 0-3 级到亮度乘法因子
    float aoFactor = aoLevels[int(vAO + 0.5)];

    // 光照：使用很小的环境光防止完全黑暗
    float lightFactor = max(vLight, AMBIENT_LIGHT);

    // 组合纹理、光照与 AO
    FragColor = vec4(texColor.rgb * lightFactor * aoFactor, texColor.a);
}

