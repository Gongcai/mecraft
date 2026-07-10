#include "SsgiPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <size_t Count>
[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, Count>& lhs,
                                    const std::array<RhiTextureViewHandle, Count>& rhs) {
    for (size_t index = 0u; index < lhs.size(); ++index) {
        if (!sameTextureView(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

struct alignas(16) SsgiBaseParams {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 cameraPosRadius;
    glm::vec4 halfResolutionStrengthMaxDistance;
    glm::vec4 quality;
    glm::ivec4 controls;
};
static_assert(sizeof(SsgiBaseParams) == 192u);
} // namespace

void SsgiPass::init(ResourceMgr& resourceMgr) {
    m_ssgiDenoiseShader = resourceMgr.getShader("ssgi_denoise");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsgiPass::shutdown() {
    destroyBaseRhiResources();
    destroyUpsampleRhiResources();
    destroyTemporalRhiResources();
    destroyNoiseTextureView();
    m_ssgiDenoiseShader = nullptr;
    m_noiseTexture = {};
}

void SsgiPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                       DeferredRenderTargets& targets) {
    if (!settings.ssgi.enabled) {
        return;
    }

    renderSsgiBase(ctx, settings, targets);
    renderSsgiUpsample(ctx, targets);
    const bool temporalActive = settings.ssgi.temporalEnabled;
    if (temporalActive) {
        renderSsgiTemporal(ctx, settings.ssgi, targets);
    }
    const bool denoiseActive = settings.ssgi.denoiseEnabled &&
        m_ssgiDenoiseShader != nullptr &&
        std::clamp(settings.ssgi.denoiseIterations, 0, 4) > 0;
    if (denoiseActive) {
        renderSsgiDenoise(ctx, settings.ssgi, targets,
                          temporalActive
                              ? renderer::rhi::gl::textureId(targets.ssgiTemporalTextureHandle())
                              : renderer::rhi::gl::textureId(targets.ssgiTextureHandle()),
                          temporalActive
                              ? renderer::rhi::gl::textureId(targets.ssgiTemporalMomentsTextureHandle())
                              : 0);
    } else if (temporalActive) {
        if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
            return;
        }
        targets.copySsgiTemporalToSsgi(*ctx.shared->rhiDevice);
    }
}

void SsgiPass::renderSsgiBase(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSceneLightingTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }
    const SsgiSettings& ssgi = settings.ssgi;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureNoiseTextureView(rhiDevice)) {
        return;
    }
    const std::array<RhiTextureViewHandle, 6> views = {
        targets.sceneLightingTextureViewHandle(),
        targets.albedoTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.materialAuxTextureViewHandle(),
        targets.depthTextureViewHandle(),
        m_noiseTextureView
    };
    if (!ensureBaseRhiPipeline(rhiDevice) || !ensureBaseBindGroup(rhiDevice, views)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssgiHalfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsgiHalfRes";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    SsgiBaseParams params{};
    params.viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    params.invViewProj = settings.taa.enabled
        ? ctx.camera.jitteredInvViewProj
        : ctx.camera.invViewProj;
    params.cameraPosRadius = glm::vec4(ctx.camera.position, ssgi.radius);
    params.halfResolutionStrengthMaxDistance = glm::vec4(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ssgi.strength,
        ssgi.maxDistance);
    params.quality = glm::vec4(ssgi.thickness,
                               ssgi.radianceFilterStrength,
                               ssgi.colorBleedStrength,
                               0.0f);
    params.controls = glm::ivec4(std::clamp(ssgi.samples, 1, 32),
                                 static_cast<int>(ctx.frameIndex & 0x7fffffffULL),
                                 0,
                                 0);

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.updateBuffer(m_baseUniformBuffer, 0u, &params, sizeof(params));
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_basePipeline);
    commandList.setBindGroup(0u, m_baseBindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

bool SsgiPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
    if (m_noiseViewDevice != nullptr && m_noiseViewDevice != &rhiDevice) {
        destroyNoiseTextureView();
    }
    if (m_noiseTextureView.isValid()) {
        return true;
    }
    if (!m_noiseTexture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_noiseTexture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    m_noiseTextureView = rhiDevice.createTextureView(viewDesc);
    if (!m_noiseTextureView.isValid()) {
        return false;
    }

    m_noiseViewDevice = &rhiDevice;
    return true;
}

void SsgiPass::destroyNoiseTextureView() {
    if (m_noiseViewDevice != nullptr && m_noiseTextureView.isValid()) {
        m_noiseViewDevice->destroyTextureView(m_noiseTextureView);
    }
    m_noiseTextureView = {};
    m_noiseViewDevice = nullptr;
}

bool SsgiPass::ensureBaseRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/ssgi.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsgiBase.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_baseVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsgiBase.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_baseFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_baseVertexShader.isValid() || !m_baseFragmentShader.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "SsgiBase.Params";
    uniformBufferDesc.size = sizeof(SsgiBaseParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_baseUniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_baseUniformBuffer.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter, const RhiAddressMode addressMode) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = filter;
        samplerDesc.magFilter = filter;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = addressMode;
        samplerDesc.addressV = addressMode;
        samplerDesc.addressW = addressMode;
        return rhiDevice.createSampler(samplerDesc);
    };
    m_baseNearestSampler = createSampler(RhiFilter::Nearest, RhiAddressMode::ClampToEdge);
    m_baseLinearSampler = createSampler(RhiFilter::Linear, RhiAddressMode::ClampToEdge);
    m_baseNoiseSampler = createSampler(RhiFilter::Linear, RhiAddressMode::Repeat);
    if (!m_baseNearestSampler.isValid() || !m_baseLinearSampler.isValid() ||
        !m_baseNoiseSampler.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsgiBase.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 6u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    bindGroupLayoutDesc.entries.push_back({
        6u,
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
    pipelineLayoutDesc.debugName = "SsgiBase.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_baseBindGroupLayout);
    m_basePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_basePipelineLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsgiBase.Pipeline";
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

bool SsgiPass::ensureBaseBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 6>& views) {
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
    const RhiSamplerHandle samplers[6] = {
        m_baseLinearSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNoiseSampler
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
    uniformEntry.binding = 6u;
    uniformEntry.resource.buffer.buffer = m_baseUniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(SsgiBaseParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_baseBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_baseBindGroup.isValid()) {
        m_baseBoundViews = {};
        return false;
    }

    m_baseBoundViews = views;
    return true;
}

void SsgiPass::destroyBaseBindGroup() {
    if (m_baseRhiDevice != nullptr && m_baseBindGroup.isValid()) {
        m_baseRhiDevice->destroyBindGroup(m_baseBindGroup);
    }
    m_baseBindGroup = {};
    m_baseBoundViews = {};
}

void SsgiPass::destroyBaseRhiResources() {
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
        const RhiSamplerHandle samplers[] = {
            m_baseNearestSampler,
            m_baseLinearSampler,
            m_baseNoiseSampler
        };
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_baseRhiDevice->destroySampler(sampler);
            }
        }
    }

    m_baseUniformBuffer = {};
    m_baseNearestSampler = {};
    m_baseLinearSampler = {};
    m_baseNoiseSampler = {};
    m_baseBindGroupLayout = {};
    m_basePipelineLayout = {};
    m_baseVertexShader = {};
    m_baseFragmentShader = {};
    m_basePipeline = {};
    m_baseRhiDevice = nullptr;
}

void SsgiPass::renderSsgiUpsample(const FrameContext& ctx, DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsgiHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 2> views = {
        targets.ssgiHalfResTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    if (!ensureUpsampleRhiPipeline(rhiDevice) || !ensureUpsampleBindGroup(rhiDevice, views)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssgiTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Ssgi";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::vec4 pushConstants(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ctx.camera.nearPlane,
        0.0f);
    commandList.setGraphicsPipeline(m_upsamplePipeline);
    commandList.setBindGroup(0u, m_upsampleBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

bool SsgiPass::ensureUpsampleRhiPipeline(RhiDevice& rhiDevice) {
    if (m_upsampleRhiDevice != nullptr && m_upsampleRhiDevice != &rhiDevice) {
        destroyUpsampleRhiResources();
    }
    if (m_upsamplePipeline.isValid()) {
        return true;
    }
    m_upsampleRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssgi_upsample.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsgiUpsample.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_upsampleVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsgiUpsample.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_upsampleFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_upsampleVertexShader.isValid() || !m_upsampleFragmentShader.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_upsampleNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_upsampleLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_upsampleNearestSampler.isValid() || !m_upsampleLinearSampler.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsgiUpsample.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 2u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_upsampleBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_upsampleBindGroupLayout.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsgiUpsample.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_upsampleBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_upsamplePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_upsamplePipelineLayout.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsgiUpsample.Pipeline";
    pipelineDesc.vertexShader = m_upsampleVertexShader;
    pipelineDesc.fragmentShader = m_upsampleFragmentShader;
    pipelineDesc.layout = m_upsamplePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_upsamplePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_upsamplePipeline.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    return true;
}

bool SsgiPass::ensureUpsampleBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 2>& views) {
    if (!ensureUpsampleRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_upsampleBindGroup.isValid() && sameTextureViews(m_upsampleBoundViews, views)) {
        return true;
    }

    destroyUpsampleBindGroup();
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_upsampleBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 0u ? m_upsampleNearestSampler : m_upsampleLinearSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_upsampleBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_upsampleBindGroup.isValid()) {
        m_upsampleBoundViews = {};
        return false;
    }

    m_upsampleBoundViews = views;
    return true;
}

void SsgiPass::destroyUpsampleBindGroup() {
    if (m_upsampleRhiDevice != nullptr && m_upsampleBindGroup.isValid()) {
        m_upsampleRhiDevice->destroyBindGroup(m_upsampleBindGroup);
    }
    m_upsampleBindGroup = {};
    m_upsampleBoundViews = {};
}

void SsgiPass::destroyUpsampleRhiResources() {
    destroyUpsampleBindGroup();
    if (m_upsampleRhiDevice != nullptr) {
        if (m_upsamplePipeline.isValid()) {
            m_upsampleRhiDevice->destroyPipeline(m_upsamplePipeline);
        }
        if (m_upsampleVertexShader.isValid()) {
            m_upsampleRhiDevice->destroyShader(m_upsampleVertexShader);
        }
        if (m_upsampleFragmentShader.isValid()) {
            m_upsampleRhiDevice->destroyShader(m_upsampleFragmentShader);
        }
        if (m_upsamplePipelineLayout.isValid()) {
            m_upsampleRhiDevice->destroyPipelineLayout(m_upsamplePipelineLayout);
        }
        if (m_upsampleBindGroupLayout.isValid()) {
            m_upsampleRhiDevice->destroyBindGroupLayout(m_upsampleBindGroupLayout);
        }
        if (m_upsampleNearestSampler.isValid()) {
            m_upsampleRhiDevice->destroySampler(m_upsampleNearestSampler);
        }
        if (m_upsampleLinearSampler.isValid()) {
            m_upsampleRhiDevice->destroySampler(m_upsampleLinearSampler);
        }
    }

    m_upsamplePipeline = {};
    m_upsampleVertexShader = {};
    m_upsampleFragmentShader = {};
    m_upsamplePipelineLayout = {};
    m_upsampleBindGroupLayout = {};
    m_upsampleNearestSampler = {};
    m_upsampleLinearSampler = {};
    m_upsampleRhiDevice = nullptr;
}

void SsgiPass::renderSsgiDenoise(const FrameContext& ctx, const SsgiSettings& ssgi,
                                 DeferredRenderTargets& targets, const uint32_t initialInputTexture,
                                 const uint32_t momentsTexture) {
    if (m_ssgiDenoiseShader == nullptr) {
        return;
    }

    const int iterations = std::clamp(ssgi.denoiseIterations, 0, 4);
    if (iterations <= 0) {
        return;
    }
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiDenoiseTextureView(*ctx.shared->rhiDevice, 0) ||
        (iterations > 1 && !targets.ensureSsgiDenoiseTextureView(*ctx.shared->rhiDevice, 1))) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const glm::vec2 screenSize(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())));

    m_ssgiDenoiseShader->use();
    m_ssgiDenoiseShader->setInt("uInputTex", 0);
    m_ssgiDenoiseShader->setInt("uDepthTex", 1);
    m_ssgiDenoiseShader->setInt("uNormalAoTex", 2);
    m_ssgiDenoiseShader->setInt("uMomentsTex", 3);
    m_ssgiDenoiseShader->setInt("uMomentsAvailable", momentsTexture != 0 ? 1 : 0);
    m_ssgiDenoiseShader->setVec2("uScreenSize", screenSize);
    m_ssgiDenoiseShader->setFloat("uNear", ctx.camera.nearPlane);
    m_ssgiDenoiseShader->setFloat("uStrength", ssgi.denoiseStrength);

    int outputSlot = 0;
    for (int i = 0; i < iterations; ++i) {
        outputSlot = i & 1;
        RhiColorAttachment colorAttachment;
        colorAttachment.view = targets.ssgiDenoiseTextureViewHandle(outputSlot);
        colorAttachment.loadOp = RhiLoadOp::Clear;
        colorAttachment.storeOp = RhiStoreOp::Store;
        colorAttachment.clearColor[0] = 0.0f;
        colorAttachment.clearColor[1] = 0.0f;
        colorAttachment.clearColor[2] = 0.0f;
        colorAttachment.clearColor[3] = 0.0f;

        RhiRenderingInfo renderingInfo;
        renderingInfo.debugName = "SsgiDenoise";
        renderingInfo.renderArea = {
            0,
            0,
            static_cast<uint32_t>(std::max(1, targets.width())),
            static_cast<uint32_t>(std::max(1, targets.height()))
        };
        renderingInfo.colorAttachments = &colorAttachment;
        renderingInfo.colorAttachmentCount = 1u;

        RhiCommandList& commandList = rhiDevice.beginFrame();
        commandList.beginRendering(renderingInfo);

        const GLuint passInputTexture = (i == 0)
            ? initialInputTexture
            : renderer::rhi::gl::textureId(targets.ssgiDenoiseTextureHandle(1 - outputSlot));
        m_ssgiDenoiseShader->setFloat("uStepWidth", static_cast<float>(1 << i));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, passInputTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, momentsTexture);

        RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiDenoiseShader);
        commandList.endRendering();
        rhiDevice.submitFrame(commandList);
    }

    targets.copySsgiDenoiseToSsgi(rhiDevice, outputSlot);

    for (int i = 3; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

void SsgiPass::renderSsgiTemporal(const FrameContext& ctx, const SsgiSettings& ssgi,
                                  DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiTemporalTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSsgiHistoryTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSsgiTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureVelocityTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureHistoryDepthTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 7> views = {
        targets.ssgiTextureViewHandle(),
        targets.ssgiHistoryTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.historyDepthTexturePrevViewHandle(),
        targets.ssgiMomentsHistoryTexturePrevViewHandle()
    };
    if (!ensureTemporalRhiPipeline(rhiDevice) || !ensureTemporalBindGroup(rhiDevice, views)) {
        return;
    }

    RhiColorAttachment colorAttachments[2];
    colorAttachments[0].view = targets.ssgiTemporalTextureViewHandle();
    colorAttachments[0].loadOp = RhiLoadOp::Clear;
    colorAttachments[0].storeOp = RhiStoreOp::Store;
    colorAttachments[0].clearColor[0] = 0.0f;
    colorAttachments[0].clearColor[1] = 0.0f;
    colorAttachments[0].clearColor[2] = 0.0f;
    colorAttachments[0].clearColor[3] = 0.0f;
    colorAttachments[1].view = targets.ssgiTemporalMomentsTextureViewHandle();
    colorAttachments[1].loadOp = RhiLoadOp::Clear;
    colorAttachments[1].storeOp = RhiStoreOp::Store;
    colorAttachments[1].clearColor[0] = 0.0f;
    colorAttachments[1].clearColor[1] = 0.0f;
    colorAttachments[1].clearColor[2] = 0.0f;
    colorAttachments[1].clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsgiTemporal";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 2u;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);
    const glm::vec4 pushConstants(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())),
        ssgi.historyWeight,
        ctx.camera.nearPlane);
    commandList.setGraphicsPipeline(m_temporalPipeline);
    commandList.setBindGroup(0u, m_temporalBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
    targets.copySsgiTemporalToHistory(rhiDevice);
}

bool SsgiPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
    if (m_temporalRhiDevice != nullptr && m_temporalRhiDevice != &rhiDevice) {
        destroyTemporalRhiResources();
    }
    if (m_temporalPipeline.isValid()) {
        return true;
    }
    m_temporalRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssgi_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsgiTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsgiTemporal.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_temporalFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_temporalVertexShader.isValid() || !m_temporalFragmentShader.isValid()) {
        destroyTemporalRhiResources();
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
    m_temporalNearestSampler = createSampler(RhiFilter::Nearest);
    m_temporalLinearSampler = createSampler(RhiFilter::Linear);
    if (!m_temporalNearestSampler.isValid() || !m_temporalLinearSampler.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsgiTemporal.BindGroupLayout";
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
    pipelineLayoutDesc.debugName = "SsgiTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsgiTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool SsgiPass::ensureTemporalBindGroup(
    RhiDevice& rhiDevice,
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
    const RhiSamplerHandle samplers[7] = {
        m_temporalNearestSampler,
        m_temporalLinearSampler,
        m_temporalNearestSampler,
        m_temporalNearestSampler,
        m_temporalNearestSampler,
        m_temporalNearestSampler,
        m_temporalLinearSampler
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_temporalBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
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

void SsgiPass::destroyTemporalBindGroup() {
    if (m_temporalRhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_temporalRhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void SsgiPass::destroyTemporalRhiResources() {
    destroyTemporalBindGroup();
    if (m_temporalRhiDevice != nullptr) {
        if (m_temporalPipeline.isValid()) {
            m_temporalRhiDevice->destroyPipeline(m_temporalPipeline);
        }
        if (m_temporalVertexShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalVertexShader);
        }
        if (m_temporalFragmentShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalFragmentShader);
        }
        if (m_temporalPipelineLayout.isValid()) {
            m_temporalRhiDevice->destroyPipelineLayout(m_temporalPipelineLayout);
        }
        if (m_temporalBindGroupLayout.isValid()) {
            m_temporalRhiDevice->destroyBindGroupLayout(m_temporalBindGroupLayout);
        }
        if (m_temporalNearestSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalNearestSampler);
        }
        if (m_temporalLinearSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalLinearSampler);
        }
    }

    m_temporalPipeline = {};
    m_temporalVertexShader = {};
    m_temporalFragmentShader = {};
    m_temporalPipelineLayout = {};
    m_temporalBindGroupLayout = {};
    m_temporalNearestSampler = {};
    m_temporalLinearSampler = {};
    m_temporalRhiDevice = nullptr;
}
