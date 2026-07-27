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

    /// Uploads current per-instance lighting before the G-buffer rendering scope starts.
    /// @param commandList Graphics command list recording the current G-buffer pass.
    /// @return True when frame data was prepared and the upload was recorded.
    [[nodiscard]] bool prepareGBuffer(RhiCommandList& commandList) const;

    /// Records all static primitives into the active G-buffer rendering scope.
    /// @param commandList Command list with compatible G-buffer attachments active.
    /// @param viewProj Current raster view-projection matrix.
    /// @param previousViewProj Previous view-projection carrying current temporal jitter.
    void renderToGBuffer(RhiCommandList& commandList,
                         const glm::mat4& viewProj,
                         const glm::mat4& previousViewProj) const;

    /// Records all static primitives into the active shadow depth rendering scope.
    /// @param commandList Command list with one CSM cascade active.
    /// @param shadowViewProj Current cascade view-projection matrix.
    void renderToShadowMap(RhiCommandList& commandList,
                           const glm::mat4& shadowViewProj) const;

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
    RhiShaderHandle m_gbufferVertexShader;
    RhiShaderHandle m_gbufferFragmentShader;
    RhiShaderHandle m_shadowVertexShader;
    RhiShaderHandle m_shadowFragmentShader;
    RhiPipelineHandle m_gbufferPipeline;
    RhiPipelineHandle m_gbufferDoubleSidedPipeline;
    RhiPipelineHandle m_shadowPipeline;
    RhiPipelineHandle m_shadowDoubleSidedPipeline;
    glm::vec3 m_assetBoundsMin{0.0f};
    glm::vec3 m_assetBoundsMax{0.0f};
    glm::mat4 m_modelMatrix{1.0f};
    glm::vec4 m_voxelLight{1.0f, 0.0f, 0.0f, 1.0f};
    bool m_instancePlaced = false;
    bool m_framePrepared = false;
    std::string m_lastError;
};

#endif // MECRAFT_STATIC_MESH_RENDERER_H
