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

class ImGuiRhiRenderer;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;
class StaticMeshRenderer;

/// Owns editor scene entities, mesh assets, picking data, and offscreen targets.
class ModelSceneRuntime {
public:
    ModelSceneRuntime();
    ~ModelSceneRuntime();

    ModelSceneRuntime(const ModelSceneRuntime&) = delete;
    ModelSceneRuntime& operator=(const ModelSceneRuntime&) = delete;

    /// Loads the initial model asset and creates one ECS scene instance.
    [[nodiscard]] bool init(ResourceMgr& resourceMgr,
                            RhiDevice& rhiDevice,
                            ImGuiRhiRenderer& imguiRenderer);
    void shutdown();

    /// Resizes the offscreen viewport and refreshes its ImGui texture binding.
    [[nodiscard]] bool ensureViewport(uint32_t width, uint32_t height);

    /// Records all ECS mesh instances into the offscreen viewport.
    [[nodiscard]] bool recordViewport(RhiCommandList& commandList,
                                      const glm::mat4& viewProjection);

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
    [[nodiscard]] uint64_t viewportTextureId() const { return m_viewportTextureId; }
    [[nodiscard]] uint32_t viewportWidth() const { return m_viewportWidth; }
    [[nodiscard]] uint32_t viewportHeight() const { return m_viewportHeight; }
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
    void destroyViewport();
    void setError(std::string message);

    entt::registry m_registry;
    std::vector<MeshAsset> m_assets;
    RhiDevice* m_rhiDevice = nullptr;
    ImGuiRhiRenderer* m_imguiRenderer = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
    RhiTextureHandle m_colorTexture;
    RhiTextureViewHandle m_colorView;
    RhiTextureHandle m_depthTexture;
    RhiTextureViewHandle m_depthView;
    RhiSamplerHandle m_viewportSampler;
    RhiResourceState m_colorState = RhiResourceState::Undefined;
    RhiResourceState m_depthState = RhiResourceState::Undefined;
    uint64_t m_viewportTextureId = 0u;
    uint32_t m_viewportWidth = 0u;
    uint32_t m_viewportHeight = 0u;
    entt::entity m_selectedEntity = entt::null;
    std::string m_lastError;
};

#endif // MECRAFT_MODEL_SCENE_RUNTIME_H
