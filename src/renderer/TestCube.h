//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_TESTCUBE_H
#define MECRAFT_TESTCUBE_H

#include <stb/stb_image.h>
#include <glm/glm.hpp>
#include "Shader.h"
// 仅用作渲染测试
class TestCube {
public:
    TestCube();
    explicit TestCube(glm::vec3 pos);
    void draw();
    void setViewProjection(glm::mat4 viewProj);
private:
    unsigned int loadTexture(const char* path);
    glm::vec3 pos = {0.0f,0.0f,0.0f};
    unsigned int VAO, VBO;
    unsigned int texture;
    Shader shader = Shader("../assets/shaders/chunk.vs", "../assets/shaders/chunk.fs");

};


#endif //MECRAFT_TESTCUBE_H