//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_TESTCUBE_H
#define MECRAFT_TESTCUBE_H

#include <glm/glm.hpp>
#include "Shader.h"
#include "../core/Time.h"

class ResourceMgr;

// 仅用作渲染测试 - 只在 Debug 模式下可用
#ifndef NDEBUG
class TestCube {
public:
    TestCube();
    explicit TestCube(glm::vec3 pos);
    TestCube(glm::vec3 pos, ResourceMgr& resourceMgr);
    void draw();
    void setViewProjection(glm::mat4 viewProj);
    void update();
    void setScale(glm::vec3 scale);
private:
    unsigned int loadTexture(const char* path);
    glm::vec3 pos = {0.0f,0.0f,0.0f};
    glm::vec3 scale = {1.0f,1.0f,1.0f};
    float rotationY = 0.0f;
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int texture = 0;
    Shader shader = Shader("../assets/shaders/chunk.vs", "../assets/shaders/chunk.fs");

};
#endif // NDEBUG


#endif //MECRAFT_TESTCUBE_H