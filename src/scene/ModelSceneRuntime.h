#ifndef MECRAFT_MODEL_SCENE_RUNTIME_H
#define MECRAFT_MODEL_SCENE_RUNTIME_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"
#include "renderer/core/IDeferredGeometryProvider.h"

class ImGuiRhiRenderer;
class ResourceMgr;
class RhiCommandList;
class RhiCommandListPool;
class RhiDevice;
class StaticMeshRenderer;
class ModelSceneDeferredRenderer;

/// Owns editor scene entities, mesh assets, picking data, and offscreen targets.
class ModelSceneRuntime : public IDeferredGeometryProvider {
public:
    ModelSceneRuntime();
    ~ModelSceneRuntime();

    ModelSceneRuntime(const ModelSceneRuntime&) = delete;
    ModelSceneRuntime& operator=(const ModelSceneRuntime&) = delete;

    /// Loads the initial model asset and creates one ECS scene instance.
    [[nodiscard]] bool init(ResourceMgr& resourceMgr,
                            RhiDevice& rhiDevice,
                            RhiCommandListPool& commandListPool,
                            ImGuiRhiRenderer& imguiRenderer);
    void shutdown();

    /// Resizes the offscreen viewport and refreshes its ImGui texture binding.
    [[nodiscard]] bool ensureViewport(uint32_t width, uint32_t height);

    /// Renders all ECS mesh instances through the deferred viewport pipeline.
    [[nodiscard]] bool renderViewport(const glm::mat4& view,
                                      const glm::mat4& projection,
                                      const glm::vec3& cameraPosition,
                                      float deltaTime);

    [[nodiscard]] bool prepareGBuffer(
        RhiCommandList& commandList,
        const FrameContext& context) override;
    void renderToGBuffer(
        RhiCommandList& commandList,
        const glm::mat4& viewProjection,
        const glm::mat4& previousViewProjection) override;
    void renderToShadowMap(
        RhiCommandList& commandList,
        const glm::mat4& shadowViewProjection) override;

    /// Imports or reuses one glTF asset and creates an independent ECS instance.
    /// @param path Filesystem path to a GLB or glTF document.
    /// @return Created scene entity, or entt::null when import fails.
    [[nodiscard]] entt::entity importModel(const std::string& path);

    /// Destroys one scene instance without affecting shared mesh assets.
    void destroyEntity(entt::entity entity);

    /// Selects the nearest pickable ECS entity intersected by a world-space ray.
    /// @param rayOrigin World-space ray origin.
    /// @param rayDirection Normalized world-space direction.
    /// @return Nearest intersected entity, or entt::null.
    [[nodiscard]] entt::entity pick(const glm::vec3& rayOrigin,
                                    const glm::vec3& rayDirection) const;

    /// Rebuilds world and previous-world matrices from editable local transforms.
    void syncTransforms();

    [[nodiscard]] entt::registry& registry() { return m_registry; }
    [[nodiscard]] const entt::registry& registry() const { return m_registry; }
    [[nodiscard]] entt::entity selectedEntity() const { return m_selectedEntity; }
    void setSelectedEntity(entt::entity entity) { m_selectedEntity = entity; }
    [[nodiscard]] uint64_t viewportTextureId() const;
    [[nodiscard]] uint32_t viewportWidth() const;
    [[nodiscard]] uint32_t viewportHeight() const;

    /// Updates the deferred environment time used by sky and lighting passes.
    /// @param timeOfDaySeconds Time within the 1200-second world day.
    void setTimeOfDay(float timeOfDaySeconds);

    /// Returns the current deferred environment time in seconds.
    [[nodiscard]] float timeOfDay() const;
    [[nodiscard]] size_t assetCount() const { return m_assets.size(); }
    [[nodiscard]] const std::string& assetName(size_t index) const;
    [[nodiscard]] const std::string& assetPath(size_t index) const;
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

private:
    struct MeshAsset {
        std::string name;
        std::string path;
        std::unique_ptr<StaticMeshRenderer> renderer;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };

    [[nodiscard]] bool loadMeshAsset(ResourceMgr& resourceMgr,
                                     const std::string& name,
                                     const std::string& path,
                                     uint32_t& assetIndex);
    [[nodiscard]] entt::entity instantiateAsset(uint32_t assetIndex,
                                                const std::string& instanceName);
    void setError(std::string message);

    entt::registry m_registry;
    std::vector<MeshAsset> m_assets;
    ResourceMgr* m_resourceMgr = nullptr;
    std::unique_ptr<ModelSceneDeferredRenderer> m_deferredRenderer;
    entt::entity m_selectedEntity = entt::null;
    std::string m_lastError;
};

#endif // MECRAFT_MODEL_SCENE_RUNTIME_H
