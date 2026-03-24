//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RENDERER_H
#define MECRAFT_RENDERER_H
#include "../core/Camera.h"
#include "../resource/ResourceMgr.h"
#include "../core/Window.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <array>
#include <memory>

#include "TestCube.h"


class Renderer {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(const Camera &camera, const Window &window);

    // 视锥剔除
    void updateFrustum(const glm::mat4& viewProj);
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const;

private:
    void beginFrame(const Camera& camera, const Window &window);   // 设置 VP 矩阵, 清屏
    void renderWorld();
    //TODO: 传入 World 和 UI 数据进行渲染
    //void renderWorld(const World& world, const Camera& camera);
    //void renderUI(const UI& ui);
    void endFrame(const Window &window);


    Shader* m_chunkShader = nullptr;
    Shader* m_uiShader = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    glm::mat4 m_projection;
    glm::mat4 m_view;
    TestCube *m_testCube = nullptr;
    // 视锥体6个平面
    std::array<glm::vec4, 6> m_frustumPlanes;
};



#endif //MECRAFT_RENDERER_H