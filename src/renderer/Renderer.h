//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RENDERER_H
#define MECRAFT_RENDERER_H
#include "../core/Camera.h"
#include "../resource/ResourceMgr.h"
#include "../core/Window.h"
#include "ChunkMeshingService.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <array>
#include <unordered_set>

class World;
class Player;

class Renderer {
public:
    struct MeshingFrameStats {
        int submitBudget = 0;
        int submitted = 0;
        int completed = 0;
        int inFlight = 0;
    };

    struct CullingFrameStats {
        int regionTests = 0;
        int regionPassed = 0;
        int columnTests = 0;
        int columnPassed = 0;
        int chunkTests = 0;
        int chunkPassed = 0;
    };

    static constexpr size_t MESHING_HISTORY_SIZE = 120;

    ~Renderer();
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(const World& world, const Camera &camera, const Window &window, const Player& player);

    void setMeshingSubmitBudget(int budget);
    void setRegionChunkSize(int chunkSize);
    [[nodiscard]] int getMeshingSubmitBudget() const;
    [[nodiscard]] int getRegionChunkSize() const;
    [[nodiscard]] MeshingFrameStats getMeshingFrameStats() const;
    [[nodiscard]] CullingFrameStats getCullingFrameStats() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingSubmittedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingCompletedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingInFlightHistory() const;
    [[nodiscard]] size_t getMeshingHistoryCount() const;

    // 视锥剔除
    void updateFrustum(const glm::mat4& viewProj);
    [[nodiscard]] bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const;
    [[nodiscard]] int getDrawCallCount() const;
private:
    struct Plane {
        glm::vec3 n = glm::vec3(0.0f);
        float d = 0.0f;
    };

    void recordMeshingHistory();
    void drainMeshingResults(const World& world);
    void beginFrame(const Camera& camera, const Window &window);   // 设置 VP 矩阵, 清屏
    void renderWorld(const World& world);
    void initOutlineMesh();
    void renderBlockOutline(const Player& player);
    //TODO: 传入 World 和 UI 数据进行渲染
    //void renderWorld(const World& world, const Camera& camera);
    //void renderUI(const UI& ui);
    void endFrame(const Window &window);

    int drawCallCount = 0;

    Shader* m_chunkShader = nullptr;
    Shader* m_uiShader = nullptr;
    Shader* m_outlineShader = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    GLuint m_outlineVao = 0;
    GLuint m_outlineVbo = 0;

    ChunkMeshingService m_meshingService;
    std::unordered_set<int64_t> m_meshingInFlight;
    int m_meshingSubmitBudget = 8;
    int m_regionChunkSize = 4;
    int m_meshingSubmittedThisFrame = 0;
    int m_meshingCompletedThisFrame = 0;
    int m_regionTestsThisFrame = 0;
    int m_regionPassedThisFrame = 0;
    int m_columnTestsThisFrame = 0;
    int m_columnPassedThisFrame = 0;
    int m_chunkTestsThisFrame = 0;
    int m_chunkPassedThisFrame = 0;
    size_t m_meshingHistoryCount = 0;
    std::array<float, MESHING_HISTORY_SIZE> m_meshingSubmittedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingCompletedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingInFlightHistory{};

    glm::mat4 m_projection = glm::mat4(1.0f);
    glm::mat4 m_view = glm::mat4(1.0f);
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    // 视锥体6个平面
    std::array<Plane, 6> m_frustumPlanes{};
};



#endif //MECRAFT_RENDERER_H

