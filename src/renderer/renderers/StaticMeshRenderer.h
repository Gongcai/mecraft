#ifndef MECRAFT_STATIC_MESH_RENDERER_H
#define MECRAFT_STATIC_MESH_RENDERER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../rhi/RhiHandles.h"
#include "../contracts/ClusteredLightingContract.h"
#include "../contracts/GpuLightContract.h"
#include "../contracts/SceneIdentityContract.h"

class FrameContext;
class IWorldView;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;

/// Loads and renders one static glTF 2.0 PBR showcase asset.
class StaticMeshRenderer {
public:
    struct TransparentDraw {
        size_t primitiveIndex = 0u;
        glm::mat4 model{1.0f};
        float distanceSquared = 0.0f;
    };

    /// Creates the asset's CPU and GPU resources.
    /// @param resourceMgr Provides the active RHI device and command pool.
    /// @param modelPath Filesystem path to a glTF 2.0 GLB or JSON document.
    /// @return True when the complete asset and all graphics pipelines are ready.
    [[nodiscard]] bool init(ResourceMgr& resourceMgr, const std::string& modelPath);

    /// Releases every GPU object owned by the renderer.
    void shutdown();

    /// Fixes the showcase instance in front of the first camera and samples its world light.
    /// @param ctx Current camera matrices and position.
    /// @param worldView Read-only source for block and sky light at the instance center.
    void prepareFrame(const FrameContext& ctx, const IWorldView& worldView);

    /// Supplies an editor-owned instance transform without requiring a world.
    /// @param model Current local-to-world matrix.
    /// @param previousModel Local-to-world matrix used by temporal velocity.
    void setInstanceTransform(const glm::mat4& model, const glm::mat4& previousModel);

    /// Sets the stable object identity written by subsequent G-buffer draws.
    /// @param objectId Non-zero identity owned by the visible scene instance.
    /// @return True when the supplied identity is valid and was accepted.
    [[nodiscard]] bool setStableObjectId(renderer::contracts::StableObjectId objectId);

    /// Prepares constant neutral lighting for standalone scene preview rendering.
    void prepareStandaloneFrame();

    /// Returns the asset-space axis-aligned bounds generated while decoding glTF vertices.
    void assetBounds(glm::vec3& minimum, glm::vec3& maximum) const;

    /// Returns the number of KHR_lights_punctual instances in the default scene.
    /// @return Count of validated asset-local punctual light definitions.
    [[nodiscard]] std::size_t punctualLightCount() const { return m_punctualLights.size(); }

    /// Appends this asset instance's camera-relative punctual lights.
    /// @param model Local-to-world transform shared with the rendered instance.
    /// @param cameraPosition World-space camera position used as the floating origin.
    /// @param lightIds Stable identities in asset-local light order.
    /// @param lights Destination receiving a complete light batch on success.
    /// @param error Receives a precise transform or physical-contract failure.
    /// @return True when every asset light was instantiated without partial output.
    [[nodiscard]] bool appendPunctualLights(const glm::mat4& model, const glm::vec3& cameraPosition,
                                            const std::vector<renderer::contracts::StableLightId>& lightIds,
                                            std::vector<renderer::contracts::SceneLight>& lights,
                                            std::string& error) const;

    /// Uploads lighting and camera transforms before the G-buffer rendering scope starts.
    /// @param commandList Graphics command list recording the current G-buffer pass.
    /// @param viewProjection Current raster view-projection matrix.
    /// @param previousViewProjection Previous view-projection used for velocity.
    /// @param context Current camera, fog, and celestial lighting state.
    /// @return True when frame data was prepared and the upload was recorded.
    [[nodiscard]] bool prepareGBuffer(RhiCommandList& commandList, const glm::mat4& viewProjection,
                                      const glm::mat4& previousViewProjection, const FrameContext& context) const;

    /// Records all static primitives into the active G-buffer rendering scope.
    /// @param commandList Command list with compatible G-buffer attachments active.
    void renderToGBuffer(RhiCommandList& commandList) const;

    /// Appends alpha-blended primitives with camera distances for global sorting.
    /// @param model Local-to-world transform for the rendered instance.
    /// @param cameraPosition World-space camera position used for distance sorting.
    /// @param draws Destination receiving one entry per transparent primitive.
    void appendTransparentDraws(const glm::mat4& model, const glm::vec3& cameraPosition,
                                std::vector<TransparentDraw>& draws) const;

    /// Appends forward optical primitives for the currently prepared world instance.
    /// @param cameraPosition World-space camera or probe position used for distance sorting.
    /// @param draws Destination receiving one entry per transparent primitive.
    void appendPreparedTransparentDraws(const glm::vec3& cameraPosition, std::vector<TransparentDraw>& draws) const;

    /// Draws one previously collected transparent primitive.
    /// @param commandList Command list with the transparent attachments active.
    /// @param draw Primitive, transform, and sort data collected for this draw.
    /// @param reflectionCompositeStrength Global scene reflection contribution.
    void renderTransparentDraw(RhiCommandList& commandList, const TransparentDraw& draw,
                               float reflectionCompositeStrength) const;

    /// Reports whether a loaded primitive uses alpha blending.
    /// @return True when the asset contains renderable transparent geometry.
    [[nodiscard]] bool hasTransparentPrimitives() const;

    /// Creates the descriptors used to reflect the resolved deferred scene.
    /// @param sceneColor Opaque HDR scene after deferred composition.
    /// @param opaqueDepth Opaque depth used by screen-space ray traversal.
    /// @param skyCapture Dynamic sky radiance used by environment reflection.
    /// @return True when the transparent scene bind group is valid.
    [[nodiscard]] bool prepareTransparentResources(RhiTextureViewHandle sceneColor, RhiTextureViewHandle opaqueDepth,
                                                   RhiTextureViewHandle skyCapture);

    /// Publishes the current Forward+ descriptor set and cluster lattice.
    /// @param bindGroupLayout Exact shared layout used by the descriptor set.
    /// @param bindGroup Four-buffer clustered-light descriptor set.
    /// @param grid Current view-space 16x16x24 cluster lattice.
    /// @return True when both resources satisfy the Vulkan transparent path.
    [[nodiscard]] bool configureClusteredLighting(RhiBindGroupLayoutHandle bindGroupLayout,
                                                  RhiBindGroupHandle bindGroup,
                                                  const renderer::contracts::ClusterGrid& grid);

    /// Records all static primitives into the active shadow depth rendering scope.
    /// @param commandList Command list with one CSM cascade active.
    /// @param shadowViewProj Current cascade view-projection matrix.
    void renderToShadowMap(RhiCommandList& commandList, const glm::mat4& shadowViewProj) const;

    /// Records the asset into an active RGBA8/depth editor viewport pass.
    /// @param commandList Command list with compatible preview attachments active.
    /// @param viewProj Current editor camera view-projection matrix.
    void renderPreview(RhiCommandList& commandList, const glm::mat4& viewProj) const;

    /// Ensures the probe-capture light buffer can hold one complete scene snapshot.
    /// @param lightCount Number of normalized analytic lights captured per face.
    /// @return True when the dedicated capture descriptor set is ready.
    [[nodiscard]] bool ensureReflectionProbeCaptureLightCapacity(uint32_t lightCount);

    /// Uploads view-independent HDR probe-capture frame and light data.
    /// @param commandList Graphics command list recording the capture face.
    /// @param viewProjection Probe-face view-projection matrix.
    /// @param probePosition World-space probe position used as the light origin.
    /// @param context Main-frame environment state supplying sun and ambient energy.
    /// @param lights Probe-relative normalized analytic light snapshot.
    /// @return True when all dedicated capture resources were updated.
    [[nodiscard]] bool prepareReflectionProbeCapture(RhiCommandList& commandList, const glm::mat4& viewProjection,
                                                     const glm::vec3& probePosition, const FrameContext& context,
                                                     const std::vector<renderer::contracts::SceneLight>& lights);

    /// Draws opaque and alpha-tested primitives into an active HDR probe face.
    /// @param commandList Command list with RGBA16F and depth attachments active.
    void renderReflectionProbeCaptureOpaque(RhiCommandList& commandList) const;

    /// Draws one globally sorted optical primitive into an active HDR probe face.
    /// @param commandList Command list with RGBA16F and depth attachments active.
    /// @param draw Primitive and world transform collected for probe sorting.
    void renderReflectionProbeCaptureTransparent(RhiCommandList& commandList, const TransparentDraw& draw) const;

    /// Returns the precise initialization failure reported by the asset pipeline.
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

private:
    struct TextureResource {
        RhiTextureHandle texture;
        RhiTextureViewHandle view;
        uint32_t mipLevels = 1u;
    };

    struct MaterialResource {
        RhiBufferHandle uniformBuffer;
        RhiBindGroupHandle bindGroup;
        renderer::contracts::StableMaterialId materialId;
        bool doubleSided = false;
        bool alphaBlended = false;
        bool transmissive = false;
        bool forwardOpticalLayer = false;
    };

    struct PrimitiveResource {
        RhiBufferHandle vertexBuffer;
        RhiBufferHandle indexBuffer;
        uint32_t indexCount = 0u;
        uint32_t materialIndex = 0u;
        glm::vec3 boundsCenter{0.0f};
    };

    [[nodiscard]] bool createPipelineResources();
    [[nodiscard]] bool rebuildReflectionProbeCaptureBindGroup();
    [[nodiscard]] bool ensureTransparentPipelines(RhiBindGroupLayoutHandle clusteredLightingLayout);
    [[nodiscard]] bool loadAsset(const std::string& modelPath, ResourceMgr& resourceMgr);
    void destroyTransparentPipelines();
    void destroyPipelineResources();
    void setError(std::string message);

    RhiDevice* m_rhiDevice = nullptr;
    std::vector<TextureResource> m_textures;
    std::vector<RhiSamplerHandle> m_samplers;
    std::vector<MaterialResource> m_materials;
    std::vector<PrimitiveResource> m_primitives;
    std::vector<renderer::contracts::AnalyticLightSourceDefinition> m_punctualLights;
    RhiBufferHandle m_frameUniformBuffer;
    RhiBufferHandle m_probeCaptureFrameUniformBuffer;
    RhiBufferHandle m_probeCaptureLightBuffer;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiBindGroupLayoutHandle m_transparentSceneBindGroupLayout;
    RhiBindGroupLayoutHandle m_transparentClusterBindGroupLayout;
    RhiBindGroupLayoutHandle m_probeCaptureBindGroupLayout;
    RhiBindGroupHandle m_transparentSceneBindGroup;
    RhiBindGroupHandle m_transparentClusterBindGroup;
    RhiBindGroupHandle m_probeCaptureBindGroup;
    RhiSamplerHandle m_transparentSceneLinearSampler;
    RhiSamplerHandle m_transparentSceneDepthSampler;
    std::array<RhiTextureViewHandle, 3> m_transparentSceneViews{};
    renderer::contracts::ClusterGrid m_transparentClusterGrid;
    RhiPipelineLayoutHandle m_gbufferPipelineLayout;
    RhiPipelineLayoutHandle m_shadowPipelineLayout;
    RhiPipelineLayoutHandle m_previewPipelineLayout;
    RhiPipelineLayoutHandle m_transparentPipelineLayout;
    RhiPipelineLayoutHandle m_probeCapturePipelineLayout;
    RhiShaderHandle m_gbufferVertexShader;
    RhiShaderHandle m_gbufferFragmentShader;
    RhiShaderHandle m_shadowVertexShader;
    RhiShaderHandle m_shadowFragmentShader;
    RhiShaderHandle m_previewVertexShader;
    RhiShaderHandle m_previewFragmentShader;
    RhiShaderHandle m_transparentVertexShader;
    RhiShaderHandle m_transparentFragmentShader;
    RhiShaderHandle m_probeCaptureVertexShader;
    RhiShaderHandle m_probeCaptureFragmentShader;
    RhiPipelineHandle m_gbufferPipeline;
    RhiPipelineHandle m_gbufferDoubleSidedPipeline;
    RhiPipelineHandle m_shadowPipeline;
    RhiPipelineHandle m_shadowDoubleSidedPipeline;
    RhiPipelineHandle m_previewPipeline;
    RhiPipelineHandle m_previewDoubleSidedPipeline;
    RhiPipelineHandle m_previewTransparentPipeline;
    RhiPipelineHandle m_previewTransparentDoubleSidedPipeline;
    RhiPipelineHandle m_transparentPipeline;
    RhiPipelineHandle m_transparentDoubleSidedPipeline;
    RhiPipelineHandle m_probeCapturePipeline;
    RhiPipelineHandle m_probeCaptureDoubleSidedPipeline;
    RhiPipelineHandle m_probeCaptureTransparentPipeline;
    RhiPipelineHandle m_probeCaptureTransparentDoubleSidedPipeline;
    glm::vec3 m_assetBoundsMin{0.0f};
    glm::vec3 m_assetBoundsMax{0.0f};
    glm::mat4 m_modelMatrix{1.0f};
    glm::mat4 m_previousModelMatrix{1.0f};
    glm::vec4 m_voxelLight{1.0f, 0.0f, 0.0f, 1.0f};
    renderer::contracts::StableObjectId m_objectId;
    bool m_instancePlaced = false;
    bool m_framePrepared = false;
    uint32_t m_probeCaptureLightCapacity = 0u;
    std::string m_lastError;
};

#endif // MECRAFT_STATIC_MESH_RENDERER_H
