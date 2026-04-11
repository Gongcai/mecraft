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
#include <vector>

class World;
class Player;
class Chunk;

class Renderer {
public:
    enum class FogMode : int {
        Linear = 0,
        Exp = 1,
        Exp2 = 2
    };

    struct FogSettings {
        bool enabled = true;
        FogMode mode = FogMode::Linear;
        glm::vec3 color = glm::vec3(0.67f, 0.84f, 1.0f);
        float startDistance = 140.0f;
        float endDistance = 260.0f;
        float density = 0.01f;
        bool autoDistanceByRenderDistance = true;
        float autoStartOffsetChunks = -0.5f;
        float autoFadeWidthChunks = 1.5f;
    };

    enum class FrustumPlane : size_t {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5,
        Count = 6
    };

#ifndef NDEBUG
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
        int chunkCulled = 0;
        std::array<int, static_cast<size_t>(FrustumPlane::Count)> chunkCulledByPlane{};
    };

    static constexpr size_t MESHING_HISTORY_SIZE = 120;
#endif

    ~Renderer();
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(const World& world, const Camera &camera, const Window &window, const Player& player);

    void setMeshingSubmitBudget(int budget);
    void setRegionChunkSize(int chunkSize);
    void setAtlasAnisotropy(float anisotropy);
    void setFogEnabled(bool enabled);
    void setFogMode(FogMode mode);
    void setFogColor(const glm::vec3& color);
    void setFogLinearDistances(float startDistance, float endDistance);
    void setFogDensity(float density);
    void setFogAutoDistanceEnabled(bool enabled);
    void setFogAutoStartOffsetChunks(float offsetChunks);
    void setFogAutoFadeWidthChunks(float fadeWidthChunks);
    [[nodiscard]] FogSettings getFogSettings() const;
#ifndef NDEBUG
    void setChunkCullingDebugEnabled(bool enabled);
    [[nodiscard]] int getMeshingSubmitBudget() const;
    [[nodiscard]] int getRegionChunkSize() const;
    [[nodiscard]] bool isChunkCullingDebugEnabled() const;
    [[nodiscard]] MeshingFrameStats getMeshingFrameStats() const;
    [[nodiscard]] CullingFrameStats getCullingFrameStats() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingSubmittedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingCompletedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingInFlightHistory() const;
    [[nodiscard]] size_t getMeshingHistoryCount() const;
#endif

    // 视锥剔除
    void updateFrustum(const glm::mat4& viewProj);
    [[nodiscard]] int getDrawCallCount() const;
    [[nodiscard]] float getAtlasAnisotropy() const;
    [[nodiscard]] float getAtlasMaxAnisotropy() const;
private:
    struct Plane {
        glm::vec3 n = glm::vec3(0.0f);
        float d = 0.0f;
    };

    struct ChunkRenderEntry {
        Chunk* chunk = nullptr;
        int regionX = 0;
        int regionZ = 0;
        int chunkX = 0;
        int chunkZ = 0;
        glm::vec3 boundsMin = glm::vec3(0.0f);
        glm::vec3 boundsMax = glm::vec3(0.0f);
    };

    void recordMeshingHistory();
    void drainMeshingResults(const World& world);
    void beginFrame(const Camera& camera, const Window &window);   // 设置 VP 矩阵, 清屏
    void renderWorld(const World& world);
    void bindChunkRenderState(const World& world, const TextureAtlas& atlas) const;
    void submitMeshingJobs(const World& world, const TextureAtlas& atlas);
    void renderOpaqueChunksAndCollectTransparent(const World& world, std::vector<Chunk*>& transparentChunks);
    void renderTransparentChunks(const std::vector<Chunk*>& transparentChunks);
    void initOutlineMesh();
    void initBreakOverlayMesh();
    void renderBlockOutline(const Player& player);
    void renderBlockBreakOverlay(const Player& player);
#ifndef NDEBUG
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax, FrustumPlane* culledPlane) const;
    void recordChunkCull(FrustumPlane plane, int count);
#endif
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const;
    //TODO: 传入 World 和 UI 数据进行渲染
    //void renderWorld(const World& world, const Camera& camera);
    //void renderUI(const UI& ui);
    void endFrame(const Window &window);

    int drawCallCount = 0;

    Shader* m_chunkShader = nullptr;
   // Shader* m_uiShader = nullptr;
    Shader* m_outlineShader = nullptr;
    Shader* m_breakOverlayShader = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    GLuint m_outlineVao = 0;
    GLuint m_outlineVbo = 0;
    GLuint m_breakOverlayVao = 0;
    GLuint m_breakOverlayVbo = 0;

    ChunkMeshingService m_meshingService;
    std::unordered_set<int64_t> m_meshingInFlight;
    int m_meshingSubmitBudget = 8;
    int m_regionChunkSize = 4;
#ifndef NDEBUG
    bool m_chunkCullingDebugEnabled = false;
    int m_meshingSubmittedThisFrame = 0;
    int m_meshingCompletedThisFrame = 0;
    int m_regionTestsThisFrame = 0;
    int m_regionPassedThisFrame = 0;
    int m_columnTestsThisFrame = 0;
    int m_columnPassedThisFrame = 0;
    int m_chunkTestsThisFrame = 0;
    int m_chunkPassedThisFrame = 0;
    int m_chunkCulledThisFrame = 0;
    std::array<int, static_cast<size_t>(FrustumPlane::Count)> m_chunkCulledByPlaneThisFrame{};
    size_t m_meshingHistoryCount = 0;
    std::array<float, MESHING_HISTORY_SIZE> m_meshingSubmittedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingCompletedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingInFlightHistory{};
#endif

    glm::mat4 m_projection = glm::mat4(1.0f);
    glm::mat4 m_view = glm::mat4(1.0f);
    glm::mat4 m_viewProj = glm::mat4(1.0f);
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    FogSettings m_fogSettings{};
    // 视锥体6个平面
    std::array<Plane, 6> m_frustumPlanes{};
    std::vector<ChunkRenderEntry> m_chunkRenderEntries;
};



#endif //MECRAFT_RENDERER_H

