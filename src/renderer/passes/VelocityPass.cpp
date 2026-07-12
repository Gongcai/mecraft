#include "VelocityPass.h"

#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../targets/DeferredRenderTargets.h"

#include <algorithm>
#include <optional>

#include <glm/glm.hpp>

namespace {
struct VelocityPushConstants {
    glm::mat4 invViewProj;
    glm::mat4 previousViewProj;
    glm::vec4 screenParams;
};

[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void VelocityPass::init(ResourceMgr&) {}

void VelocityPass::shutdown() {
    destroyRhiResources();
}

void VelocityPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                           DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureVelocityTextureView(rhiDevice) ||
        !targets.ensurePerObjectVelocityTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !ensureRhiPipeline(rhiDevice) ||
        !ensureRhiBindGroup(rhiDevice,
                            targets.depthTextureViewHandle(),
                            targets.perObjectVelocityTextureViewHandle())) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.velocityTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Velocity";
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
                              targets.velocityTextureHandle(),
                              RhiResourceState::RenderTarget);
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);

    const VelocityPushConstants pushConstants{
        ctx.camera.invViewProj,
        ctx.previousViewProj,
        glm::vec4(static_cast<float>(std::max(1, targets.width())),
                  static_cast<float>(std::max(1, targets.height())),
                  settings.taa.forceZeroVelocity ? 1.0f : 0.0f,
                  0.0f)
    };
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList,
                              targets.velocityTextureHandle(),
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

bool VelocityPass::ensureRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/velocity_resolve.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Velocity.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Velocity.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Nearest;
    samplerDesc.magFilter = RhiFilter::Nearest;
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
    bindGroupLayoutDesc.debugName = "Velocity.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 2u; ++binding) {
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
    pipelineLayoutDesc.debugName = "Velocity.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(VelocityPushConstants));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Velocity.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rg16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_pipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }

    return true;
}

bool VelocityPass::ensureRhiBindGroup(RhiDevice& rhiDevice,
                                      const RhiTextureViewHandle depthView,
                                      const RhiTextureViewHandle perObjectVelocityView) {
    if (!ensureRhiPipeline(rhiDevice) ||
        !depthView.isValid() ||
        !perObjectVelocityView.isValid()) {
        return false;
    }

    if (m_bindGroup.isValid() &&
        sameTextureView(m_boundDepthView, depthView) &&
        sameTextureView(m_boundPerObjectVelocityView, perObjectVelocityView)) {
        return true;
    }

    destroyRhiBindGroup();

    const RhiTextureViewHandle views[2] = {
        depthView,
        perObjectVelocityView
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < 2u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = m_sampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup.isValid()) {
        m_boundDepthView = {};
        m_boundPerObjectVelocityView = {};
        return false;
    }

    m_boundDepthView = depthView;
    m_boundPerObjectVelocityView = perObjectVelocityView;
    return true;
}

void VelocityPass::destroyRhiBindGroup() {
    if (m_rhiDevice != nullptr && m_bindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_bindGroup);
    }
    m_bindGroup = {};
    m_boundDepthView = {};
    m_boundPerObjectVelocityView = {};
}

void VelocityPass::destroyRhiResources() {
    destroyRhiBindGroup();
    if (m_rhiDevice != nullptr) {
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

    m_pipeline = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_pipelineLayout = {};
    m_bindGroupLayout = {};
    m_sampler = {};
    m_rhiDevice = nullptr;
}
