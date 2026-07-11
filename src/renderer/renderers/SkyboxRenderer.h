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
    RhiDevice* m_rhiDevice = nullptr;
    uint32_t m_cubeVao = 0;
    uint32_t m_cubeVbo = 0;
    uint32_t m_fullscreenVao = 0;

    // Blur pipeline: scene texture + two ping-pong textures at half resolution
    uint32_t m_sceneColorTex = 0;
    RhiTextureHandle m_sceneColorHandle;
    RhiTextureViewHandle m_sceneColorView;
    uint32_t m_pingColorTex = 0;
    RhiTextureHandle m_pingColorHandle;
    RhiTextureViewHandle m_pingColorView;
    uint32_t m_pongColorTex = 0;
    RhiTextureHandle m_pongColorHandle;
    RhiTextureViewHandle m_pongColorView;
    RhiDevice* m_blurViewDevice = nullptr;
    int m_blurWidth = 0;
    int m_blurHeight = 0;
};

#endif // MECRAFT_SKYBOXRENDERER_H
