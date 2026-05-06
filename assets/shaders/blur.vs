#version 330 core

out vec2 vTexCoord;

void main() {
    // Fullscreen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    vTexCoord = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
