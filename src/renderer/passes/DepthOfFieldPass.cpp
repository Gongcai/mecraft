#include "DepthOfFieldPass.h"

#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../targets/DeferredRenderTargets.h"
#include "../../resource/ResourceMgr.h"

#include <algorithm>
#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace {
struct DofPushConstants {
    glm::mat4 projection;
    glm::mat4 invProjection;
    glm::vec4 params;
    glm::vec4 screenParams;
};

[[nodiscard]] bool sameTextureHandle(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void DepthOfFieldPass::init(ResourceMgr& resourceMgr) {
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void DepthOfFieldPass::shutdown() {
    destroyRhiResources();
    m_noiseTexture = {};
}

void DepthOfFieldPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                               DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureSceneResolvedTextureView(rhiDevice) ||
        !targets.ensureHistorySceneTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !ensureRhiPipeline(rhiDevice) ||
        !ensureNoiseTextureView(rhiDevice) ||
        !ensureRhiBindGroup(rhiDevice,
                            targets.currentHistoryIndex(),
                            targets.historySceneTextureViewHandle(),
                            targets.depthTextureViewHandle(),
                            m_noiseTextureView)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneResolvedTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "DepthOfField";
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
    targets.transitionTexture(commandList,
                              targets.sceneResolvedTextureHandle(),
                              RhiResourceState::TransferSrc);
    targets.transitionTexture(commandList,
                              targets.historySceneTextureHandle(),
                              RhiResourceState::TransferDst);
    RhiTextureBlit historyCopy;
    historyCopy.src = targets.sceneResolvedTextureHandle();
    historyCopy.dst = targets.historySceneTextureHandle();
    commandList.blitTexture(historyCopy);
    targets.transitionTexture(commandList,
                              targets.historySceneTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList,
                              targets.sceneResolvedTextureHandle(),
                              RhiResourceState::RenderTarget);
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup[targets.currentHistoryIndex()]);

    const DofPushConstants pushConstants{
        ctx.camera.projection,
        glm::inverse(ctx.camera.projection),
        glm::vec4(settings.postProcess.dofFocusDistance,
                  settings.postProcess.dofAperture,
                  settings.postProcess.dofIntensity,
                  1.0f),
        glm::vec4(static_cast<float>(std::max(1, targets.width())),
                  static_cast<float>(std::max(1, targets.height())),
                  0.0f,
                  0.0f)
    };
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList,
                              targets.sceneResolvedTextureHandle(),
                              RhiResourceState::ShaderRead);
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

bool DepthOfFieldPass::ensureRhiPipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/dof.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "DepthOfField.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "DepthOfField.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_sampler = rhiDevice.createSampler(samplerDesc);
    if (!m_sampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "DepthOfField.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "DepthOfField.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(DofPushConstants));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "DepthOfField.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_pipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }

    return true;
}

bool DepthOfFieldPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    m_rhiDevice = &rhiDevice;

    if (m_noiseTextureView.isValid() && sameTextureHandle(m_noiseViewTexture, m_noiseTexture)) {
        return true;
    }

    if (m_noiseTextureView.isValid()) {
        destroyRhiBindGroup();
        rhiDevice.destroyTextureView(m_noiseTextureView);
        m_noiseTextureView = {};
        m_noiseViewTexture = {};
    }

    if (!m_noiseTexture.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_noiseTexture;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_noiseTextureView = rhiDevice.createTextureView(desc);
    if (!m_noiseTextureView.isValid()) {
        return false;
    }

    m_noiseViewTexture = m_noiseTexture;
    return true;
}

bool DepthOfFieldPass::ensureRhiBindGroup(RhiDevice& rhiDevice,
                                          const int historyIndex,
                                          const RhiTextureViewHandle sceneView,
                                          const RhiTextureViewHandle depthView,
                                          const RhiTextureViewHandle noiseView) {
    if (!ensureRhiPipeline(rhiDevice) ||
        historyIndex < 0 ||
        historyIndex >= 2 ||
        !sceneView.isValid() ||
        !depthView.isValid() ||
        !noiseView.isValid()) {
        return false;
    }

    if (m_bindGroup[historyIndex].isValid() &&
        sameTextureView(m_boundSceneView[historyIndex], sceneView) &&
        sameTextureView(m_boundDepthView[historyIndex], depthView) &&
        sameTextureView(m_boundNoiseView[historyIndex], noiseView)) {
        return true;
    }

    if (m_bindGroup[historyIndex].isValid()) {
        rhiDevice.destroyBindGroup(m_bindGroup[historyIndex]);
        m_bindGroup[historyIndex] = {};
    }

    const RhiTextureViewHandle views[3] = {
        sceneView,
        depthView,
        noiseView
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = m_sampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_bindGroup[historyIndex] = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup[historyIndex].isValid()) {
        m_boundSceneView[historyIndex] = {};
        m_boundDepthView[historyIndex] = {};
        m_boundNoiseView[historyIndex] = {};
        return false;
    }

    m_boundSceneView[historyIndex] = sceneView;
    m_boundDepthView[historyIndex] = depthView;
    m_boundNoiseView[historyIndex] = noiseView;
    return true;
}

void DepthOfFieldPass::destroyRhiBindGroup() {
    if (m_rhiDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_bindGroup) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    } else {
        m_bindGroup[0] = {};
        m_bindGroup[1] = {};
    }

    for (int i = 0; i < 2; ++i) {
        m_boundSceneView[i] = {};
        m_boundDepthView[i] = {};
        m_boundNoiseView[i] = {};
    }
}

void DepthOfFieldPass::destroyRhiResources() {
    destroyRhiBindGroup();
    if (m_rhiDevice != nullptr) {
        if (m_noiseTextureView.isValid()) {
            m_rhiDevice->destroyTextureView(m_noiseTextureView);
        }
        if (m_pipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_pipeline);
        }
        if (m_vertexShader.isValid()) {
            m_rhiDevice->destroyShader(m_vertexShader);
        }
        if (m_fragmentShader.isValid()) {
            m_rhiDevice->destroyShader(m_fragmentShader);
        }
        if (m_pipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        }
        if (m_bindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_bindGroupLayout);
        }
        if (m_sampler.isValid()) {
            m_rhiDevice->destroySampler(m_sampler);
        }
    }

    m_noiseTextureView = {};
    m_noiseViewTexture = {};
    m_pipeline = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_pipelineLayout = {};
    m_bindGroupLayout = {};
    m_sampler = {};
    m_rhiDevice = nullptr;
}
