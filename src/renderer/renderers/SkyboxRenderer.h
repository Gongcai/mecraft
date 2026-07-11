#ifndef MECRAFT_SKYBOXRENDERER_H
#define MECRAFT_SKYBOXRENDERER_H

#include <cstdint>

#include "../rhi/RhiHandles.h"

class ResourceMgr;
class RhiDevice;
class Shader;

class SkyboxRenderer {
public:
    void init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice);
    void shutdown();
    void render(float aspect, float yawDegrees, float pitchDegrees, RhiDevice& rhiDevice);

private:
    void initCubeMesh();
    void destroyCubeMesh();
    bool ensureBlurTargets(RhiDevice& rhiDevice, int width, int height);
    void destroyBlurTargets();

    Shader* m_shader = nullptr;
    Shader* m_blurShader = nullptr;
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
    RhiDevice* m_rhiDevice = nullptr;
    uint32_t m_cubeVao = 0;
    uint32_t m_cubeVbo = 0;
    uint32_t m_fullscreenVao = 0;

    // Blur pipeline: scene texture + two ping-pong textures at half resolution
    RhiTextureHandle m_sceneColorHandle;
    RhiTextureViewHandle m_sceneColorView;
    RhiTextureHandle m_pingColorHandle;
    RhiTextureViewHandle m_pingColorView;
    RhiTextureHandle m_pongColorHandle;
    RhiTextureViewHandle m_pongColorView;
    int m_blurWidth = 0;
    int m_blurHeight = 0;
};

#endif // MECRAFT_SKYBOXRENDERER_H
