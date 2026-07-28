#include "ParticleSystem.h"

#include <optional>
#include <vector>

#include <glm/common.hpp>

#include "../ecs/components/Components.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/util/ParticleEventBuffer.h"
#include "../resource/ResourceMgr.h"
#include "../renderer/rhi/RhiCommandList.h"
#include "../renderer/rhi/RhiDevice.h"
#include "../renderer/rhi/RhiShaderSourceLoader.h"

namespace {
struct ParticlePushConstants {
    glm::mat4 viewProj;
    glm::vec4 biomeTint;
    glm::vec4 screenParams;
};

[[nodiscard]] bool sameTexture(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void ParticleSystem::bindRegistry(ecs::GameplayRegistry& registry) {
    m_registry = &registry;
}

bool ParticleSystem::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    m_texArray = &resourceMgr.getTextureArray();
    if (!m_texArray->texture.isValid()) {
        shutdown();
        return false;
    }
    return createRhiResources();
}

void ParticleSystem::shutdown() {
    destroyRhiResources();
    m_texArray = nullptr;
}

void ParticleSystem::emit(const glm::ivec3& blockPos, const BlockID blockType) {
    if (m_registry == nullptr || blockType == 0) {
        return;
    }

    auto& bus = ecs::ensureParticleEventBus(*m_registry);
    bus.push({blockPos, blockType});
}

void ParticleSystem::update(const float dt) {
    static_cast<void>(dt);
}

void ParticleSystem::prepareFrame(const glm::mat4& view, RhiCommandList& commandList) {
    m_preparedVertexCount = 0u;
    if (m_registry == nullptr || !m_rhiVertexBuffer.isValid()) {
        return;
    }
    if (buildVertices(view, m_vertexBuffer) == 0) {
        return;
    }
    commandList.bufferBarrier({m_rhiVertexBuffer, RhiResourceState::VertexBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_rhiVertexBuffer, 0u,
                             m_vertexBuffer.data(), m_vertexBuffer.size() * sizeof(float));
    commandList.bufferBarrier({m_rhiVertexBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::VertexBuffer});
    m_preparedVertexCount = static_cast<uint32_t>(m_vertexBuffer.size() / 8u);
}

int ParticleSystem::buildVertices(const glm::mat4& view, std::vector<float>& vertices) {
    auto particleView = m_registry->view<ecs::ParticleTag, ecs::TransformComponent, ecs::ParticleComponent>();
    if (particleView.begin() == particleView.end()) {
        return 0;
    }

    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up(view[0][1], view[1][1], view[2][1]);

    vertices.clear();
    vertices.reserve(static_cast<size_t>(MAX_PARTICLES) * 48u);

    int count = 0;
    for (const entt::entity e : particleView) {
        const auto& transform = particleView.get<ecs::TransformComponent>(e);
        const auto& particle = particleView.get<ecs::ParticleComponent>(e);
        if (particle.life <= 0.0f || particle.maxLife <= 0.0f) {
            continue;
        }
        if (count >= MAX_PARTICLES) {
            break;
        }

        const float lifeRatio = glm::clamp(particle.life / particle.maxLife, 0.0f, 1.0f);
        const float alpha = lifeRatio < 0.25f ? glm::smoothstep(0.0f, 0.25f, lifeRatio) : 1.0f;
        const float halfSize = particle.size * 0.5f;

        glm::vec3 c0 = transform.position - right * halfSize - up * halfSize;
        glm::vec3 c1 = transform.position + right * halfSize - up * halfSize;
        glm::vec3 c2 = transform.position + right * halfSize + up * halfSize;
        glm::vec3 c3 = transform.position - right * halfSize + up * halfSize;

        const float uvMinX = particle.uvMin.x, uvMinY = particle.uvMin.y;
        const float uvMaxX = particle.uvMax.x, uvMaxY = particle.uvMax.y;
        const float layer = particle.layer;
        const float btf = particle.biomeTintFactor;

        // Triangle 1: c0-c1-c2
        vertices.insert(vertices.end(), {c0.x, c0.y, c0.z, uvMinX, uvMinY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c1.x, c1.y, c1.z, uvMaxX, uvMinY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c2.x, c2.y, c2.z, uvMaxX, uvMaxY, layer, alpha, btf});
        // Triangle 2: c0-c2-c3
        vertices.insert(vertices.end(), {c0.x, c0.y, c0.z, uvMinX, uvMinY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c2.x, c2.y, c2.z, uvMaxX, uvMaxY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c3.x, c3.y, c3.z, uvMinX, uvMaxY, layer, alpha, btf});

        ++count;
    }
    return count;
}

void ParticleSystem::render(RhiCommandList& commandList, const glm::mat4& viewProj) {
    if (m_preparedVertexCount == 0u || !m_forwardPipeline.isValid() ||
        !m_forwardBindGroup.isValid()) {
        return;
    }
    const ParticlePushConstants pushConstants{
        viewProj,
        glm::vec4(0.50f, 0.78f, 0.34f, 0.0f),
        glm::vec4(0.0f)
    };
    commandList.setGraphicsPipeline(m_forwardPipeline);
    commandList.setBindGroup(0u, m_forwardBindGroup);
    commandList.setVertexBuffer(0u, m_rhiVertexBuffer, 0u);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(m_preparedVertexCount, 1u, 0u, 0u);
}

void ParticleSystem::renderToSceneResolved(RhiCommandList& commandList,
                                            const RhiTextureHandle voxelLightTexture,
                                            const RhiTextureHandle depthTexture,
                                            const glm::mat4& viewProj,
                                            const glm::vec2& screenSize) {
    if (m_preparedVertexCount == 0u ||
        !ensureDeferredBindGroup(voxelLightTexture, depthTexture)) {
        return;
    }
    const ParticlePushConstants pushConstants{
        viewProj,
        glm::vec4(0.50f, 0.78f, 0.34f, 0.0f),
        glm::vec4(screenSize, 0.0f, 0.0f)
    };
    commandList.setGraphicsPipeline(m_deferredPipeline);
    commandList.setBindGroup(0u, m_deferredBindGroup);
    commandList.setVertexBuffer(0u, m_rhiVertexBuffer, 0u);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(m_preparedVertexCount, 1u, 0u, 0u);
}

bool ParticleSystem::createRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/particle_rhi.vert");
    const auto forwardSource = renderer::rhi::loadShaderSource("assets/shaders/particle_rhi.frag");
    renderer::rhi::RhiShaderSourceOptions options;
    options.preprocessorDefinitions.push_back("PARTICLE_DEFERRED");
    const auto deferredSource = renderer::rhi::loadShaderSource("assets/shaders/particle_rhi.frag", options);
    if (!vertexSource || !forwardSource || !deferredSource) return false;

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Particle.VertexBuffer";
    bufferDesc.size = VERTEX_BUFFER_BYTES;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    bufferDesc.memoryCategory = RhiMemoryCategory::Geometry;
    m_rhiVertexBuffer = m_rhiDevice->createBuffer(bufferDesc, nullptr, 0u);
    RhiTextureViewDesc arrayViewDesc;
    arrayViewDesc.texture = m_texArray->texture;
    arrayViewDesc.viewType = RhiTextureViewType::Texture2DArray;
    arrayViewDesc.mipCount = kRhiRemainingMipLevels;
    arrayViewDesc.layerCount = kRhiRemainingArrayLayers;
    m_textureArrayView = m_rhiDevice->createTextureView(arrayViewDesc);
    RhiSamplerDesc linearDesc;
    linearDesc.minFilter = RhiFilter::Linear;
    linearDesc.magFilter = RhiFilter::Linear;
    linearDesc.mipmapMode = RhiMipmapMode::Linear;
    linearDesc.addressU = RhiAddressMode::Repeat;
    linearDesc.addressV = RhiAddressMode::Repeat;
    m_linearSampler = m_rhiDevice->createSampler(linearDesc);
    RhiSamplerDesc nearestDesc;
    nearestDesc.minFilter = RhiFilter::Nearest;
    nearestDesc.magFilter = RhiFilter::Nearest;
    nearestDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_nearestSampler = m_rhiDevice->createSampler(nearestDesc);

    auto createShader = [&](const char* name, RhiShaderStage stage, const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = name;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return m_rhiDevice->createShader(desc);
    };
    m_vertexShader = createShader("Particle.Vertex", RhiShaderStage::Vertex, *vertexSource);
    m_forwardFragmentShader = createShader("Particle.ForwardFragment", RhiShaderStage::Fragment, *forwardSource);
    m_deferredFragmentShader = createShader("Particle.DeferredFragment", RhiShaderStage::Fragment, *deferredSource);

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "Particle.ForwardBindGroupLayout";
    layoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                  rhiFlag(RhiShaderStage::Fragment), 1u});
    m_forwardBindGroupLayout = m_rhiDevice->createBindGroupLayout(layoutDesc);
    layoutDesc.debugName = "Particle.DeferredBindGroupLayout";
    layoutDesc.entries.push_back({1u, RhiBindingType::CombinedTextureSampler,
                                  rhiFlag(RhiShaderStage::Fragment), 1u});
    layoutDesc.entries.push_back({2u, RhiBindingType::CombinedTextureSampler,
                                  rhiFlag(RhiShaderStage::Fragment), 1u});
    m_deferredBindGroupLayout = m_rhiDevice->createBindGroupLayout(layoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Particle.ForwardPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_forwardBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(ParticlePushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_forwardPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Particle.DeferredPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_deferredBindGroupLayout;
    m_deferredPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_forwardFragmentShader;
    pipelineDesc.layout = m_forwardPipelineLayout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.vertexInput.bindings.push_back({0u, 8u * sizeof(float), RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, 0u}, {1u, 0u, RhiVertexFormat::Float2, 3u * sizeof(float)},
        {2u, 0u, RhiVertexFormat::Float, 5u * sizeof(float)}, {3u, 0u, RhiVertexFormat::Float, 6u * sizeof(float)},
        {4u, 0u, RhiVertexFormat::Float, 7u * sizeof(float)}};
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    pipelineDesc.debugName = "Particle.ForwardPipeline";
    m_forwardPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Particle.DeferredPipeline";
    pipelineDesc.fragmentShader = m_deferredFragmentShader;
    pipelineDesc.layout = m_deferredPipelineLayout;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.colorFormats = {
        RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::R8Unorm,
        RhiTextureFormat::R8Unorm
    };
    RhiBlendAttachmentState maskBlend;
    maskBlend.blendEnabled = true;
    maskBlend.srcColor = RhiBlendFactor::One;
    maskBlend.dstColor = RhiBlendFactor::One;
    maskBlend.colorOp = RhiBlendOp::Max;
    maskBlend.srcAlpha = RhiBlendFactor::One;
    maskBlend.dstAlpha = RhiBlendFactor::One;
    maskBlend.alphaOp = RhiBlendOp::Max;
    pipelineDesc.blend.attachments.push_back(maskBlend);
    pipelineDesc.blend.attachments.push_back(maskBlend);
    pipelineDesc.depthFormat = RhiTextureFormat::Undefined;
    m_deferredPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_forwardBindGroupLayout;
    RhiBindGroupEntry arrayEntry;
    arrayEntry.binding = 0u;
    arrayEntry.resource.combinedTextureSampler = {m_textureArrayView, m_linearSampler};
    bindGroupDesc.entries.push_back(arrayEntry);
    m_forwardBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_rhiVertexBuffer.isValid() || !m_textureArrayView.isValid() || !m_linearSampler.isValid() ||
        !m_nearestSampler.isValid() || !m_vertexShader.isValid() || !m_forwardFragmentShader.isValid() ||
        !m_deferredFragmentShader.isValid() || !m_forwardBindGroupLayout.isValid() ||
        !m_deferredBindGroupLayout.isValid() || !m_forwardPipelineLayout.isValid() ||
        !m_deferredPipelineLayout.isValid() || !m_forwardPipeline.isValid() ||
        !m_deferredPipeline.isValid() || !m_forwardBindGroup.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool ParticleSystem::ensureDeferredBindGroup(const RhiTextureHandle voxelLightTexture,
                                             const RhiTextureHandle depthTexture) {
    if (!voxelLightTexture.isValid() || !depthTexture.isValid()) return false;
    if (m_deferredBindGroup.isValid() && sameTexture(m_boundVoxelLightTexture, voxelLightTexture) &&
        sameTexture(m_boundDepthTexture, depthTexture)) return true;
    destroyDeferredBindGroup();
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = voxelLightTexture;
    m_voxelLightView = m_rhiDevice->createTextureView(viewDesc);
    viewDesc.texture = depthTexture;
    m_depthView = m_rhiDevice->createTextureView(viewDesc);
    if (!m_voxelLightView.isValid() || !m_depthView.isValid()) {
        destroyDeferredBindGroup();
        return false;
    }
    RhiBindGroupDesc desc;
    desc.layout = m_deferredBindGroupLayout;
    const RhiTextureViewHandle views[] = {m_textureArrayView, m_voxelLightView, m_depthView};
    const RhiSamplerHandle samplers[] = {m_linearSampler, m_linearSampler, m_nearestSampler};
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {views[binding], samplers[binding]};
        desc.entries.push_back(entry);
    }
    m_deferredBindGroup = m_rhiDevice->createBindGroup(desc);
    if (!m_deferredBindGroup.isValid()) {
        destroyDeferredBindGroup();
        return false;
    }
    m_boundVoxelLightTexture = voxelLightTexture;
    m_boundDepthTexture = depthTexture;
    return true;
}

void ParticleSystem::destroyDeferredBindGroup() {
    if (m_rhiDevice) {
        if (m_deferredBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_deferredBindGroup);
        if (m_depthView.isValid()) m_rhiDevice->destroyTextureView(m_depthView);
        if (m_voxelLightView.isValid()) m_rhiDevice->destroyTextureView(m_voxelLightView);
    }
    m_deferredBindGroup = {};
    m_depthView = {};
    m_voxelLightView = {};
    m_boundVoxelLightTexture = {};
    m_boundDepthTexture = {};
}

void ParticleSystem::destroyRhiResources() {
    destroyDeferredBindGroup();
    if (m_rhiDevice) {
        if (m_forwardBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_forwardBindGroup);
        if (m_deferredPipeline.isValid()) m_rhiDevice->destroyPipeline(m_deferredPipeline);
        if (m_forwardPipeline.isValid()) m_rhiDevice->destroyPipeline(m_forwardPipeline);
        if (m_deferredPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_deferredPipelineLayout);
        if (m_forwardPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_forwardPipelineLayout);
        if (m_deferredBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_deferredBindGroupLayout);
        if (m_forwardBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_forwardBindGroupLayout);
        if (m_deferredFragmentShader.isValid()) m_rhiDevice->destroyShader(m_deferredFragmentShader);
        if (m_forwardFragmentShader.isValid()) m_rhiDevice->destroyShader(m_forwardFragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
        if (m_nearestSampler.isValid()) m_rhiDevice->destroySampler(m_nearestSampler);
        if (m_linearSampler.isValid()) m_rhiDevice->destroySampler(m_linearSampler);
        if (m_textureArrayView.isValid()) m_rhiDevice->destroyTextureView(m_textureArrayView);
        if (m_rhiVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_rhiVertexBuffer);
    }
    m_forwardBindGroup = {};
    m_deferredPipeline = {};
    m_forwardPipeline = {};
    m_deferredPipelineLayout = {};
    m_forwardPipelineLayout = {};
    m_deferredBindGroupLayout = {};
    m_forwardBindGroupLayout = {};
    m_deferredFragmentShader = {};
    m_forwardFragmentShader = {};
    m_vertexShader = {};
    m_nearestSampler = {};
    m_linearSampler = {};
    m_textureArrayView = {};
    m_rhiVertexBuffer = {};
    m_rhiDevice = nullptr;
}
