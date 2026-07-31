#include "ReflectionProbeCapturePass.h"

#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

using renderer::contracts::GpuReflectionProbe;
using renderer::contracts::ReflectionProbeCaptureWorkKind;

struct ReflectionProbePrefilterPushConstants final {
    uint32_t face = 0u;
    float roughness = 0.0f;
    uint32_t sourceResolution = renderer::contracts::kReflectionProbeCubeExtent;
    uint32_t sampleCount = renderer::contracts::kReflectionProbeGgxSampleCount;
};

[[nodiscard]] bool sameVec3(const glm::vec3& lhs, const glm::vec3& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool sameSourceSpatial(const ReflectionProbeCaptureSource& lhs, const ReflectionProbeCaptureSource& rhs) {
    return lhs.probeId.value == rhs.probeId.value && sameVec3(lhs.positionWorldMeters, rhs.positionWorldMeters) &&
           lhs.exposureScale == rhs.exposureScale &&
           sameVec3(lhs.influenceMinWorldMeters, rhs.influenceMinWorldMeters) &&
           sameVec3(lhs.influenceMaxWorldMeters, rhs.influenceMaxWorldMeters) &&
           lhs.blendDistanceMeters == rhs.blendDistanceMeters &&
           sameVec3(lhs.boxProjectionMinWorldMeters, rhs.boxProjectionMinWorldMeters) &&
           sameVec3(lhs.boxProjectionMaxWorldMeters, rhs.boxProjectionMaxWorldMeters);
}

[[nodiscard]] renderer::contracts::ReflectionProbeNormalizationInput
normalizationInput(const ReflectionProbeCaptureSource& source, const glm::vec3& cameraPositionWorld,
                   const float validity, const uint32_t cubemapIndex, const uint32_t captureRevision) {
    renderer::contracts::ReflectionProbeNormalizationInput input;
    input.probeId = source.probeId;
    input.positionMeters = source.positionWorldMeters - cameraPositionWorld;
    input.exposureScale = source.exposureScale;
    input.influenceMinMeters = source.influenceMinWorldMeters - cameraPositionWorld;
    input.influenceMaxMeters = source.influenceMaxWorldMeters - cameraPositionWorld;
    input.blendDistanceMeters = source.blendDistanceMeters;
    input.boxProjectionMinMeters = source.boxProjectionMinWorldMeters - cameraPositionWorld;
    input.boxProjectionMaxMeters = source.boxProjectionMaxWorldMeters - cameraPositionWorld;
    input.validity = validity;
    input.prefilteredCubemapIndex = cubemapIndex;
    input.captureRevision = captureRevision;
    return input;
}

[[nodiscard]] float captureFarPlane(const ReflectionProbeCaptureSource& source) {
    float maximumDistanceSquared = 0.0f;
    for (uint32_t corner = 0u; corner < 8u; ++corner) {
        const glm::vec3 point{
            (corner & 1u) != 0u ? source.boxProjectionMaxWorldMeters.x : source.boxProjectionMinWorldMeters.x,
            (corner & 2u) != 0u ? source.boxProjectionMaxWorldMeters.y : source.boxProjectionMinWorldMeters.y,
            (corner & 4u) != 0u ? source.boxProjectionMaxWorldMeters.z : source.boxProjectionMinWorldMeters.z};
        const glm::vec3 delta = point - source.positionWorldMeters;
        maximumDistanceSquared = std::max(maximumDistanceSquared, glm::dot(delta, delta));
    }
    return std::sqrt(maximumDistanceSquared);
}

[[nodiscard]] glm::mat4 captureViewProjection(const ReflectionProbeCaptureSource& source, const uint32_t face) {
    constexpr std::array<glm::vec3, renderer::contracts::kReflectionProbeCubeFaceCount> kDirections{
        {{1.0f, 0.0f, 0.0f},
         {-1.0f, 0.0f, 0.0f},
         {0.0f, 1.0f, 0.0f},
         {0.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.0f, 0.0f, -1.0f}}};
    constexpr std::array<glm::vec3, renderer::contracts::kReflectionProbeCubeFaceCount> kUpVectors{
        {{0.0f, -1.0f, 0.0f},
         {0.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {0.0f, 0.0f, -1.0f},
         {0.0f, -1.0f, 0.0f},
         {0.0f, -1.0f, 0.0f}}};
    const float farPlane = captureFarPlane(source);
    const glm::mat4 projection = glm::perspective(
        glm::half_pi<float>(), 1.0f, renderer::contracts::kReflectionProbeCaptureNearPlaneMeters, farPlane);
    const glm::mat4 view =
        glm::lookAt(source.positionWorldMeters, source.positionWorldMeters + kDirections[face], kUpVectors[face]);
    return projection * view;
}

} // namespace

void ReflectionProbeCapturePass::setSources(std::vector<ReflectionProbeCaptureSource> sources) {
    std::sort(sources.begin(), sources.end(),
              [](const ReflectionProbeCaptureSource& lhs, const ReflectionProbeCaptureSource& rhs) {
                  return lhs.probeId.value < rhs.probeId.value;
              });
    bool unchanged = sources.size() == m_sources.size();
    for (uint32_t index = 0u; unchanged && index < static_cast<uint32_t>(sources.size()); ++index) {
        unchanged = sameSourceSpatial(sources[index], m_sources[index]) &&
                    sources[index].requestedRevision == m_sources[index].requestedRevision;
    }
    if (unchanged) {
        return;
    }
    m_sources = std::move(sources);
    ++m_sourceRevision;
}

bool ReflectionProbeCapturePass::prepareFrame(RhiDevice& rhiDevice, const glm::vec3& cameraPositionWorld) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyResources();
        for (ProbeState& state : m_states) {
            state.active = false;
            state.building = false;
            state.activeSlot = renderer::contracts::kReflectionProbeInvalidCubemapIndex;
            state.buildSlot = renderer::contracts::kReflectionProbeInvalidCubemapIndex;
            state.activeRevision = 0u;
            state.buildRevision = 0u;
            state.nextWorkItem = 0u;
        }
    }
    m_rhiDevice = &rhiDevice;
    m_cameraPositionWorld = cameraPositionWorld;
    m_lastError.clear();

    if (m_sources.size() > renderer::contracts::kReflectionProbeCaptureMaxProbeCount) {
        m_lastError = "ProbeCapacityExceeded";
        return false;
    }
    if (m_preparedRevision != m_sourceRevision && !rebuildSources(cameraPositionWorld)) {
        return false;
    }

    if (!m_states.empty()) {
        const uint32_t requiredSlotCapacity =
            static_cast<uint32_t>(m_states.size()) * renderer::contracts::kReflectionProbeCaptureSlotCount;
        if (!ensureResources(rhiDevice, requiredSlotCapacity) || !createPipelines(rhiDevice)) {
            return false;
        }
    }

    m_activeProbes.clear();
    m_activeProbes.reserve(m_states.size());
    for (const ProbeState& state : m_states) {
        if (!state.active) {
            continue;
        }
        GpuReflectionProbe probe;
        if (!buildActiveProbe(state, cameraPositionWorld, probe)) {
            return false;
        }
        m_activeProbes.push_back(probe);
    }
    return true;
}

bool ReflectionProbeCapturePass::rebuildSources(const glm::vec3& cameraPositionWorld) {
    std::vector<ReflectionProbeCaptureSource> sorted = m_sources;
    std::sort(sorted.begin(), sorted.end(),
              [](const ReflectionProbeCaptureSource& lhs, const ReflectionProbeCaptureSource& rhs) {
                  return lhs.probeId.value < rhs.probeId.value;
              });
    for (uint32_t index = 0u; index < static_cast<uint32_t>(sorted.size()); ++index) {
        const ReflectionProbeCaptureSource& source = sorted[index];
        if (source.requestedRevision == 0u) {
            m_lastError = "InvalidCaptureRevision";
            return false;
        }
        if (index > 0u && sorted[index - 1u].probeId.value == source.probeId.value) {
            m_lastError = "DuplicateStableId";
            return false;
        }
        if (captureFarPlane(source) <= renderer::contracts::kReflectionProbeCaptureNearPlaneMeters) {
            m_lastError = "CaptureRangeTooSmall";
            return false;
        }
        const auto normalized = renderer::contracts::normalizeReflectionProbe(normalizationInput(
            source, cameraPositionWorld, 0.0f, renderer::contracts::kReflectionProbeInvalidCubemapIndex, 0u));
        if (!normalized.succeeded()) {
            m_lastError = renderer::contracts::reflectionProbeErrorStableId(normalized.error);
            m_lastError += ":";
            m_lastError += renderer::contracts::reflectionProbeFieldStableId(normalized.field);
            return false;
        }
    }

    bool topologyChanged = sorted.size() != m_states.size();
    if (!topologyChanged) {
        for (uint32_t index = 0u; index < static_cast<uint32_t>(sorted.size()); ++index) {
            if (sorted[index].probeId.value != m_states[index].source.probeId.value) {
                topologyChanged = true;
                break;
            }
        }
    }

    if (topologyChanged) {
        m_states.clear();
        m_states.reserve(sorted.size());
        for (uint32_t index = 0u; index < static_cast<uint32_t>(sorted.size()); ++index) {
            ProbeState state;
            state.source = sorted[index];
            state.slotBase = index * renderer::contracts::kReflectionProbeCaptureSlotCount;
            state.buildSlot = 0u;
            state.buildRevision = sorted[index].requestedRevision;
            state.nextWorkItem = 0u;
            state.building = true;
            m_states.push_back(state);
        }
        m_queueCursor = 0u;
    } else {
        for (uint32_t index = 0u; index < static_cast<uint32_t>(sorted.size()); ++index) {
            ProbeState& state = m_states[index];
            const ReflectionProbeCaptureSource& source = sorted[index];
            const bool spatialChanged = !sameSourceSpatial(source, state.source);
            const uint32_t currentRevision = state.building ? state.buildRevision : state.activeRevision;
            if (source.requestedRevision < currentRevision) {
                m_lastError = "CaptureRevisionRegressed";
                return false;
            }
            if (spatialChanged && source.requestedRevision == currentRevision) {
                m_lastError = "SourceChangedWithoutRevision";
                return false;
            }
            state.source = source;
            if (source.requestedRevision > currentRevision) {
                state.buildSlot = state.active ? 1u - state.activeSlot : 0u;
                state.buildRevision = source.requestedRevision;
                state.nextWorkItem = 0u;
                state.building = true;
            }
        }
    }

    m_sources = std::move(sorted);
    m_preparedRevision = m_sourceRevision;
    return true;
}

bool ReflectionProbeCapturePass::ensureResources(RhiDevice& rhiDevice, const uint32_t requiredSlotCapacity) {
    if (requiredSlotCapacity == 0u) {
        m_lastError = "InvalidSlotCapacity";
        return false;
    }
    if (m_slotCapacity == requiredSlotCapacity && m_radianceTexture.isValid() && m_prefilteredTexture.isValid() &&
        m_depthTexture.isValid() && m_radianceView.isValid() && m_prefilteredView.isValid() && m_depthView.isValid()) {
        return true;
    }

    destroyResources();
    m_rhiDevice = &rhiDevice;
    for (ProbeState& state : m_states) {
        state.active = false;
        state.activeSlot = renderer::contracts::kReflectionProbeInvalidCubemapIndex;
        state.activeRevision = 0u;
        state.buildSlot = 0u;
        state.buildRevision = state.source.requestedRevision;
        state.nextWorkItem = 0u;
        state.building = true;
    }

    const auto createTexture = [&rhiDevice](const char* name, const uint32_t layers, const uint32_t mipLevels) {
        RhiTextureDesc desc;
        desc.debugName = name;
        desc.dimension = RhiTextureDimension::CubeArray;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = renderer::contracts::kReflectionProbeCubeExtent;
        desc.height = renderer::contracts::kReflectionProbeCubeExtent;
        desc.depthOrLayers = layers;
        desc.mipLevels = mipLevels;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment);
        desc.memoryCategory = RhiMemoryCategory::SceneData;
        desc.queueSharing = RhiTextureQueueSharing::GraphicsComputeConcurrent;
        return rhiDevice.createTexture(desc, nullptr);
    };
    const uint32_t layerCount = requiredSlotCapacity * renderer::contracts::kReflectionProbeCubeFaceCount;
    m_radianceTexture = createTexture("ReflectionProbeCapture.Radiance", layerCount, 1u);
    m_prefilteredTexture = createTexture("ReflectionProbeCapture.Prefiltered", layerCount,
                                         renderer::contracts::kReflectionProbeCubeMipCount);
    RhiTextureDesc depthDesc;
    depthDesc.debugName = "ReflectionProbeCapture.Depth";
    depthDesc.dimension = RhiTextureDimension::Texture2D;
    depthDesc.format = RhiTextureFormat::Depth32Float;
    depthDesc.width = renderer::contracts::kReflectionProbeCubeExtent;
    depthDesc.height = renderer::contracts::kReflectionProbeCubeExtent;
    depthDesc.depthOrLayers = 1u;
    depthDesc.mipLevels = 1u;
    depthDesc.usage = rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    depthDesc.memoryCategory = RhiMemoryCategory::SceneData;
    m_depthTexture = rhiDevice.createTexture(depthDesc, nullptr);
    if (!m_radianceTexture.isValid() || !m_prefilteredTexture.isValid() || !m_depthTexture.isValid()) {
        m_lastError = "CaptureTextureCreationFailed";
        destroyResources();
        m_rhiDevice = &rhiDevice;
        return false;
    }
    m_slotCapacity = requiredSlotCapacity;
    if (!createViews(rhiDevice, requiredSlotCapacity)) {
        const std::string error = m_lastError;
        destroyResources();
        m_rhiDevice = &rhiDevice;
        m_lastError = error;
        return false;
    }
    return true;
}

bool ReflectionProbeCapturePass::createViews(RhiDevice& rhiDevice, const uint32_t slotCapacity) {
    const auto createArrayView = [&rhiDevice](const RhiTextureHandle texture, const uint32_t mipCount,
                                              const uint32_t layerCount) {
        RhiTextureViewDesc desc;
        desc.texture = texture;
        desc.viewType = RhiTextureViewType::CubeArray;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0u;
        desc.mipCount = mipCount;
        desc.baseLayer = 0u;
        desc.layerCount = layerCount;
        return rhiDevice.createTextureView(desc);
    };
    const uint32_t layerCount = slotCapacity * renderer::contracts::kReflectionProbeCubeFaceCount;
    m_radianceView = createArrayView(m_radianceTexture, 1u, layerCount);
    m_prefilteredView =
        createArrayView(m_prefilteredTexture, renderer::contracts::kReflectionProbeCubeMipCount, layerCount);
    RhiTextureViewDesc depthViewDesc;
    depthViewDesc.texture = m_depthTexture;
    depthViewDesc.viewType = RhiTextureViewType::Texture2D;
    depthViewDesc.format = RhiTextureFormat::Depth32Float;
    m_depthView = rhiDevice.createTextureView(depthViewDesc);
    if (!m_radianceView.isValid() || !m_prefilteredView.isValid() || !m_depthView.isValid()) {
        m_lastError = "CaptureArrayViewCreationFailed";
        return false;
    }

    m_radianceFaceViews.assign(slotCapacity,
                               std::vector<RhiTextureViewHandle>(renderer::contracts::kReflectionProbeCubeFaceCount));
    m_prefilterFaceMipViews.assign(
        slotCapacity, std::vector<std::vector<RhiTextureViewHandle>>(
                          renderer::contracts::kReflectionProbeCubeMipCount,
                          std::vector<RhiTextureViewHandle>(renderer::contracts::kReflectionProbeCubeFaceCount)));
    for (uint32_t slot = 0u; slot < slotCapacity; ++slot) {
        for (uint32_t face = 0u; face < renderer::contracts::kReflectionProbeCubeFaceCount; ++face) {
            RhiTextureViewDesc desc;
            desc.texture = m_radianceTexture;
            desc.viewType = RhiTextureViewType::Texture2D;
            desc.format = RhiTextureFormat::Rgba16Float;
            desc.baseMip = 0u;
            desc.mipCount = 1u;
            desc.baseLayer = slot * renderer::contracts::kReflectionProbeCubeFaceCount + face;
            desc.layerCount = 1u;
            m_radianceFaceViews[slot][face] = rhiDevice.createTextureView(desc);
            if (!m_radianceFaceViews[slot][face].isValid()) {
                m_lastError = "RadianceFaceViewCreationFailed";
                return false;
            }
            desc.texture = m_prefilteredTexture;
            for (uint32_t mip = 0u; mip < renderer::contracts::kReflectionProbeCubeMipCount; ++mip) {
                desc.baseMip = mip;
                m_prefilterFaceMipViews[slot][mip][face] = rhiDevice.createTextureView(desc);
                if (!m_prefilterFaceMipViews[slot][mip][face].isValid()) {
                    m_lastError = "PrefilterFaceViewCreationFailed";
                    return false;
                }
            }
        }
    }
    return true;
}

bool ReflectionProbeCapturePass::createPipelines(RhiDevice& rhiDevice) {
    if (m_prefilterPipeline.isValid()) {
        return true;
    }
    const auto vertex = renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const auto fragment = renderer::rhi::loadShaderSource("assets/shaders/reflection_probe_prefilter.frag");
    if (!vertex || !fragment) {
        const std::string error = "CaptureShaderLoadFailed";
        destroyResources();
        m_rhiDevice = &rhiDevice;
        m_lastError = error;
        return false;
    }
    const auto createShader = [&rhiDevice](const char* name, const RhiShaderStage stage, const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = name;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return rhiDevice.createShader(desc);
    };
    m_fullscreenVertexShader = createShader("ReflectionProbeCapture.Prefilter.Vertex", RhiShaderStage::Vertex, *vertex);
    m_prefilterFragmentShader =
        createShader("ReflectionProbeCapture.Prefilter.Fragment", RhiShaderStage::Fragment, *fragment);
    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_linearClampSampler = rhiDevice.createSampler(samplerDesc);
    RhiBindGroupLayoutDesc groupLayout;
    groupLayout.debugName = "ReflectionProbeCapture.Prefilter.BindGroupLayout";
    groupLayout.entries.push_back({0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_prefilterBindGroupLayout = rhiDevice.createBindGroupLayout(groupLayout);
    RhiPipelineLayoutDesc pipelineLayout;
    pipelineLayout.debugName = "ReflectionProbeCapture.Prefilter.PipelineLayout";
    pipelineLayout.bindGroupLayouts.push_back(m_prefilterBindGroupLayout);
    pipelineLayout.pushConstantBytes = sizeof(ReflectionProbePrefilterPushConstants);
    pipelineLayout.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_prefilterPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayout);
    RhiGraphicsPipelineDesc pipeline;
    pipeline.debugName = "ReflectionProbeCapture.Prefilter.Pipeline";
    pipeline.vertexShader = m_fullscreenVertexShader;
    pipeline.fragmentShader = m_prefilterFragmentShader;
    pipeline.layout = m_prefilterPipelineLayout;
    pipeline.raster.cullMode = RhiCullMode::None;
    pipeline.depthStencil.depthTestEnabled = false;
    pipeline.depthStencil.depthWriteEnabled = false;
    pipeline.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipeline.blend.attachments.push_back({});
    m_prefilterPipeline = rhiDevice.createGraphicsPipeline(pipeline);
    if (!m_fullscreenVertexShader.isValid() || !m_prefilterFragmentShader.isValid() ||
        !m_linearClampSampler.isValid() || !m_prefilterBindGroupLayout.isValid() ||
        !m_prefilterPipelineLayout.isValid() || !m_prefilterPipeline.isValid()) {
        const std::string error = "CapturePipelineCreationFailed";
        destroyResources();
        m_rhiDevice = &rhiDevice;
        m_lastError = error;
        return false;
    }

    m_radianceCubeViews.resize(m_slotCapacity);
    m_prefilterBindGroups.resize(m_slotCapacity);
    for (uint32_t slot = 0u; slot < m_slotCapacity; ++slot) {
        RhiTextureViewDesc cubeViewDesc;
        cubeViewDesc.texture = m_radianceTexture;
        cubeViewDesc.viewType = RhiTextureViewType::Cube;
        cubeViewDesc.format = RhiTextureFormat::Rgba16Float;
        cubeViewDesc.baseMip = 0u;
        cubeViewDesc.mipCount = 1u;
        cubeViewDesc.baseLayer = slot * renderer::contracts::kReflectionProbeCubeFaceCount;
        cubeViewDesc.layerCount = renderer::contracts::kReflectionProbeCubeFaceCount;
        m_radianceCubeViews[slot] = rhiDevice.createTextureView(cubeViewDesc);
        if (!m_radianceCubeViews[slot].isValid()) {
            const std::string error = "RadianceCubeViewCreationFailed";
            destroyResources();
            m_rhiDevice = &rhiDevice;
            m_lastError = error;
            return false;
        }
        RhiBindGroupDesc group;
        group.layout = m_prefilterBindGroupLayout;
        RhiBindGroupEntry entry;
        entry.binding = 0u;
        entry.resource.combinedTextureSampler = {m_radianceCubeViews[slot], m_linearClampSampler};
        group.entries.push_back(entry);
        m_prefilterBindGroups[slot] = rhiDevice.createBindGroup(group);
        if (!m_prefilterBindGroups[slot].isValid()) {
            const std::string error = "PrefilterBindGroupCreationFailed";
            destroyResources();
            m_rhiDevice = &rhiDevice;
            m_lastError = error;
            return false;
        }
    }
    return true;
}

bool ReflectionProbeCapturePass::importGraphResources(RenderGraph& graph, GraphResources& resources) const {
    if (m_states.empty()) {
        return true;
    }
    if (m_rhiDevice == nullptr || !m_radianceTexture.isValid() || !m_prefilteredTexture.isValid() ||
        !m_depthTexture.isValid() || !m_radianceView.isValid() || !m_prefilteredView.isValid() ||
        !m_depthView.isValid()) {
        return false;
    }
    const auto import = [&graph, this](const char* name, const RhiTextureHandle texture,
                                       const RhiTextureViewHandle view, const bool initialized,
                                       RgTextureHandle& output) {
        RhiTextureDesc desc;
        if (!m_rhiDevice->getTextureDesc(texture, desc)) {
            return false;
        }
        output = graph.importTexture({name, texture, desc,
                                      initialized ? RhiResourceState::ShaderRead : RhiResourceState::Undefined,
                                      RhiResourceState::ShaderRead, view});
        return output.isValid();
    };
    if (!import("ReflectionProbeCapture.Radiance", m_radianceTexture, m_radianceView, m_radianceInitialized,
                resources.radiance) ||
        !import("ReflectionProbeCapture.Prefiltered", m_prefilteredTexture, m_prefilteredView, m_prefilteredInitialized,
                resources.prefiltered)) {
        return false;
    }
    RhiTextureDesc depthDesc;
    if (!m_rhiDevice->getTextureDesc(m_depthTexture, depthDesc)) {
        return false;
    }
    resources.depth =
        graph.importTexture({"ReflectionProbeCapture.Depth", m_depthTexture, depthDesc,
                             m_depthInitialized ? RhiResourceState::DepthWrite : RhiResourceState::Undefined,
                             RhiResourceState::DepthWrite, m_depthView});
    return resources.depth.isValid();
}

RgPassHandle ReflectionProbeCapturePass::addGraphPasses(RenderGraph& graph, const GraphResources& resources,
                                                        const FrameContext& context, const RgPassHandle dependency) {
    m_workScheduled = false;
    if (!dependency.isValid()) {
        return {};
    }
    if (!hasPendingWork()) {
        return dependency;
    }
    if (!resources.radiance.isValid() || !resources.prefiltered.isValid() || !resources.depth.isValid()) {
        m_lastError = "CaptureGraphResourcesMissing";
        return {};
    }

    uint32_t selected = static_cast<uint32_t>(m_states.size());
    for (uint32_t offset = 0u; offset < static_cast<uint32_t>(m_states.size()); ++offset) {
        const uint32_t index = (m_queueCursor + offset) % static_cast<uint32_t>(m_states.size());
        if (m_states[index].building) {
            selected = index;
            break;
        }
    }
    if (selected >= m_states.size()) {
        return dependency;
    }
    const ProbeState& state = m_states[selected];
    m_scheduledProbeIndex = selected;
    m_scheduledWorkItem = state.nextWorkItem;
    const ReflectionProbeCaptureWorkKind kind = renderer::contracts::reflectionProbeCaptureWorkKind(state.nextWorkItem);
    RenderGraphPassBuilder pass =
        graph.addPass({kind == ReflectionProbeCaptureWorkKind::RadianceFace ? "ReflectionProbeCapture.RadianceFace"
                                                                            : "ReflectionProbeCapture.PrefilterFaceMip",
                       RgPassType::Graphics, RhiQueueType::Graphics, true});
    pass.dependsOn(dependency);
    if (kind == ReflectionProbeCaptureWorkKind::RadianceFace) {
        if (m_captureRenderer == nullptr) {
            m_lastError = "CaptureRendererMissing";
            return {};
        }
        pass.writeTexture(resources.radiance, RhiResourceState::RenderTarget)
            .writeTexture(resources.depth, RhiResourceState::DepthWrite)
            .setExecute([this, &context](RgPassContext& graphPass) {
                return recordRadianceFace(graphPass.commandList(), context);
            });
    } else {
        pass.readTexture(resources.radiance, RhiResourceState::ShaderRead)
            .writeTexture(resources.prefiltered, RhiResourceState::RenderTarget)
            .setExecute([this](RgPassContext& graphPass) { return recordPrefilter(graphPass.commandList()); });
    }
    m_queueCursor = (selected + 1u) % static_cast<uint32_t>(m_states.size());
    m_workScheduled = true;
    return pass.handle();
}

ReflectionProbeCaptureWork ReflectionProbeCapturePass::buildWork(const ProbeState& state, const uint32_t probeIndex,
                                                                 const uint32_t workItem) const {
    ReflectionProbeCaptureWork work;
    work.probeIndex = probeIndex;
    work.cubemapIndex = state.slotBase + state.buildSlot;
    work.workItem = workItem;
    work.face = renderer::contracts::reflectionProbeCaptureFace(workItem);
    work.mip = renderer::contracts::reflectionProbeCaptureMip(workItem);
    work.positionWorldMeters = state.source.positionWorldMeters;
    work.viewProjection = captureViewProjection(state.source, work.face);
    work.depthTargetView = m_depthView;
    if (renderer::contracts::reflectionProbeCaptureWorkKind(workItem) == ReflectionProbeCaptureWorkKind::RadianceFace) {
        work.targetView = m_radianceFaceViews[work.cubemapIndex][work.face];
    } else {
        work.targetView = m_prefilterFaceMipViews[work.cubemapIndex][work.mip][work.face];
    }
    return work;
}

bool ReflectionProbeCapturePass::recordRadianceFace(RhiCommandList& commandList, const FrameContext& context) const {
    if (!m_workScheduled || m_captureRenderer == nullptr || m_scheduledProbeIndex >= m_states.size()) {
        return false;
    }
    const ProbeState& state = m_states[m_scheduledProbeIndex];
    if (!state.building || renderer::contracts::reflectionProbeCaptureWorkKind(m_scheduledWorkItem) !=
                               ReflectionProbeCaptureWorkKind::RadianceFace) {
        return false;
    }
    return m_captureRenderer->recordReflectionProbeRadianceFace(
        commandList, context, buildWork(state, m_scheduledProbeIndex, m_scheduledWorkItem));
}

bool ReflectionProbeCapturePass::recordPrefilter(RhiCommandList& commandList) const {
    if (!m_workScheduled || m_scheduledProbeIndex >= m_states.size()) {
        return false;
    }
    const ProbeState& state = m_states[m_scheduledProbeIndex];
    if (!state.building || renderer::contracts::reflectionProbeCaptureWorkKind(m_scheduledWorkItem) !=
                               ReflectionProbeCaptureWorkKind::PrefilterFaceMip) {
        return false;
    }
    const ReflectionProbeCaptureWork work = buildWork(state, m_scheduledProbeIndex, m_scheduledWorkItem);
    if (work.cubemapIndex >= m_prefilterBindGroups.size() || !work.targetView.isValid() ||
        !m_prefilterBindGroups[work.cubemapIndex].isValid()) {
        return false;
    }
    const uint32_t extent = std::max(1u, renderer::contracts::kReflectionProbeCubeExtent >> work.mip);
    RhiColorAttachment attachment{work.targetView, RhiLoadOp::Clear, RhiStoreOp::Store};
    RhiRenderingInfo rendering{
        "ReflectionProbeCapture.Prefilter.FaceMip", {0, 0, extent, extent}, &attachment, 1u, nullptr};
    commandList.beginRendering(rendering);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(extent), static_cast<float>(extent), 0.0f, 1.0f});
    commandList.setScissor(rendering.renderArea);
    commandList.setGraphicsPipeline(m_prefilterPipeline);
    commandList.setBindGroup(0u, m_prefilterBindGroups[work.cubemapIndex]);
    ReflectionProbePrefilterPushConstants constants;
    constants.face = work.face;
    constants.roughness = renderer::contracts::reflectionProbeRoughnessForMip(work.mip);
    commandList.pushConstants(&constants, sizeof(constants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    return true;
}

void ReflectionProbeCapturePass::finishGraphExecution(const bool succeeded) {
    if (m_workScheduled && succeeded && m_scheduledProbeIndex < m_states.size()) {
        ProbeState& state = m_states[m_scheduledProbeIndex];
        if (state.building && state.nextWorkItem == m_scheduledWorkItem) {
            const ReflectionProbeCaptureWorkKind kind =
                renderer::contracts::reflectionProbeCaptureWorkKind(m_scheduledWorkItem);
            if (kind == ReflectionProbeCaptureWorkKind::RadianceFace) {
                m_radianceInitialized = true;
                m_depthInitialized = true;
            } else {
                m_prefilteredInitialized = true;
            }
            ++state.nextWorkItem;
            if (state.nextWorkItem == renderer::contracts::kReflectionProbeCaptureWorkItemCount) {
                state.active = true;
                state.activeSource = state.source;
                state.activeSlot = state.buildSlot;
                state.activeRevision = state.buildRevision;
                state.buildSlot = renderer::contracts::kReflectionProbeInvalidCubemapIndex;
                state.buildRevision = 0u;
                state.nextWorkItem = 0u;
                state.building = false;
            }
        }
    }
    m_workScheduled = false;
}

bool ReflectionProbeCapturePass::buildActiveProbe(const ProbeState& state, const glm::vec3& cameraPositionWorld,
                                                  GpuReflectionProbe& probe) const {
    const auto normalized = renderer::contracts::normalizeReflectionProbe(normalizationInput(
        state.activeSource, cameraPositionWorld, 1.0f, state.slotBase + state.activeSlot, state.activeRevision));
    if (!normalized.succeeded()) {
        return false;
    }
    probe = normalized.probe;
    return true;
}

ReflectionProbeCapturePass::ConsumerResources ReflectionProbeCapturePass::consumerResources() const {
    ConsumerResources resources;
    resources.prefilteredTexture = m_prefilteredTexture;
    resources.prefilteredView = m_prefilteredView;
    resources.slotCapacity = m_slotCapacity;
    return resources;
}

bool ReflectionProbeCapturePass::hasPendingWork() const {
    return std::any_of(m_states.begin(), m_states.end(), [](const ProbeState& state) { return state.building; });
}

ReflectionProbeCaptureFrameStats ReflectionProbeCapturePass::frameStats() const {
    ReflectionProbeCaptureFrameStats stats;
    stats.sourceCount = static_cast<uint32_t>(m_sources.size());
    stats.activeProbeCount = static_cast<uint32_t>(m_activeProbes.size());
    stats.slotCapacity = m_slotCapacity;
    stats.workScheduled = m_workScheduled;
    uint32_t selected = static_cast<uint32_t>(m_states.size());
    for (uint32_t index = 0u; index < static_cast<uint32_t>(m_states.size()); ++index) {
        const ProbeState& state = m_states[index];
        if (!state.building) {
            continue;
        }
        ++stats.buildingProbeCount;
        stats.pendingWorkItemCount += renderer::contracts::kReflectionProbeCaptureWorkItemCount - state.nextWorkItem;
    }
    if (m_workScheduled && m_scheduledProbeIndex < m_states.size()) {
        selected = m_scheduledProbeIndex;
    } else if (!m_states.empty()) {
        for (uint32_t offset = 0u; offset < static_cast<uint32_t>(m_states.size()); ++offset) {
            const uint32_t index = (m_queueCursor + offset) % static_cast<uint32_t>(m_states.size());
            if (m_states[index].building) {
                selected = index;
                break;
            }
        }
    }
    if (selected >= m_states.size()) {
        return stats;
    }
    const ProbeState& state = m_states[selected];
    stats.currentProbeId = state.source.probeId;
    stats.currentWorkItem = m_workScheduled ? m_scheduledWorkItem : state.nextWorkItem;
    stats.activeCubemapIndex =
        state.active ? state.slotBase + state.activeSlot : renderer::contracts::kReflectionProbeInvalidCubemapIndex;
    stats.buildCubemapIndex =
        state.building ? state.slotBase + state.buildSlot : renderer::contracts::kReflectionProbeInvalidCubemapIndex;
    stats.activeRevision = state.activeRevision;
    stats.buildRevision = state.buildRevision;
    return stats;
}

void ReflectionProbeCapturePass::destroyResources() {
    if (m_rhiDevice != nullptr) {
        for (const RhiBindGroupHandle group : m_prefilterBindGroups) {
            if (group.isValid()) {
                m_rhiDevice->destroyBindGroup(group);
            }
        }
        if (m_prefilterPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_prefilterPipeline);
        }
        if (m_prefilterPipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_prefilterPipelineLayout);
        }
        if (m_prefilterBindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_prefilterBindGroupLayout);
        }
        if (m_linearClampSampler.isValid()) {
            m_rhiDevice->destroySampler(m_linearClampSampler);
        }
        if (m_prefilterFragmentShader.isValid()) {
            m_rhiDevice->destroyShader(m_prefilterFragmentShader);
        }
        if (m_fullscreenVertexShader.isValid()) {
            m_rhiDevice->destroyShader(m_fullscreenVertexShader);
        }
        for (const auto& slotViews : m_radianceFaceViews) {
            for (const RhiTextureViewHandle view : slotViews) {
                if (view.isValid()) {
                    m_rhiDevice->destroyTextureView(view);
                }
            }
        }
        for (const RhiTextureViewHandle view : m_radianceCubeViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
        }
        for (const auto& slotMips : m_prefilterFaceMipViews) {
            for (const auto& mipViews : slotMips) {
                for (const RhiTextureViewHandle view : mipViews) {
                    if (view.isValid()) {
                        m_rhiDevice->destroyTextureView(view);
                    }
                }
            }
        }
        if (m_radianceView.isValid()) {
            m_rhiDevice->destroyTextureView(m_radianceView);
        }
        if (m_prefilteredView.isValid()) {
            m_rhiDevice->destroyTextureView(m_prefilteredView);
        }
        if (m_depthView.isValid()) {
            m_rhiDevice->destroyTextureView(m_depthView);
        }
        if (m_radianceTexture.isValid()) {
            m_rhiDevice->destroyTexture(m_radianceTexture);
        }
        if (m_prefilteredTexture.isValid()) {
            m_rhiDevice->destroyTexture(m_prefilteredTexture);
        }
        if (m_depthTexture.isValid()) {
            m_rhiDevice->destroyTexture(m_depthTexture);
        }
    }
    m_radianceTexture = {};
    m_prefilteredTexture = {};
    m_depthTexture = {};
    m_radianceView = {};
    m_prefilteredView = {};
    m_depthView = {};
    m_radianceFaceViews.clear();
    m_radianceCubeViews.clear();
    m_prefilterFaceMipViews.clear();
    m_prefilterBindGroups.clear();
    m_linearClampSampler = {};
    m_fullscreenVertexShader = {};
    m_prefilterFragmentShader = {};
    m_prefilterBindGroupLayout = {};
    m_prefilterPipelineLayout = {};
    m_prefilterPipeline = {};
    m_slotCapacity = 0u;
    m_radianceInitialized = false;
    m_prefilteredInitialized = false;
    m_depthInitialized = false;
    m_workScheduled = false;
}

void ReflectionProbeCapturePass::shutdown() {
    destroyResources();
    m_rhiDevice = nullptr;
    m_captureRenderer = nullptr;
    m_sources.clear();
    m_states.clear();
    m_activeProbes.clear();
    m_queueCursor = 0u;
    m_sourceRevision = 1u;
    m_preparedRevision = 0u;
    m_lastError.clear();
}
