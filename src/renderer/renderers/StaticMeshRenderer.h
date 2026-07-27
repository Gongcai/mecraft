#ifndef MECRAFT_STATIC_MESH_RENDERER_H
#define MECRAFT_STATIC_MESH_RENDERER_H

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../rhi/RhiHandles.h"

class FrameContext;
class IWorldView;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;

/// Loads and renders one static glTF 2.0 metallic-roughness showcase asset.
class StaticMeshRenderer {
public:
    /// Creates the asset's CPU and GPU resources.
    /// @param resourceMgr Provides the active RHI device and command pool.
    /// @param modelPath Filesystem path to a glTF 2.0 GLB or JSON document.
    /// @return True when the complete asset and both graphics pipelines are ready.
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
    void setInstanceTransform(const glm::mat4& model,
                              const glm::mat4& previousModel);

    /// Prepares constant neutral lighting for standalone scene preview rendering.
    void prepareStandaloneFrame();

    /// Returns the asset-space axis-aligned bounds generated while decoding glTF vertices.
    void assetBounds(glm::vec3& minimum, glm::vec3& maximum) const;

    /// Uploads lighting and camera transforms before the G-buffer rendering scope starts.
    /// @param commandList Graphics command list recording the current G-buffer pass.
    /// @param viewProjection Current raster view-projection matrix.
    /// @param previousViewProjection Previous view-projection used for velocity.
    /// @return True when frame data was prepared and the upload was recorded.
    [[nodiscard]] bool prepareGBuffer(
        RhiCommandList& commandList,
        const glm::mat4& viewProjection,
        const glm::mat4& previousViewProjection) const;

    /// Records all static primitives into the active G-buffer rendering scope.
    /// @param commandList Command list with compatible G-buffer attachments active.
    void renderToGBuffer(RhiCommandList& commandList) const;

    /// Records all static primitives into the active shadow depth rendering scope.
    /// @param commandList Command list with one CSM cascade active.
    /// @param shadowViewProj Current cascade view-projection matrix.
    void renderToShadowMap(RhiCommandList& commandList,
                           const glm::mat4& shadowViewProj) const;

    /// Records the asset into an active RGBA8/depth editor viewport pass.
    /// @param commandList Command list with compatible preview attachments active.
    /// @param viewProj Current editor camera view-projection matrix.
    void renderPreview(RhiCommandList& commandList,
                       const glm::mat4& viewProj) const;

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
        bool doubleSided = false;
    };

    struct PrimitiveResource {
        RhiBufferHandle vertexBuffer;
        RhiBufferHandle indexBuffer;
        uint32_t indexCount = 0u;
        uint32_t materialIndex = 0u;
    };

    [[nodiscard]] bool createPipelineResources();
    [[nodiscard]] bool loadAsset(const std::string& modelPath, ResourceMgr& resourceMgr);
    void destroyPipelineResources();
    void setError(std::string message);

    RhiDevice* m_rhiDevice = nullptr;
    std::vector<TextureResource> m_textures;
    std::vector<RhiSamplerHandle> m_samplers;
    std::vector<MaterialResource> m_materials;
    std::vector<PrimitiveResource> m_primitives;
    RhiBufferHandle m_frameUniformBuffer;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_gbufferPipelineLayout;
    RhiPipelineLayoutHandle m_shadowPipelineLayout;
    RhiPipelineLayoutHandle m_previewPipelineLayout;
    RhiShaderHandle m_gbufferVertexShader;
    RhiShaderHandle m_gbufferFragmentShader;
    RhiShaderHandle m_shadowVertexShader;
    RhiShaderHandle m_shadowFragmentShader;
    RhiShaderHandle m_previewVertexShader;
    RhiShaderHandle m_previewFragmentShader;
    RhiPipelineHandle m_gbufferPipeline;
    RhiPipelineHandle m_gbufferDoubleSidedPipeline;
    RhiPipelineHandle m_shadowPipeline;
    RhiPipelineHandle m_shadowDoubleSidedPipeline;
    RhiPipelineHandle m_previewPipeline;
    RhiPipelineHandle m_previewDoubleSidedPipeline;
    glm::vec3 m_assetBoundsMin{0.0f};
    glm::vec3 m_assetBoundsMax{0.0f};
    glm::mat4 m_modelMatrix{1.0f};
    glm::mat4 m_previousModelMatrix{1.0f};
    glm::vec4 m_voxelLight{1.0f, 0.0f, 0.0f, 1.0f};
    bool m_instancePlaced = false;
    bool m_framePrepared = false;
    std::string m_lastError;
};

#endif // MECRAFT_STATIC_MESH_RENDERER_H
