#ifndef MECRAFT_SKYBOXRENDERER_H
#define MECRAFT_SKYBOXRENDERER_H

#include <cstdint>

#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

class ResourceMgr;
class RhiDevice;
class RhiCommandListPool;

class SkyboxRenderer {
public:
    void init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice);
    void shutdown();
    void render(int width, int height, float aspect, float yawDegrees,
                float pitchDegrees, RhiDevice& rhiDevice);

private:
    void initCubeMesh();
    void destroyCubeMesh();
    bool ensureBlurTargets(RhiDevice& rhiDevice, int width, int height);
    void destroyBlurTargets();

    RhiTextureHandle m_cubemapTexture;
    RhiTextureViewHandle m_cubemapView;
    RhiSamplerHandle m_cubemapSampler;
    RhiBufferHandle m_cubeVertexBuffer;
    RhiShaderHandle m_skyboxVertexShader;
    RhiShaderHandle m_skyboxFragmentShader;
    RhiBindGroupLayoutHandle m_skyboxBindGroupLayout;
    RhiPipelineLayoutHandle m_skyboxPipelineLayout;
    RhiPipelineHandle m_skyboxPipeline;
    RhiBindGroupHandle m_skyboxBindGroup;
    RhiSamplerHandle m_blurSampler;
    RhiShaderHandle m_blurVertexShader;
    RhiShaderHandle m_blurFragmentShader;
    RhiBindGroupLayoutHandle m_blurBindGroupLayout;
    RhiPipelineLayoutHandle m_blurPipelineLayout;
    RhiPipelineHandle m_blurPipeline;
    RhiBindGroupHandle m_sceneBlurBindGroup;
    RhiBindGroupHandle m_pingBlurBindGroup;
    RhiDevice* m_rhiDevice = nullptr;
    RhiCommandListPool* m_commandListPool = nullptr;

    // Blur pipeline: scene texture + two ping-pong textures at half resolution
    RhiTextureHandle m_sceneColorHandle;
    RhiTextureViewHandle m_sceneColorView;
    RhiTextureHandle m_pingColorHandle;
    RhiTextureViewHandle m_pingColorView;
    RhiTextureHandle m_pongColorHandle;
    RhiTextureViewHandle m_pongColorView;
    int m_blurWidth = 0;
    int m_blurHeight = 0;
    bool m_blurTargetsInitialized = false;
    RenderGraph m_renderGraph;
};

#endif // MECRAFT_SKYBOXRENDERER_H
