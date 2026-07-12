#include "ReflectionPass.h"
#include "../core/RenderScene.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <size_t Count>
[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, Count>& lhs,
                                    const std::array<RhiTextureViewHandle, Count>& rhs) {
    for (size_t i = 0u; i < lhs.size(); ++i) {
        if (!sameTextureView(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

struct alignas(16) ReflectionBaseParams {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 cameraPosNear;
    glm::vec4 farSurfaceTime;
    glm::ivec4 controls;
};
static_assert(sizeof(ReflectionBaseParams) == 176u);
} // namespace

void ReflectionPass::init(ResourceMgr&) {}

void ReflectionPass::shutdown() {
    destroyBaseRhiResources();
    destroyFilterRhiResources();
    destroyTemporalRhiResources();
}

void ReflectionPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    renderReflection(ctx, settings, targets);

    if (settings.reflection.filterEnabled &&
        settings.debug.reflectionDebugMode == 0) {
        renderFilter(ctx, settings.reflection, targets);
    }
    if (settings.reflection.temporalEnabled &&
        settings.debug.reflectionDebugMode == 0 &&
        ctx.hasPreviousFrame) {
        renderTemporal(ctx, settings.reflection, targets);
    }
}

void ReflectionPass::renderReflection(const FrameContext& ctx, const RenderSettings& settings,
                                       DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureReflectionTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSceneLightingTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 7> views = {
        targets.sceneLightingTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.materialTextureViewHandle(),
        targets.materialAuxTextureViewHandle(),
        targets.skyCaptureTextureViewHandle(),
        targets.voxelLightTextureViewHandle()
    };
    if (!ensureBaseRhiPipeline(rhiDevice) || !ensureBaseBindGroup(rhiDevice, views)) {
        return;
    }

    ReflectionBaseParams params{};
    params.viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    params.invViewProj = settings.taa.enabled
        ? ctx.camera.jitteredInvViewProj
        : ctx.camera.invViewProj;
    params.cameraPosNear = glm::vec4(ctx.camera.position, ctx.camera.nearPlane);
    params.farSurfaceTime = glm::vec4(ctx.camera.farPlane,
                                      ctx.weather.surfaceWetness,
                                      ctx.shaderTime,
                                      0.0f);
    params.controls = glm::ivec4(settings.debug.reflectionDebugMode,
                                 settings.weather.rainLinesEnabled ? 1 : 0,
                                 0,
                                 0);

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.reflectionTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Reflection";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    targets.transitionTexture(commandList, targets.sceneLightingTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.depthTextureHandle(),
                              RhiResourceState::DepthRead);
    targets.transitionTexture(commandList, targets.normalAoTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialAuxTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.skyCaptureTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.voxelLightTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.reflectionTextureHandle(),
                              RhiResourceState::RenderTarget);
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Reflection)
        : GpuTimerSegmentToken{};
    commandList.bufferBarrier({m_baseUniformBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_baseUniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_baseUniformBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_basePipeline);
    commandList.setBindGroup(0u, m_baseBindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList, targets.reflectionTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
}

bool ReflectionPass::ensureBaseRhiPipeline(RhiDevice& rhiDevice) {
    if (m_baseRhiDevice != nullptr && m_baseRhiDevice != &rhiDevice) {
        destroyBaseRhiResources();
    }
    if (m_basePipeline.isValid()) {
        return true;
    }
    m_baseRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/reflection_probe.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "ReflectionBase.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_baseVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "ReflectionBase.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_baseFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_baseVertexShader.isValid() || !m_baseFragmentShader.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "ReflectionBase.Params";
    uniformBufferDesc.size = sizeof(ReflectionBaseParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    m_baseUniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_baseUniformBuffer.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = filter;
        samplerDesc.magFilter = filter;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = RhiAddressMode::ClampToEdge;
        samplerDesc.addressV = RhiAddressMode::ClampToEdge;
        samplerDesc.addressW = RhiAddressMode::ClampToEdge;
        return rhiDevice.createSampler(samplerDesc);
    };
    m_baseNearestSampler = createSampler(RhiFilter::Nearest);
    m_baseLinearSampler = createSampler(RhiFilter::Linear);
    if (!m_baseNearestSampler.isValid() || !m_baseLinearSampler.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "ReflectionBase.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 7u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    bindGroupLayoutDesc.entries.push_back({
        7u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_baseBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_baseBindGroupLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "ReflectionBase.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_baseBindGroupLayout);
    m_basePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_basePipelineLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "ReflectionBase.Pipeline";
    pipelineDesc.vertexShader = m_baseVertexShader;
    pipelineDesc.fragmentShader = m_baseFragmentShader;
    pipelineDesc.layout = m_basePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_basePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_basePipeline.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    return true;
}

bool ReflectionPass::ensureBaseBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 7>& views) {
    if (!ensureBaseRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_baseBindGroup.isValid() && sameTextureViews(m_baseBoundViews, views)) {
        return true;
    }

    destroyBaseBindGroup();
    const RhiSamplerHandle samplers[7] = {
        m_baseLinearSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseLinearSampler,
        m_baseNearestSampler
    };
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_baseBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 7u;
    uniformEntry.resource.buffer.buffer = m_baseUniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(ReflectionBaseParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_baseBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_baseBindGroup.isValid()) {
        m_baseBoundViews = {};
        return false;
    }

    m_baseBoundViews = views;
    return true;
}

void ReflectionPass::destroyBaseBindGroup() {
    if (m_baseRhiDevice != nullptr && m_baseBindGroup.isValid()) {
        m_baseRhiDevice->destroyBindGroup(m_baseBindGroup);
    }
    m_baseBindGroup = {};
    m_baseBoundViews = {};
}

void ReflectionPass::destroyBaseRhiResources() {
    destroyBaseBindGroup();
    if (m_baseRhiDevice != nullptr) {
        if (m_basePipeline.isValid()) {
            m_baseRhiDevice->destroyPipeline(m_basePipeline);
        }
        if (m_baseVertexShader.isValid()) {
            m_baseRhiDevice->destroyShader(m_baseVertexShader);
        }
        if (m_baseFragmentShader.isValid()) {
            m_baseRhiDevice->destroyShader(m_baseFragmentShader);
        }
        if (m_basePipelineLayout.isValid()) {
            m_baseRhiDevice->destroyPipelineLayout(m_basePipelineLayout);
        }
        if (m_baseBindGroupLayout.isValid()) {
            m_baseRhiDevice->destroyBindGroupLayout(m_baseBindGroupLayout);
        }
        if (m_baseUniformBuffer.isValid()) {
            m_baseRhiDevice->destroyBuffer(m_baseUniformBuffer);
        }
        if (m_baseNearestSampler.isValid()) {
            m_baseRhiDevice->destroySampler(m_baseNearestSampler);
        }
        if (m_baseLinearSampler.isValid()) {
            m_baseRhiDevice->destroySampler(m_baseLinearSampler);
        }
    }

    m_baseUniformBuffer = {};
    m_baseNearestSampler = {};
    m_baseLinearSampler = {};
    m_baseBindGroupLayout = {};
    m_basePipelineLayout = {};
    m_baseVertexShader = {};
    m_baseFragmentShader = {};
    m_basePipeline = {};
    m_baseRhiDevice = nullptr;
}

void ReflectionPass::renderFilter(const FrameContext& ctx, const ReflectionSettings& reflection,
                                   DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureReflectionTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureReflectionTemporalScratchTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.reflectionTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ReflectionFilter";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList* copyCommandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (copyCommandListStorage == nullptr ||
        !copyCommandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& copyCommandList = *copyCommandListStorage;
    const GpuTimerSegmentToken copyTimerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(copyCommandList, GpuTimerPass::Reflection)
        : GpuTimerSegmentToken{};
    targets.transitionTexture(copyCommandList, targets.reflectionTextureHandle(),
                              RhiResourceState::TransferSrc);
    targets.transitionTexture(copyCommandList,
                              targets.reflectionTemporalScratchTextureHandle(),
                              RhiResourceState::TransferDst);
    targets.copyReflectionToTemporalScratch(copyCommandList);
    targets.transitionTexture(copyCommandList, targets.reflectionTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(copyCommandList,
                              targets.reflectionTemporalScratchTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(copyCommandList, copyTimerToken);
    }
    if (!copyCommandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&copyCommandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
    const std::array<RhiTextureViewHandle, 5> views = {
        targets.reflectionTemporalScratchTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.materialTextureViewHandle(),
        targets.materialAuxTextureViewHandle()
    };
    if (!ensureFilterRhiPipeline(rhiDevice) ||
        !ensureFilterBindGroup(rhiDevice, views)) {
        return;
    }

    RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    targets.transitionTexture(commandList,
                              targets.reflectionTemporalScratchTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.depthTextureHandle(),
                              RhiResourceState::DepthRead);
    targets.transitionTexture(commandList, targets.normalAoTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialAuxTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.reflectionTextureHandle(),
                              RhiResourceState::RenderTarget);
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Reflection)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_filterPipeline);
    commandList.setBindGroup(0u, m_filterBindGroup);
    const glm::vec4 pushConstants[4] = {
        glm::vec4(static_cast<float>(std::max(1, targets.width())),
                  static_cast<float>(std::max(1, targets.height())),
                  reflection.filterStrength,
                  ctx.weather.surfaceWetness),
        glm::vec4(ctx.camera.position, ctx.camera.nearPlane),
        glm::vec4(ctx.camera.farPlane, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f)
    };
    struct FilterPushConstants {
        glm::mat4 invViewProj;
        glm::vec4 params[4];
    };
    const FilterPushConstants filterPushConstants{
        ctx.camera.invViewProj,
        {
            pushConstants[0],
            pushConstants[1],
            pushConstants[2],
            pushConstants[3]
        }
    };
    commandList.pushConstants(&filterPushConstants,
                              sizeof(filterPushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList, targets.reflectionTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
}

void ReflectionPass::renderTemporal(const FrameContext& ctx, const ReflectionSettings& reflection,
                                     DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureReflectionTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureReflectionTemporalScratchTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureHistoryReflectionTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureVelocityTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.reflectionTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ReflectionTemporal";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList* copyCommandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (copyCommandListStorage == nullptr ||
        !copyCommandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& copyCommandList = *copyCommandListStorage;
    const GpuTimerSegmentToken copyTimerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(copyCommandList, GpuTimerPass::Reflection)
        : GpuTimerSegmentToken{};
    targets.transitionTexture(copyCommandList, targets.reflectionTextureHandle(),
                              RhiResourceState::TransferSrc);
    targets.transitionTexture(copyCommandList,
                              targets.reflectionTemporalScratchTextureHandle(),
                              RhiResourceState::TransferDst);
    targets.copyReflectionToTemporalScratch(copyCommandList);
    targets.transitionTexture(copyCommandList, targets.reflectionTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(copyCommandList,
                              targets.reflectionTemporalScratchTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(copyCommandList, copyTimerToken);
    }
    if (!copyCommandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&copyCommandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
    const std::array<RhiTextureViewHandle, 7> views = {
        targets.reflectionTemporalScratchTextureViewHandle(),
        targets.historyReflectionTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.materialTextureViewHandle(),
        targets.materialAuxTextureViewHandle()
    };
    if (!ensureTemporalRhiPipeline(rhiDevice) ||
        !ensureTemporalBindGroup(rhiDevice, views)) {
        return;
    }

    RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    targets.transitionTexture(commandList,
                              targets.reflectionTemporalScratchTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.historyReflectionTexturePrevHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.velocityTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.depthTextureHandle(),
                              RhiResourceState::DepthRead);
    targets.transitionTexture(commandList, targets.normalAoTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialAuxTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.reflectionTextureHandle(),
                              RhiResourceState::RenderTarget);
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Reflection)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_temporalPipeline);
    commandList.setBindGroup(0u, m_temporalBindGroup);
    const glm::vec4 pushConstants(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())),
        reflection.historyWeight,
        ctx.camera.nearPlane);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList, targets.reflectionTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
}

bool ReflectionPass::ensureFilterRhiPipeline(RhiDevice& rhiDevice) {
    if (m_filterRhiDevice != nullptr && m_filterRhiDevice != &rhiDevice) {
        destroyFilterRhiResources();
    }
    if (m_filterPipeline.isValid()) {
        return true;
    }
    m_filterRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/reflection_filter.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "ReflectionFilter.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_filterVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "ReflectionFilter.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_filterFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_filterVertexShader.isValid() || !m_filterFragmentShader.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_filterNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_filterLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_filterNearestSampler.isValid() || !m_filterLinearSampler.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "ReflectionFilter.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_filterBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_filterBindGroupLayout.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "ReflectionFilter.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_filterBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::mat4) + sizeof(glm::vec4) * 4u);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_filterPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_filterPipelineLayout.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "ReflectionFilter.Pipeline";
    pipelineDesc.vertexShader = m_filterVertexShader;
    pipelineDesc.fragmentShader = m_filterFragmentShader;
    pipelineDesc.layout = m_filterPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_filterPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_filterPipeline.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    return true;
}

bool ReflectionPass::ensureFilterBindGroup(RhiDevice& rhiDevice,
                                           const std::array<RhiTextureViewHandle, 5>& views) {
    if (!ensureFilterRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_filterBindGroup.isValid() && sameTextureViews(m_filterBoundViews, views)) {
        return true;
    }

    destroyFilterBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_filterBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 0u ? m_filterLinearSampler : m_filterNearestSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_filterBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_filterBindGroup.isValid()) {
        m_filterBoundViews = {};
        return false;
    }

    m_filterBoundViews = views;
    return true;
}

void ReflectionPass::destroyFilterBindGroup() {
    if (m_filterRhiDevice != nullptr && m_filterBindGroup.isValid()) {
        m_filterRhiDevice->destroyBindGroup(m_filterBindGroup);
    }
    m_filterBindGroup = {};
    m_filterBoundViews = {};
}

void ReflectionPass::destroyFilterRhiResources() {
    destroyFilterBindGroup();
    if (m_filterRhiDevice != nullptr) {
        if (m_filterPipeline.isValid()) {
            m_filterRhiDevice->destroyPipeline(m_filterPipeline);
        }
        if (m_filterVertexShader.isValid()) {
            m_filterRhiDevice->destroyShader(m_filterVertexShader);
        }
        if (m_filterFragmentShader.isValid()) {
            m_filterRhiDevice->destroyShader(m_filterFragmentShader);
        }
        if (m_filterPipelineLayout.isValid()) {
            m_filterRhiDevice->destroyPipelineLayout(m_filterPipelineLayout);
        }
        if (m_filterBindGroupLayout.isValid()) {
            m_filterRhiDevice->destroyBindGroupLayout(m_filterBindGroupLayout);
        }
        if (m_filterNearestSampler.isValid()) {
            m_filterRhiDevice->destroySampler(m_filterNearestSampler);
        }
        if (m_filterLinearSampler.isValid()) {
            m_filterRhiDevice->destroySampler(m_filterLinearSampler);
        }
    }

    m_filterPipeline = {};
    m_filterVertexShader = {};
    m_filterFragmentShader = {};
    m_filterPipelineLayout = {};
    m_filterBindGroupLayout = {};
    m_filterNearestSampler = {};
    m_filterLinearSampler = {};
    m_filterRhiDevice = nullptr;
}

bool ReflectionPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyTemporalRhiResources();
    }
    if (m_temporalPipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/reflection_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "ReflectionTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "ReflectionTemporal.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_temporalFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_temporalVertexShader.isValid() || !m_temporalFragmentShader.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_temporalNearestSampler.isValid() || !m_temporalLinearSampler.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "ReflectionTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 7u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_temporalBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_temporalBindGroupLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "ReflectionTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "ReflectionTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool ReflectionPass::ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                             const std::array<RhiTextureViewHandle, 7>& views) {
    if (!ensureTemporalRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_temporalBindGroup.isValid() && sameTextureViews(m_temporalBoundViews, views)) {
        return true;
    }

    destroyTemporalBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_temporalBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 1u ? m_temporalLinearSampler : m_temporalNearestSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_temporalBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_temporalBindGroup.isValid()) {
        m_temporalBoundViews = {};
        return false;
    }

    m_temporalBoundViews = views;
    return true;
}

void ReflectionPass::destroyTemporalBindGroup() {
    if (m_rhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void ReflectionPass::destroyTemporalRhiResources() {
    destroyTemporalBindGroup();
    if (m_rhiDevice != nullptr) {
        if (m_temporalPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_temporalPipeline);
        }
        if (m_temporalVertexShader.isValid()) {
            m_rhiDevice->destroyShader(m_temporalVertexShader);
        }
        if (m_temporalFragmentShader.isValid()) {
            m_rhiDevice->destroyShader(m_temporalFragmentShader);
        }
        if (m_temporalPipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_temporalPipelineLayout);
        }
        if (m_temporalBindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_temporalBindGroupLayout);
        }
        if (m_temporalNearestSampler.isValid()) {
            m_rhiDevice->destroySampler(m_temporalNearestSampler);
        }
        if (m_temporalLinearSampler.isValid()) {
            m_rhiDevice->destroySampler(m_temporalLinearSampler);
        }
    }

    m_temporalPipeline = {};
    m_temporalVertexShader = {};
    m_temporalFragmentShader = {};
    m_temporalPipelineLayout = {};
    m_temporalBindGroupLayout = {};
    m_temporalNearestSampler = {};
    m_temporalLinearSampler = {};
    m_rhiDevice = nullptr;
}
