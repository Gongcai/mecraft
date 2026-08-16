#ifndef MECRAFT_MODEL_SCENE_RUNTIME_H
#define MECRAFT_MODEL_SCENE_RUNTIME_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"
#include "renderer/core/IDeferredGeometryProvider.h"
#include "renderer/passes/ReflectionProbeCapturePass.h"
#include "ModelSceneDocument.h"

class ImGuiRhiRenderer;
class ResourceMgr;
struct GpuFrameStats;
struct RenderGraphFrameStats;
struct RenderFrameClock;
class RhiCommandList;
class RhiCommandListPool;
class RhiDevice;
class StaticMeshRenderer;
class ModelSceneDeferredRenderer;
struct RenderSettings;
namespace ecs {
struct LocalTransformComponent;
}

/// Owns editor scene entities, mesh assets, picking data, and offscreen targets.
class ModelSceneRuntime : public IDeferredGeometryProvider, public IReflectionProbeCaptureRenderer {
public:
    ModelSceneRuntime();
    ~ModelSceneRuntime();

    ModelSceneRuntime(const ModelSceneRuntime&) = delete;
    ModelSceneRuntime& operator=(const ModelSceneRuntime&) = delete;

    /// Initializes rendering resources for an empty editable scene.
    [[nodiscard]] bool init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice, RhiCommandListPool& commandListPool,
                            ImGuiRhiRenderer& imguiRenderer);
    void shutdown();

    /// Resizes the offscreen viewport and refreshes its ImGui texture binding.
    [[nodiscard]] bool ensureViewport(uint32_t width, uint32_t height);

    /// Renders all ECS mesh instances through the deferred viewport pipeline.
    [[nodiscard]] bool renderViewport(const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& cameraPosition, float nearPlane, float farPlane,
                                      float verticalFovDegrees, const RenderFrameClock& frameClock);

    [[nodiscard]] bool prepareGBuffer(RhiCommandList& commandList, const FrameContext& context) override;
    void renderToGBuffer(RhiCommandList& commandList, const glm::mat4& viewProjection,
                         const glm::mat4& previousViewProjection) override;
    void renderToShadowMap(RhiCommandList& commandList, const glm::mat4& shadowViewProjection) override;
    [[nodiscard]] bool prepareShadowFrame() override;
    [[nodiscard]] bool collectSceneLights(const glm::vec3& cameraPosition,
                                          std::vector<renderer::contracts::SceneLight>& lights,
                                          std::string& error) override;
    [[nodiscard]] bool queryLocalShadowSceneRevisions(DeferredLocalShadowSceneRevisions& revisions,
                                                      std::string& error) const override;
    [[nodiscard]] bool collectRayTracingInstances(std::vector<renderer::rt::SceneTlasInstanceInput>& instances,
                                                  std::string& error) const override;
    [[nodiscard]] renderer::rt::StaticMeshBlasAggregateStats staticBlasAggregateStats() const override;
    [[nodiscard]] bool configureClusteredLighting(const DeferredClusteredLightingResources& resources) override;
    [[nodiscard]] bool hasTransparentGeometry() const override;
    [[nodiscard]] bool prepareTransparentResources(const DeferredTransparentResources& resources) override;
    void renderTransparent(RhiCommandList& commandList, const glm::vec3& cameraPosition,
                           float reflectionCompositeStrength) override;
    [[nodiscard]] bool recordReflectionProbeRadianceOpaque(RhiCommandList& commandList, const FrameContext& context,
                                                           const ReflectionProbeCaptureWork& work) override;
    [[nodiscard]] bool recordReflectionProbeRadianceTransparent(RhiCommandList& commandList,
                                                                const FrameContext& context,
                                                                const ReflectionProbeCaptureWork& work) override;

    /// Imports or reuses one glTF asset and creates an independent ECS instance.
    /// @param path Filesystem path to a GLB or glTF document.
    /// @return Created scene entity, or entt::null when import fails.
    [[nodiscard]] entt::entity importModel(const std::string& path);

    /// Creates an empty transform entity that can own scene children.
    [[nodiscard]] entt::entity createEmptyEntity(const std::string& baseName);

    /// Creates another scene instance from an already loaded mesh asset.
    /// @param assetId Stable asset identifier returned by assetId().
    /// @return Created scene entity, or entt::null when the asset is unknown.
    [[nodiscard]] entt::entity createAssetInstance(scene::SceneAssetId assetId);

    /// Renames an entity while preserving globally unique scene names.
    /// @param entity Scene entity whose display name changes.
    /// @param requestedName Non-empty requested display name.
    /// @return True when the entity name was updated or already matched.
    [[nodiscard]] bool renameEntity(entt::entity entity, const std::string& requestedName);

    /// Duplicates an entity and its complete descendant hierarchy.
    /// Mesh assets remain shared while every duplicate receives a new stable ID.
    /// @return Root of the duplicated hierarchy, or entt::null on invalid input.
    [[nodiscard]] entt::entity duplicateEntity(entt::entity source);

    /// Captures one entity using stable document identifiers and local state.
    [[nodiscard]] bool captureEntityState(entt::entity entity, scene::SceneEntityDocument& state) const;

    /// Captures an entity and all descendants in parent-before-child order.
    [[nodiscard]] bool captureEntitySubtree(entt::entity root, std::vector<scene::SceneEntityDocument>& states) const;

    /// Applies an exact name, local transform, and parent from a stable snapshot.
    /// The mesh asset association must match the existing entity.
    [[nodiscard]] bool applyEntityState(const scene::SceneEntityDocument& state);

    /// Restores a previously removed hierarchy while preserving stable IDs.
    /// @return Root entity from the first snapshot, or entt::null on validation failure.
    [[nodiscard]] entt::entity restoreEntitySubtree(const std::vector<scene::SceneEntityDocument>& states);

    /// Removes all entities and loaded assets while keeping rendering initialized.
    void clearScene();

    /// Restores standalone time and renderer settings for a new scene.
    void resetEnvironment();

    /// Rebuilds the complete runtime scene transactionally from stable document data.
    /// @return True when every asset and entity was loaded and committed.
    [[nodiscard]] bool loadDocument(const scene::ModelSceneDocument& document);

    /// Reparents an entity while preserving its world-space transform.
    /// @param child Entity whose parent relationship changes.
    /// @param parent New parent, or entt::null to move the entity to the root.
    /// @return True when the hierarchy and local transform were updated.
    [[nodiscard]] bool setParent(entt::entity child, entt::entity parent);

    /// Applies a world-space transform and derives the corresponding local transform.
    [[nodiscard]] bool setWorldTransform(entt::entity entity, const glm::mat4& worldMatrix);

    /// Destroys one scene instance without affecting shared mesh assets.
    void destroyEntity(entt::entity entity);

    /// Selects the nearest pickable ECS entity intersected by a world-space ray.
    /// @param rayOrigin World-space ray origin.
    /// @param rayDirection Normalized world-space direction.
    /// @return Nearest intersected entity, or entt::null.
    [[nodiscard]] entt::entity pick(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;

    /// Computes world-space bounds for an entity pivot, meshes, and descendants.
    /// @return True when entity is a valid scene entity and bounds were produced.
    [[nodiscard]] bool entityWorldBounds(entt::entity entity, glm::vec3& boundsMin, glm::vec3& boundsMax) const;

    /// Rebuilds world and previous-world matrices from editable local transforms.
    void syncTransforms();

    /// Captures stable scene data without serializing runtime entity handles.
    [[nodiscard]] scene::ModelSceneDocument captureDocument(const scene::SceneEditorCameraDocument& editorCamera) const;

    [[nodiscard]] entt::registry& registry() { return m_registry; }
    [[nodiscard]] const entt::registry& registry() const { return m_registry; }
    [[nodiscard]] entt::entity selectedEntity() const { return m_selectedEntity; }
    void setSelectedEntity(entt::entity entity) { m_selectedEntity = entity; }
    [[nodiscard]] scene::SceneEntityId entityId(entt::entity entity) const;
    [[nodiscard]] entt::entity findEntity(scene::SceneEntityId id) const;
    [[nodiscard]] uint64_t viewportTextureId() const;
    [[nodiscard]] uint32_t viewportWidth() const;
    [[nodiscard]] uint32_t viewportHeight() const;
    [[nodiscard]] RhiTextureHandle captureTextureHandle() const;
    [[nodiscard]] RhiTextureFormat captureTextureFormat() const;
    [[nodiscard]] RhiTextureHandle rtgiRawDiffuseTextureHandle() const;
    [[nodiscard]] RhiTextureHandle nrdDiffuseTextureHandle() const;
    [[nodiscard]] RhiTextureHandle rtgiLeakageNormalTextureHandle() const;
    [[nodiscard]] RhiTextureHandle rtgiLeakageViewZTextureHandle() const;
    [[nodiscard]] float nrdDiffuseToPreExposedScale() const;
    [[nodiscard]] const GpuFrameStats* gpuFrameStats() const;
    [[nodiscard]] RenderGraphFrameStats renderGraphFrameStats() const;
    [[nodiscard]] ReflectionProbeCaptureFrameStats reflectionProbeCaptureStats() const;
    [[nodiscard]] bool isAccelerationStructureReady() const;
    /// Discards the just-rendered validation frame when scene resources were not ready.
    void discardValidationTemporalFrame();

    /// Adds one manually placed probe centered at the requested world position.
    /// @return Persistent scene ID, or zero when validation or allocation fails.
    [[nodiscard]] scene::SceneReflectionProbeId addReflectionProbe(const glm::vec3& position);

    /// Replaces one probe's spatial and exposure settings while preserving its ID.
    [[nodiscard]] bool updateReflectionProbe(const scene::SceneReflectionProbeDocument& probe);

    /// Removes one explicitly placed reflection probe.
    [[nodiscard]] bool removeReflectionProbe(scene::SceneReflectionProbeId id);

    /// Replaces all probes with a deterministic regular grid over scene mesh bounds.
    [[nodiscard]] bool generateReflectionProbeGrid(float spacingMeters, float boundsPaddingMeters);

    [[nodiscard]] std::size_t reflectionProbeCount() const { return m_reflectionProbes.size(); }
    [[nodiscard]] const scene::SceneReflectionProbeDocument& reflectionProbe(std::size_t index) const;

    /// Creates a Point-light scene entity at a world-space position.
    /// @return The selected entity, or entt::null when validation or allocation fails.
    [[nodiscard]] entt::entity createPointLight(const glm::vec3& position);

    /// Replaces one Point-light payload without changing its transform or stable GPU-light identity.
    [[nodiscard]] bool updatePointLight(entt::entity entity, const scene::SceneManualPointLightDocument& light);

    /// Updates the deferred environment time used by sky and lighting passes.
    /// @param timeOfDaySeconds Time within the 1200-second world day.
    void setTimeOfDay(float timeOfDaySeconds);

    /// Returns the current deferred environment time in seconds.
    [[nodiscard]] float timeOfDay() const;
    void setTimePaused(bool paused);
    [[nodiscard]] bool timePaused() const;
    void setTimeScale(float scale);
    [[nodiscard]] float timeScale() const;
    void setWeather(WeatherType weather, bool instant);
    [[nodiscard]] WeatherType weather() const;
    [[nodiscard]] bool weatherTransitionInstant() const;

    /// Replaces the model viewport renderer configuration.
    /// @param settings Complete deferred renderer configuration for subsequent frames.
    /// @return True when the settings are supported by standalone scene resources.
    [[nodiscard]] bool setRenderSettings(const RenderSettings& settings);

    /// Returns the active model viewport renderer configuration.
    [[nodiscard]] const RenderSettings& renderSettings() const;
    [[nodiscard]] bool isFsr1Supported() const;
    [[nodiscard]] bool isFsr31Supported() const;
    [[nodiscard]] size_t assetCount() const { return m_assets.size(); }
    [[nodiscard]] scene::SceneAssetId assetId(size_t index) const;
    [[nodiscard]] const std::string& assetName(size_t index) const;
    [[nodiscard]] const std::string& assetPath(size_t index) const;
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

private:
    struct MeshAsset {
        scene::SceneAssetId id = scene::kInvalidSceneAssetId;
        std::string name;
        std::string path;
        std::unique_ptr<StaticMeshRenderer> renderer;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };

    struct RuntimeReflectionProbe {
        scene::SceneReflectionProbeDocument document;
        renderer::contracts::StableReflectionProbeId stableId;
        uint32_t captureRevision = 1u;
    };

    [[nodiscard]] bool createMeshAsset(ResourceMgr& resourceMgr, scene::SceneAssetId assetId, const std::string& name,
                                       const std::string& path, MeshAsset& asset);
    [[nodiscard]] bool loadMeshAsset(ResourceMgr& resourceMgr, const std::string& name, const std::string& path,
                                     scene::SceneAssetId& assetId);
    [[nodiscard]] entt::entity instantiateAsset(scene::SceneAssetId assetId, const std::string& instanceName);
    [[nodiscard]] entt::entity createEntity(const std::string& baseName);
    [[nodiscard]] std::string makeUniqueInstanceName(const std::string& baseName,
                                                     entt::entity ignoredEntity = entt::null) const;
    [[nodiscard]] uint32_t assetIndex(scene::SceneAssetId id) const;
    [[nodiscard]] bool localTransformFromMatrix(const glm::mat4& matrix, ecs::LocalTransformComponent& transform) const;
    [[nodiscard]] bool configureReflectionProbeCapture();
    [[nodiscard]] bool sceneWorldBounds(glm::vec3& boundsMin, glm::vec3& boundsMax) const;
    [[nodiscard]] bool allocateReflectionProbeIdentities(std::vector<RuntimeReflectionProbe>& probes);
    [[nodiscard]] bool emplaceManualPointLight(entt::registry& registry, entt::entity entity,
                                               const scene::SceneManualPointLightDocument& light);
    void invalidateReflectionProbeCapture();
    void detachFromParent(entt::entity entity);
    void setError(std::string message);

    entt::registry m_registry;
    std::vector<MeshAsset> m_assets;
    std::unordered_map<scene::SceneAssetId, uint32_t> m_assetIndices;
    ResourceMgr* m_resourceMgr = nullptr;
    std::unique_ptr<ModelSceneDeferredRenderer> m_deferredRenderer;
    entt::entity m_selectedEntity = entt::null;
    scene::SceneEntityId m_nextEntityId = 1u;
    scene::SceneAssetId m_nextAssetId = 1u;
    scene::SceneReflectionProbeId m_nextReflectionProbeId = 1u;
    std::vector<RuntimeReflectionProbe> m_reflectionProbes;
    std::vector<renderer::contracts::SceneLight> m_reflectionProbeLights;
    uint64_t m_reflectionProbeSceneSignature = 0u;
    bool m_reflectionProbeSignatureValid = false;
    bool m_reflectionProbeRevisionInvalidated = false;
    std::string m_lastError;
};

#endif // MECRAFT_MODEL_SCENE_RUNTIME_H
