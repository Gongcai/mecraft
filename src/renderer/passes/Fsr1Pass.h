#ifndef MECRAFT_FSR1_PASS_H
#define MECRAFT_FSR1_PASS_H

#include "RenderPass.h"
#include "../rhi/RhiHandles.h"

#include <cstdint>
#include <glm/vec4.hpp>

class ResourceMgr;
class RhiDevice;
class RhiCommandList;
class RhiCommandListPool;
class RenderDebugService;

class Fsr1Pass : public RenderPass {
public:
    ~Fsr1Pass() override;

    void init(ResourceMgr& resourceMgr, RhiCommandListPool& commandListPool);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "FSR1"; }
    [[nodiscard]] static bool isSupported(const RhiDevice& rhiDevice);

    bool execute(RhiDevice& rhiDevice,
                 RhiTextureViewHandle swapchainColorView,
                 RhiTextureViewHandle inputView,
                 int inputWidth,
                 int inputHeight,
                 int outputWidth,
                 int outputHeight,
                 float sharpness,
                 RenderDebugService& debugService);

private:
    [[nodiscard]] RhiCommandList& beginCommandList(const char* debugName) const;
    void submitCommandList(RhiDevice& rhiDevice,
                           RhiCommandList& commandList,
                           const char* debugName) const;
    bool ensureTargets(RhiDevice& rhiDevice, int width, int height);
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureEasuBindGroup(RhiDevice& rhiDevice, RhiTextureViewHandle inputView);
    bool ensureRcasBindGroup(RhiDevice& rhiDevice);
    void destroyRhiBindGroups();
    void destroyRhiResources();
    void destroyTargets();

    static void populateEasuConstants(glm::vec4& con0,
                                      glm::vec4& con1,
                                      glm::vec4& con2,
                                      glm::vec4& con3,
                                      float inputViewportWidth,
                                      float inputViewportHeight,
                                      float inputTextureWidth,
                                      float inputTextureHeight,
                                      float outputWidth,
                                      float outputHeight);
    static glm::vec4 populateRcasConstants(float sharpness);

    RhiDevice* m_rhiDevice = nullptr;
    RhiCommandListPool* m_commandListPool = nullptr;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_easuFragmentShader;
    RhiShaderHandle m_rcasFragmentShader;
    RhiPipelineHandle m_easuPipeline;
    RhiPipelineHandle m_rcasPipeline;
    RhiBindGroupHandle m_easuBindGroup;
    RhiBindGroupHandle m_rcasBindGroup;
    RhiTextureViewHandle m_boundEasuInputView;
    RhiTextureViewHandle m_boundRcasInputView;
    RhiTextureHandle m_easuHandle;
    RhiTextureViewHandle m_easuView;
    int m_width = 0;
    int m_height = 0;
};

#endif // MECRAFT_FSR1_PASS_H
