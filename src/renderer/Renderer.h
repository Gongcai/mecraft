//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RENDERER_H
#define MECRAFT_RENDERER_H
#include "../core/Camera.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <array>



class Renderer {
public:
    void init();
    void shutdown();

    void beginFrame(const Camera& camera);   // 设置 VP 矩阵, 清屏
    void renderWorld();
    //TODO: 传入 World 和 UI 数据进行渲染
    //void renderWorld(const World& world, const Camera& camera);
    //void renderUI(const UI& ui);
    void endFrame();

    // 视锥剔除
    void updateFrustum(const glm::mat4& viewProj);
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const;

private:
    Shader m_chunkShader;
    Shader m_uiShader;

    glm::mat4 m_projection;
    glm::mat4 m_view;

    // 视锥体6个平面
    std::array<glm::vec4, 6> m_frustumPlanes;
};



#endif //MECRAFT_RENDERER_H