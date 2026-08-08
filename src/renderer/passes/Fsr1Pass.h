#ifndef MECRAFT_FSR1_PASS_H
#define MECRAFT_FSR1_PASS_H

#include "RenderPass.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

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

    /// Allocates the owned shader-readable RCAS output for an offscreen consumer.
    [[nodiscard]] bool prepareTextureOutput(RhiDevice& rhiDevice, int width, int height);

    /// Executes EASU and RCAS into the supplied output view.
    /// @param terminalFrameGpuSpan Whether the RCAS pass writes the scene-frame terminal timestamp.
    bool execute(RhiDevice& rhiDevice, RhiTextureViewHandle swapchainColorView, RhiTextureHandle inputTexture,
                 RhiTextureViewHandle inputView, int inputWidth, int inputHeight, int outputWidth, int outputHeight,
                 float sharpness, RenderDebugService& debugService, bool terminalFrameGpuSpan);

    /// Executes EASU and RCAS into an owned shader-readable texture.
    /// @param terminalFrameGpuSpan Whether the RCAS pass writes the scene-frame terminal timestamp.
    [[nodiscard]] bool executeToTexture(RhiDevice& rhiDevice, RhiTextureHandle inputTexture,
                                        RhiTextureViewHandle inputView, int inputWidth, int inputHeight,
                                        int outputWidth, int outputHeight, float sharpness,
                                        RenderDebugService& debugService, bool terminalFrameGpuSpan);
    [[nodiscard]] RhiTextureHandle outputTextureHandle() const { return m_outputHandle; }
    [[nodiscard]] RhiTextureViewHandle outputTextureViewHandle() const { return m_outputView; }

private:
    [[nodiscard]] RhiCommandList& beginCommandList(const char* debugName) const;
    void submitCommandList(RhiDevice& rhiDevice, RhiCommandList& commandList, const char* debugName) const;
    bool ensureTargets(RhiDevice& rhiDevice, int width, int height);
    bool ensureOutputTarget(RhiDevice& rhiDevice, int width, int height);
    bool executeToOutput(RhiDevice& rhiDevice, RhiTextureHandle outputTexture, RhiTextureViewHandle outputView,
                         RhiResourceState outputStableState, RhiLoadOp outputLoadOp, RhiTextureHandle inputTexture,
                         RhiTextureViewHandle inputView, int inputWidth, int inputHeight, int outputWidth,
                         int outputHeight, float sharpness, RenderDebugService& debugService,
                         bool terminalFrameGpuSpan);
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureEasuBindGroup(RhiDevice& rhiDevice, RhiTextureViewHandle inputView);
    bool ensureRcasBindGroup(RhiDevice& rhiDevice);
    void destroyRhiBindGroups();
    void destroyRhiResources();
    void destroyTargets();

    static void populateEasuConstants(glm::vec4& con0, glm::vec4& con1, glm::vec4& con2, glm::vec4& con3,
                                      float inputViewportWidth, float inputViewportHeight, float inputTextureWidth,
                                      float inputTextureHeight, float outputWidth, float outputHeight);
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
    RhiTextureHandle m_outputHandle;
    RhiTextureViewHandle m_outputView;
    int m_outputWidth = 0;
    int m_outputHeight = 0;
    int m_width = 0;
    int m_height = 0;
    RenderGraph m_renderGraph;
};

#endif // MECRAFT_FSR1_PASS_H
