#include "LocalShadowPass.h"

#include "renderer/core/IDeferredGeometryProvider.h"
#include "renderer/core/RenderScene.h"
#include "renderer/debug/RenderDebugService.h"
#include "renderer/mesh/TerrainRhiPipelineSet.h"
#include "renderer/mesh/WorldRenderBuffer.h"
#include "renderer/renderers/BlockEntityRenderer.h"
#include "renderer/renderers/DropRenderer.h"
#include "renderer/renderers/FallingBlockRenderer.h"
#include "renderer/renderers/HumanoidRenderer.h"
#include "renderer/renderers/StaticMeshRenderer.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"
#include "resource/ResourceMgr.h"
#include "world/DropSystem.h"
#include "world/IWorldView.h"
#include "ecs/GameplayRegistry.h"
#include "ecs/components/TagComponents.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace {
[[nodiscard]] uint32_t growPowerOfTwo(const uint32_t required, const uint32_t maximum) {
    uint32_t value = 1u;
    while (value < required && value < maximum) {
        value <<= 1u;
    }
    return value <= maximum ? value : 0u;
}

[[nodiscard]] uint32_t spotGridForSlotCount(const uint32_t slotCount) {
    uint32_t grid = 1u;
    while (grid * grid < std::max(slotCount, 1u) && grid < 8u) {
        grid <<= 1u;
    }
    return grid * grid >= std::max(slotCount, 1u) ? grid : 0u;
}

[[nodiscard]] bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] glm::vec3 shadowUpVector(const glm::vec3& direction) {
    return std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                                              : glm::vec3(0.0f, 0.0f, 1.0f);
}

[[nodiscard]] std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& viewProjection) {
    const glm::vec4 row0{viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]};
    const glm::vec4 row1{viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]};
    const glm::vec4 row2{viewProjection[0][2], viewProjection[1][2], viewProjection[2][2], viewProjection[3][2]};
    const glm::vec4 row3{viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]};
    std::array<glm::vec4, 6> planes{row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2};
    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        plane /= length;
    }
    return planes;
}

[[nodiscard]] bool nearlyEqual(const glm::vec4& lhs, const glm::vec4& rhs) {
    const glm::vec4 difference = glm::abs(lhs - rhs);
    return difference.x <= 1.0e-4f && difference.y <= 1.0e-4f && difference.z <= 1.0e-4f && difference.w <= 1.0e-4f;
}

[[nodiscard]] bool hasDynamicOccluders(const ecs::GameplayRegistry* gameplayRegistry) {
    if (gameplayRegistry == nullptr) {
        return false;
    }
    const entt::registry& registry = gameplayRegistry->registry();
    return !registry.view<ecs::SteveTag>().empty() || !registry.view<ecs::MobTag>().empty() ||
           !registry.view<ecs::DropItemTag>().empty() || !registry.view<ecs::FallingBlockTag>().empty() ||
           !registry.view<ecs::MovingBlockTag>().empty();
}

} // namespace

void LocalShadowPass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
}

void LocalShadowPass::setSceneLights(std::vector<renderer::contracts::SceneLight> lights) {
    m_sceneLights = std::move(lights);
}

bool LocalShadowPass::prepareGraphFrame(const FrameContext& ctx, const IWorldView* worldView) {
    using namespace renderer::contracts;
    if (m_graphFramePrepared || m_resourceMgr == nullptr || ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->rhiDevice->backend() != RhiBackend::Vulkan) {
        m_lastError = "local shadow frame prerequisites are incomplete";
        return false;
    }
    m_externalGeometryFrame = ctx.shared->deferredGeometryProvider != nullptr;
    if (!m_externalGeometryFrame && (worldView == nullptr || m_terrainRenderer == nullptr ||
                                     m_worldRenderBuffer == nullptr || ctx.shared->terrainRhiPipelines == nullptr)) {
        m_lastError = "local shadow gameplay geometry dependencies are incomplete";
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyResources();
        m_allocator.reset();
        m_cacheRecords.clear();
    }
    m_rhiDevice = &rhiDevice;

    m_allocations.clear();
    if (!m_allocator.allocate(m_sceneLights, m_allocations)) {
        m_pendingFrameStats = {};
        m_pendingFrameStats.allocationError = m_allocator.failure().error;
        m_lastError = std::string("local shadow allocation failed [error=") +
                      localShadowAllocationErrorStableId(m_allocator.failure().error) +
                      ", lightId=" + std::to_string(m_allocator.failure().lightId.value) + "]";
        return false;
    }

    uint32_t spotSlotCount = 0u;
    uint32_t pointSlotCount = 0u;
    for (const LocalShadowAllocation& allocation : m_allocations) {
        uint32_t& count = allocation.type == LocalShadowType::Spot ? spotSlotCount : pointSlotCount;
        count = std::max(count, allocation.resourceSlot + 1u);
    }

    if (!ensureResources(rhiDevice, spotSlotCount, pointSlotCount)) {
        m_lastError = "local shadow persistent resource creation failed";
        return false;
    }

    m_frameContext = &ctx;
    m_worldView = worldView;
    m_worldActorsPrepared = false;
    if (!buildPreparedShadows(ctx, worldView)) {
        m_frameContext = nullptr;
        m_worldView = nullptr;
        return false;
    }
    m_graphFramePrepared = true;
    m_lastError.clear();
    return true;
}

bool LocalShadowPass::ensureResources(RhiDevice& rhiDevice, const uint32_t spotSlotCount,
                                      const uint32_t pointSlotCount) {
    const uint32_t spotGrid = spotGridForSlotCount(spotSlotCount);
    const uint32_t pointCapacity =
        growPowerOfTwo(std::max(pointSlotCount, 1u), renderer::contracts::kLocalShadowMaxPointLightCount);
    return spotGrid != 0u && pointCapacity != 0u && ensureMetadataBuffer(rhiDevice) && ensureSampler(rhiDevice) &&
           ensureSpotAtlas(rhiDevice, spotGrid) && ensurePointCubeArray(rhiDevice, pointCapacity);
}

bool LocalShadowPass::ensureMetadataBuffer(RhiDevice& rhiDevice) {
    if (m_metadataBuffer.isValid()) {
        return true;
    }
    RhiBufferDesc desc;
    desc.debugName = "LocalShadow.Metadata";
    desc.size = sizeof(m_metadata);
    desc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::StorageBuffer;
    desc.memoryCategory = RhiMemoryCategory::SceneData;
    m_metadataBuffer = rhiDevice.createBuffer(desc, nullptr, 0u);
    return m_metadataBuffer.isValid();
}

bool LocalShadowPass::ensureSampler(RhiDevice& rhiDevice) {
    if (m_sampler.isValid()) {
        return true;
    }
    RhiSamplerDesc desc;
    desc.minFilter = RhiFilter::Nearest;
    desc.magFilter = RhiFilter::Nearest;
    desc.mipmapMode = RhiMipmapMode::Nearest;
    desc.addressU = RhiAddressMode::ClampToEdge;
    desc.addressV = RhiAddressMode::ClampToEdge;
    desc.addressW = RhiAddressMode::ClampToEdge;
    desc.borderColor = RhiBorderColor::OpaqueWhite;
    m_sampler = rhiDevice.createSampler(desc);
    return m_sampler.isValid();
}

bool LocalShadowPass::ensureSpotAtlas(RhiDevice& rhiDevice, const uint32_t requiredGridSize) {
    m_spotAtlasRebuilt = false;
    if (m_spotAtlasTexture.isValid() && m_spotGridSize >= requiredGridSize) {
        return true;
    }

    RhiTextureDesc desc;
    desc.debugName = "LocalShadow.SpotAtlas";
    desc.dimension = RhiTextureDimension::Texture2D;
    desc.format = RhiTextureFormat::Depth32Float;
    desc.width = requiredGridSize * renderer::contracts::kLocalShadowSpotTileResolution;
    desc.height = desc.width;
    desc.depthOrLayers = 1u;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    desc.memoryCategory = RhiMemoryCategory::SceneData;
    const RhiTextureHandle texture = rhiDevice.createTexture(desc, nullptr);
    if (!texture.isValid()) {
        return false;
    }
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = desc.format;
    const RhiTextureViewHandle view = rhiDevice.createTextureView(viewDesc);
    if (!view.isValid()) {
        rhiDevice.destroyTexture(texture);
        return false;
    }

    destroySpotAtlas();
    m_spotAtlasTexture = texture;
    m_spotAtlasView = view;
    m_spotGridSize = requiredGridSize;
    m_spotAtlasInitialized = false;
    m_spotAtlasRebuilt = true;
    invalidateCache(renderer::contracts::LocalShadowType::Spot);
    return true;
}

bool LocalShadowPass::ensurePointCubeArray(RhiDevice& rhiDevice, const uint32_t requiredCapacity) {
    m_pointCubeArrayRebuilt = false;
    if (m_pointCubeArrayTexture.isValid() && m_pointCubeCapacity >= requiredCapacity) {
        return true;
    }

    RhiTextureDesc desc;
    desc.debugName = "LocalShadow.PointCubeArray";
    desc.dimension = RhiTextureDimension::CubeArray;
    desc.format = RhiTextureFormat::Depth32Float;
    desc.width = renderer::contracts::kLocalShadowPointFaceResolution;
    desc.height = desc.width;
    desc.depthOrLayers = requiredCapacity * 6u;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    desc.memoryCategory = RhiMemoryCategory::SceneData;
    const RhiTextureHandle texture = rhiDevice.createTexture(desc, nullptr);
    if (!texture.isValid()) {
        return false;
    }
    RhiTextureViewDesc cubeViewDesc;
    cubeViewDesc.texture = texture;
    cubeViewDesc.viewType = RhiTextureViewType::CubeArray;
    cubeViewDesc.format = desc.format;
    cubeViewDesc.layerCount = desc.depthOrLayers;
    const RhiTextureViewHandle cubeView = rhiDevice.createTextureView(cubeViewDesc);
    if (!cubeView.isValid()) {
        rhiDevice.destroyTexture(texture);
        return false;
    }

    std::vector<RhiTextureViewHandle> faceViews;
    faceViews.reserve(desc.depthOrLayers);
    for (uint32_t layer = 0u; layer < desc.depthOrLayers; ++layer) {
        RhiTextureViewDesc faceViewDesc;
        faceViewDesc.texture = texture;
        faceViewDesc.viewType = RhiTextureViewType::Texture2D;
        faceViewDesc.format = desc.format;
        faceViewDesc.baseLayer = layer;
        faceViewDesc.layerCount = 1u;
        const RhiTextureViewHandle faceView = rhiDevice.createTextureView(faceViewDesc);
        if (!faceView.isValid()) {
            for (const RhiTextureViewHandle created : faceViews) {
                rhiDevice.destroyTextureView(created);
            }
            rhiDevice.destroyTextureView(cubeView);
            rhiDevice.destroyTexture(texture);
            return false;
        }
        faceViews.push_back(faceView);
    }

    destroyPointCubeArray();
    m_pointCubeArrayTexture = texture;
    m_pointCubeArrayView = cubeView;
    m_pointFaceViews = std::move(faceViews);
    m_pointCubeCapacity = requiredCapacity;
    m_pointCubeArrayInitialized = false;
    m_pointCubeArrayRebuilt = true;
    invalidateCache(renderer::contracts::LocalShadowType::Point);
    return true;
}

bool LocalShadowPass::buildPreparedShadows(const FrameContext& ctx, const IWorldView* worldView) {
    using namespace renderer::contracts;
    m_metadata = {};
    m_resolvedLights.clear();
    m_resolvedLights.reserve(m_sceneLights.size());
    for (const SceneLight& sceneLight : m_sceneLights) {
        m_resolvedLights.push_back(sceneLight.light);
    }
    m_preparedShadows.clear();
    m_preparedShadows.reserve(m_allocations.size());
    m_pendingFrameStats = {};
    m_pendingFrameStats.valid = true;
    m_pendingFrameStats.spotAtlasResolution = m_spotGridSize * kLocalShadowSpotTileResolution;
    m_pendingFrameStats.pointCubeCapacity = m_pointCubeCapacity;

    uint64_t geometryContentRevision = 0u;
    uint64_t activeGeometryRevision = 0u;
    uint64_t dynamicOccluderRevision = 0u;
    if (worldView != nullptr) {
        geometryContentRevision = worldView->getBlockContentRevision();
        activeGeometryRevision = worldView->getActiveChunkRevision();
        dynamicOccluderRevision = hasDynamicOccluders(m_gameplayRegistry) ? ctx.frameIndex : 0u;
    } else {
        if (!m_externalGeometryFrame) {
            m_lastError = "external geometry local-shadow frame is not active";
            return false;
        }
        if (ctx.shared == nullptr) {
            m_lastError = "external geometry local-shadow frame has no shared render resources";
            return false;
        }
        if (ctx.shared->deferredGeometryProvider == nullptr) {
            m_lastError = "external geometry local-shadow frame has no geometry provider";
            return false;
        }
        DeferredLocalShadowSceneRevisions revisions;
        std::string revisionError;
        if (!ctx.shared->deferredGeometryProvider->queryLocalShadowSceneRevisions(revisions, revisionError)) {
            m_lastError = revisionError;
            return false;
        }
        if (revisions.geometryContentRevision == 0u || revisions.activeGeometryRevision == 0u) {
            m_lastError = "external geometry local-shadow revisions must be non-zero";
            return false;
        }
        geometryContentRevision = revisions.geometryContentRevision;
        activeGeometryRevision = revisions.activeGeometryRevision;
        dynamicOccluderRevision = revisions.dynamicOccluderRevision;
    }
    const glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.0f), ctx.camera.position);

    std::unordered_set<uint32_t> activeCacheIds;
    activeCacheIds.reserve(m_allocations.size());
    std::vector<LocalShadowCullVolume> dirtyVolumes;
    std::vector<size_t> dirtyPreparedIndices;
    for (const LocalShadowAllocation& allocation : m_allocations) {
        const SceneLight& source = m_sceneLights[allocation.sceneLightIndex];
        GpuLight& resolved = m_resolvedLights[allocation.sceneLightIndex];
        resolved.classificationAndIdentity.z = static_cast<uint32_t>(allocation.policy);
        resolved.classificationAndIdentity.w = allocation.metadataIndex;

        PreparedShadow prepared;
        prepared.allocation = allocation;
        prepared.worldPosition = ctx.camera.position + glm::vec3(source.light.positionAndRange);
        prepared.direction = glm::vec3(source.light.direction);
        prepared.range = source.light.positionAndRange.w;
        if (!finite(prepared.worldPosition) || !std::isfinite(prepared.range) ||
            prepared.range <= kLocalShadowNearPlaneMeters) {
            m_lastError = "local shadow world transform is invalid";
            return false;
        }

        const bool spot = allocation.type == LocalShadowType::Spot;
        if (spot) {
            ++m_pendingFrameStats.requestedSpotLights;
            const float outerCosine = source.light.spotCosinesAndRectSize.y;
            const float outerAngle = std::acos(std::clamp(outerCosine, -1.0f, 1.0f));
            prepared.views[0] = glm::lookAt(prepared.worldPosition, prepared.worldPosition + prepared.direction,
                                            shadowUpVector(prepared.direction));
            prepared.projections[0] =
                glm::perspective(outerAngle * 2.0f, 1.0f, kLocalShadowNearPlaneMeters, prepared.range);
            prepared.worldViewProjections[0] = prepared.projections[0] * prepared.views[0];
        } else {
            ++m_pendingFrameStats.requestedPointLights;
            static constexpr std::array<glm::vec3, 6> kDirections{
                glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),  glm::vec3(0.0f, 0.0f, -1.0f)};
            static constexpr std::array<glm::vec3, 6> kUpVectors{
                glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)};
            for (uint32_t face = 0u; face < 6u; ++face) {
                prepared.views[face] =
                    glm::lookAt(prepared.worldPosition, prepared.worldPosition + kDirections[face], kUpVectors[face]);
                prepared.projections[face] =
                    glm::perspective(glm::radians(90.0f), 1.0f, kLocalShadowNearPlaneMeters, prepared.range);
                prepared.worldViewProjections[face] = prepared.projections[face] * prepared.views[face];
            }
        }

        LocalShadowMetadata& metadata = m_metadata[allocation.metadataIndex];
        const uint32_t faceCount = spot ? 1u : 6u;
        for (uint32_t face = 0u; face < faceCount; ++face) {
            metadata.cameraRelativeViewProjection[face] = prepared.worldViewProjections[face] * cameraTranslation;
        }
        if (spot) {
            const uint32_t tileX = allocation.resourceSlot % m_spotGridSize;
            const uint32_t tileY = allocation.resourceSlot / m_spotGridSize;
            const float scale = 1.0f / static_cast<float>(m_spotGridSize);
            metadata.atlasScaleBias = {scale, scale, scale * static_cast<float>(tileX),
                                       scale * static_cast<float>(tileY)};
        }
        metadata.nearFarDepthBiasNormalOffset = {kLocalShadowNearPlaneMeters, prepared.range,
                                                 kLocalShadowDepthBiasMeters, kLocalShadowNormalOffsetMeters};
        metadata.classification = {static_cast<uint32_t>(allocation.type), allocation.resourceSlot, faceCount,
                                   kLocalShadowContractVersion};

        prepared.pendingCache.type = allocation.type;
        prepared.pendingCache.resourceSlot = allocation.resourceSlot;
        prepared.pendingCache.positionAndRange = {prepared.worldPosition, prepared.range};
        prepared.pendingCache.directionAndOuterCosine = {prepared.direction,
                                                         spot ? source.light.spotCosinesAndRectSize.y : 0.0f};
        prepared.pendingCache.geometryContentRevision = geometryContentRevision;
        prepared.pendingCache.activeGeometryRevision = activeGeometryRevision;
        prepared.pendingCache.dynamicOccluderRevision = dynamicOccluderRevision;
        prepared.pendingCache.valid = true;
        activeCacheIds.insert(allocation.lightId.value);

        const auto cached = m_cacheRecords.find(allocation.lightId.value);
        prepared.redraw = allocation.policy == GpuLightShadowPolicy::RasterDynamic || cached == m_cacheRecords.end() ||
                          !sameCacheRecord(cached->second, prepared.pendingCache);
        if (prepared.redraw) {
            dirtyPreparedIndices.push_back(m_preparedShadows.size());
            LocalShadowCullVolume volume;
            volume.type = allocation.type;
            volume.position = prepared.worldPosition;
            volume.range = prepared.range;
            if (spot) {
                volume.frustumPlanes = extractFrustumPlanes(prepared.worldViewProjections[0]);
                ++m_pendingFrameStats.renderedSpotPages;
            } else {
                ++m_pendingFrameStats.renderedPointPages;
            }
            dirtyVolumes.push_back(volume);
        } else {
            ++m_pendingFrameStats.reusedCachedPages;
        }
        m_preparedShadows.push_back(std::move(prepared));
    }

    for (auto it = m_cacheRecords.begin(); it != m_cacheRecords.end();) {
        if (activeCacheIds.find(it->first) == activeCacheIds.end()) {
            it = m_cacheRecords.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_externalGeometryFrame && !dirtyVolumes.empty()) {
        std::vector<LocalShadowChunkRanges> ranges;
        m_terrainRenderer->collectLocalShadowChunks(*worldView, dirtyVolumes, ranges);
        if (ranges.size() != dirtyPreparedIndices.size()) {
            m_lastError = "local shadow terrain bin count is inconsistent";
            return false;
        }
        for (size_t index = 0u; index < ranges.size(); ++index) {
            m_preparedShadows[dirtyPreparedIndices[index]].terrainRanges = std::move(ranges[index]);
        }
    }
    return true;
}

bool LocalShadowPass::importGraphResources(RenderGraph& graph, GraphResources& resources) const {
    if (!m_graphFramePrepared || m_rhiDevice == nullptr || !m_metadataBuffer.isValid()) {
        return false;
    }
    RhiBufferDesc bufferDesc;
    if (!m_rhiDevice->getBufferDesc(m_metadataBuffer, bufferDesc)) {
        return false;
    }
    RgImportedBufferDesc importedBuffer;
    importedBuffer.name = bufferDesc.debugName;
    importedBuffer.buffer = m_metadataBuffer;
    importedBuffer.desc = bufferDesc;
    importedBuffer.initialState = RhiResourceState::StorageBuffer;
    importedBuffer.finalState = RhiResourceState::StorageBuffer;
    resources.metadata = graph.importBuffer(importedBuffer);
    return resources.metadata.isValid() &&
           importTexture(graph, m_spotAtlasTexture, m_spotAtlasView, m_spotAtlasInitialized, resources.spotAtlas) &&
           importTexture(graph, m_pointCubeArrayTexture, m_pointCubeArrayView, m_pointCubeArrayInitialized,
                         resources.pointCubeArray);
}

bool LocalShadowPass::importTexture(RenderGraph& graph, const RhiTextureHandle texture, const RhiTextureViewHandle view,
                                    const bool initialized, RgTextureHandle& graphTexture) const {
    RhiTextureDesc desc;
    if (!texture.isValid() || !view.isValid() || !m_rhiDevice->getTextureDesc(texture, desc)) {
        return false;
    }
    RgImportedTextureDesc imported;
    imported.name = desc.debugName;
    imported.texture = texture;
    imported.desc = desc;
    imported.initialState = initialized ? RhiResourceState::DepthRead : RhiResourceState::Undefined;
    imported.finalState = RhiResourceState::DepthRead;
    imported.defaultView = view;
    graphTexture = graph.importTexture(imported);
    return graphTexture.isValid();
}

RgPassHandle LocalShadowPass::addGraphPasses(RenderGraph& graph, const GraphResources& resources,
                                             const RgPassHandle dependency) {
    if (!m_graphFramePrepared || !dependency.isValid() || !resources.metadata.isValid() ||
        !resources.spotAtlas.isValid() || !resources.pointCubeArray.isValid()) {
        return {};
    }

    RenderGraphPassBuilder metadata =
        graph.addPass({"LocalShadow.MetadataUpload", RgPassType::Copy, RhiQueueType::Graphics});
    metadata.dependsOn(dependency)
        .writeBuffer(resources.metadata, RhiResourceState::TransferDst)
        .setExecute([this](RgPassContext& pass) { return recordMetadataUpload(pass.commandList()); });

    RenderGraphPassBuilder spot =
        graph.addPass({"LocalShadow.SpotAtlas", RgPassType::Graphics, RhiQueueType::Graphics});
    spot.dependsOn(metadata.handle());
    if (m_spotAtlasRebuilt) {
        spot.writeTexture(resources.spotAtlas, RhiResourceState::DepthWrite);
    } else {
        spot.readWriteTexture(resources.spotAtlas, RhiResourceState::DepthWrite);
    }
    spot.setExecute([this](RgPassContext& pass) { return recordSpotAtlas(pass.commandList()); });

    RenderGraphPassBuilder point =
        graph.addPass({"LocalShadow.PointCubeArray", RgPassType::Graphics, RhiQueueType::Graphics});
    point.dependsOn(spot.handle());
    if (m_pointCubeArrayRebuilt) {
        point.writeTexture(resources.pointCubeArray, RhiResourceState::DepthWrite);
    } else {
        point.readWriteTexture(resources.pointCubeArray, RhiResourceState::DepthWrite);
    }
    point.setExecute([this](RgPassContext& pass) { return recordPointCubeArray(pass.commandList()); });
    return point.handle();
}

bool LocalShadowPass::recordMetadataUpload(RhiCommandList& commandList) const {
    commandList.updateBuffer(m_metadataBuffer, 0u, m_metadata.data(), sizeof(m_metadata));
    return true;
}

bool LocalShadowPass::prepareWorldActors() {
    if (m_worldActorsPrepared) {
        return true;
    }
    const FrameContext& ctx = *m_frameContext;
    if (ctx.worldView == nullptr) {
        return false;
    }
    if (m_blockEntityRenderer != nullptr && !m_blockEntityRenderer->prepareFrame(*ctx.worldView)) {
        return false;
    }
    if (m_dropRenderer != nullptr && m_dropSystem != nullptr &&
        !m_dropRenderer->prepareFrame(*ctx.worldView, *m_dropSystem)) {
        return false;
    }
    if (m_humanoidRenderer != nullptr && m_gameplayRegistry != nullptr &&
        !m_humanoidRenderer->prepareFrame(*ctx.worldView, *m_gameplayRegistry, HumanoidRenderer::kRenderAll)) {
        return false;
    }
    if (m_fallingBlockRenderer != nullptr && m_gameplayRegistry != nullptr &&
        !m_fallingBlockRenderer->prepareFrame(*ctx.worldView, *m_gameplayRegistry)) {
        return false;
    }
    m_worldActorsPrepared = true;
    return true;
}

bool LocalShadowPass::prepareWorldGeometry(RhiCommandList& commandList, const PreparedShadow& shadow,
                                           const uint32_t faceIndex) {
    const FrameContext& ctx = *m_frameContext;
    if (!prepareWorldActors()) {
        return false;
    }

    m_worldRenderBuffer->resetDrawCommands();
    for (const GpuMeshRange& range : shadow.terrainRanges.opaque) {
        m_worldRenderBuffer->addOpaque(range);
    }
    for (const GpuMeshRange& range : shadow.terrainRanges.cutout) {
        m_worldRenderBuffer->addCutout(range);
    }
    if (m_blockEntityRenderer != nullptr &&
        !m_blockEntityRenderer->prepareShadow(commandList, shadow.worldPosition, 0.0f, shadow.range)) {
        return false;
    }

    TerrainShadowFrameData frame;
    frame.modelView = shadow.views[faceIndex];
    frame.projection = shadow.projections[faceIndex];
    frame.lightDirection =
        shadow.allocation.type == renderer::contracts::LocalShadowType::Spot ? shadow.direction : glm::vec3(0.0f);
    frame.animationTime = ctx.animationTime;
    frame.shaderTime = ctx.shaderTime;
    frame.passMode = 0;
    return ctx.shared->terrainRhiPipelines->prepareShadow(commandList, *m_resourceMgr, frame) &&
           m_worldRenderBuffer->prepareRhiOpaqueAndCutout(commandList,
                                                          ctx.shared->terrainRhiPipelines->shadowMetadataLayout());
}

void LocalShadowPass::drawWorldGeometry(RhiCommandList& commandList, const PreparedShadow& shadow,
                                        const uint32_t faceIndex) const {
    const FrameContext& ctx = *m_frameContext;
    m_worldRenderBuffer->recordRhiOpaque(commandList, ctx.shared->terrainRhiPipelines->shadowOpaquePipeline(),
                                         ctx.shared->terrainRhiPipelines->shadowBindGroup());
    m_worldRenderBuffer->recordRhiCutout(commandList, ctx.shared->terrainRhiPipelines->shadowCutoutPipeline(),
                                         ctx.shared->terrainRhiPipelines->shadowBindGroup());
    commandList.setGraphicsPipeline(ctx.shared->terrainRhiPipelines->shadowOpaquePipeline());
    if (m_blockEntityRenderer != nullptr) {
        m_blockEntityRenderer->renderToShadowMap(commandList, shadow.worldViewProjections[faceIndex]);
    }
    if (m_staticMeshRenderer != nullptr) {
        m_staticMeshRenderer->renderToShadowMap(commandList, shadow.worldViewProjections[faceIndex]);
    }
    if (m_humanoidRenderer != nullptr && m_gameplayRegistry != nullptr) {
        m_humanoidRenderer->renderPreparedToShadowMap(commandList, shadow.worldViewProjections[faceIndex],
                                                      shadow.worldPosition, 0.0f, shadow.range);
    }
    if (m_dropRenderer != nullptr && m_dropSystem != nullptr) {
        m_dropRenderer->renderToShadowMap(commandList, shadow.worldViewProjections[faceIndex], ctx.animationTime);
    }
    if (m_fallingBlockRenderer != nullptr && m_gameplayRegistry != nullptr) {
        m_fallingBlockRenderer->renderToShadowMap(commandList, shadow.worldViewProjections[faceIndex],
                                                  ctx.animationTime);
    }
}

bool LocalShadowPass::renderShadowPage(RhiCommandList& commandList, const PreparedShadow& shadow,
                                       const uint32_t faceIndex, const RhiTextureViewHandle attachment,
                                       const RhiRect2D& renderArea, const char* debugName) {
    if (!m_externalGeometryFrame && !prepareWorldGeometry(commandList, shadow, faceIndex)) {
        return false;
    }

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = attachment;
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = renderArea;
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList.beginRendering(renderingInfo);
    commandList.clearDepthAttachment(1.0f, renderArea);
    commandList.setViewport({static_cast<float>(renderArea.x), static_cast<float>(renderArea.y),
                             static_cast<float>(renderArea.width), static_cast<float>(renderArea.height), 0.0f, 1.0f});
    commandList.setScissor(renderArea);
    if (m_externalGeometryFrame) {
        m_frameContext->shared->deferredGeometryProvider->renderToShadowMap(commandList,
                                                                            shadow.worldViewProjections[faceIndex]);
    } else {
        drawWorldGeometry(commandList, shadow, faceIndex);
    }
    commandList.endRendering();
    return true;
}

bool LocalShadowPass::recordSpotAtlas(RhiCommandList& commandList) {
    const FrameContext& ctx = *m_frameContext;
    const GpuTimerSegmentToken timer = ctx.debugService != nullptr
                                           ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Shadow)
                                           : GpuTimerSegmentToken{};
    const auto finishTimer = [&]() {
        if (ctx.debugService != nullptr) {
            ctx.debugService->endGpuTimer(commandList, timer);
        }
    };
    const uint32_t atlasResolution = m_spotGridSize * renderer::contracts::kLocalShadowSpotTileResolution;
    if (m_spotAtlasRebuilt) {
        RhiDepthStencilAttachment attachment;
        attachment.view = m_spotAtlasView;
        attachment.depthLoadOp = RhiLoadOp::Clear;
        attachment.depthStoreOp = RhiStoreOp::Store;
        attachment.clearDepth = 1.0f;
        RhiRenderingInfo info;
        info.debugName = "LocalShadow.SpotAtlasInitialize";
        info.renderArea = {0, 0, atlasResolution, atlasResolution};
        info.depthStencilAttachment = &attachment;
        commandList.beginRendering(info);
        commandList.endRendering();
    }
    for (const PreparedShadow& shadow : m_preparedShadows) {
        if (!shadow.redraw || shadow.allocation.type != renderer::contracts::LocalShadowType::Spot) {
            continue;
        }
        const uint32_t tile = renderer::contracts::kLocalShadowSpotTileResolution;
        const uint32_t tileX = shadow.allocation.resourceSlot % m_spotGridSize;
        const uint32_t tileY = shadow.allocation.resourceSlot / m_spotGridSize;
        const RhiRect2D area{static_cast<int32_t>(tileX * tile), static_cast<int32_t>(tileY * tile), tile, tile};
        if (!renderShadowPage(commandList, shadow, 0u, m_spotAtlasView, area, "LocalShadow.SpotPage")) {
            finishTimer();
            return false;
        }
    }
    finishTimer();
    return true;
}

bool LocalShadowPass::clearPointFace(RhiCommandList& commandList, const uint32_t faceLayer) const {
    if (faceLayer >= m_pointFaceViews.size()) {
        return false;
    }
    RhiDepthStencilAttachment attachment;
    attachment.view = m_pointFaceViews[faceLayer];
    attachment.depthLoadOp = RhiLoadOp::Clear;
    attachment.depthStoreOp = RhiStoreOp::Store;
    attachment.clearDepth = 1.0f;
    RhiRenderingInfo info;
    info.debugName = "LocalShadow.PointFaceInitialize";
    info.renderArea = {0, 0, renderer::contracts::kLocalShadowPointFaceResolution,
                       renderer::contracts::kLocalShadowPointFaceResolution};
    info.depthStencilAttachment = &attachment;
    commandList.beginRendering(info);
    commandList.endRendering();
    return true;
}

bool LocalShadowPass::recordPointCubeArray(RhiCommandList& commandList) {
    const FrameContext& ctx = *m_frameContext;
    const GpuTimerSegmentToken timer = ctx.debugService != nullptr
                                           ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Shadow)
                                           : GpuTimerSegmentToken{};
    const auto finishTimer = [&]() {
        if (ctx.debugService != nullptr) {
            ctx.debugService->endGpuTimer(commandList, timer);
        }
    };
    if (m_pointCubeArrayRebuilt) {
        for (uint32_t layer = 0u; layer < static_cast<uint32_t>(m_pointFaceViews.size()); ++layer) {
            if (!clearPointFace(commandList, layer)) {
                finishTimer();
                return false;
            }
        }
    }
    const RhiRect2D area{0, 0, renderer::contracts::kLocalShadowPointFaceResolution,
                         renderer::contracts::kLocalShadowPointFaceResolution};
    for (const PreparedShadow& shadow : m_preparedShadows) {
        if (!shadow.redraw || shadow.allocation.type != renderer::contracts::LocalShadowType::Point) {
            continue;
        }
        for (uint32_t face = 0u; face < 6u; ++face) {
            const uint32_t layer = shadow.allocation.resourceSlot * 6u + face;
            if (layer >= m_pointFaceViews.size() ||
                !renderShadowPage(commandList, shadow, face, m_pointFaceViews[layer], area, "LocalShadow.PointFace")) {
                finishTimer();
                return false;
            }
        }
    }
    finishTimer();
    return true;
}

LocalShadowPass::ConsumerResources LocalShadowPass::consumerResources() const {
    return {m_metadataBuffer, sizeof(m_metadata), m_spotAtlasView, m_pointCubeArrayView, m_sampler};
}

void LocalShadowPass::finishGraphExecution(const bool succeeded) {
    if (!m_graphFramePrepared) {
        return;
    }
    if (succeeded) {
        for (const PreparedShadow& shadow : m_preparedShadows) {
            if (shadow.redraw && shadow.allocation.policy == renderer::contracts::GpuLightShadowPolicy::RasterCached) {
                m_cacheRecords[shadow.allocation.lightId.value] = shadow.pendingCache;
            }
        }
        m_spotAtlasInitialized = true;
        m_pointCubeArrayInitialized = true;
        m_frameStats = m_pendingFrameStats;
    }
    m_frameContext = nullptr;
    m_worldView = nullptr;
    m_externalGeometryFrame = false;
    m_worldActorsPrepared = false;
    m_graphFramePrepared = false;
    m_spotAtlasRebuilt = false;
    m_pointCubeArrayRebuilt = false;
    m_preparedShadows.clear();
    m_allocations.clear();
}

void LocalShadowPass::invalidateCache(const renderer::contracts::LocalShadowType type) {
    for (auto& [id, record] : m_cacheRecords) {
        static_cast<void>(id);
        if (record.type == type) {
            record.valid = false;
        }
    }
}

bool LocalShadowPass::sameCacheRecord(const CacheRecord& lhs, const CacheRecord& rhs) {
    return lhs.valid && lhs.type == rhs.type && lhs.resourceSlot == rhs.resourceSlot &&
           nearlyEqual(lhs.positionAndRange, rhs.positionAndRange) &&
           nearlyEqual(lhs.directionAndOuterCosine, rhs.directionAndOuterCosine) &&
           lhs.geometryContentRevision == rhs.geometryContentRevision &&
           lhs.activeGeometryRevision == rhs.activeGeometryRevision &&
           lhs.dynamicOccluderRevision == rhs.dynamicOccluderRevision;
}

void LocalShadowPass::destroySpotAtlas() {
    if (m_rhiDevice != nullptr) {
        if (m_spotAtlasView.isValid()) {
            m_rhiDevice->destroyTextureView(m_spotAtlasView);
        }
        if (m_spotAtlasTexture.isValid()) {
            m_rhiDevice->destroyTexture(m_spotAtlasTexture);
        }
    }
    m_spotAtlasView = {};
    m_spotAtlasTexture = {};
    m_spotGridSize = 0u;
    m_spotAtlasInitialized = false;
}

void LocalShadowPass::destroyPointCubeArray() {
    if (m_rhiDevice != nullptr) {
        for (const RhiTextureViewHandle view : m_pointFaceViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
        }
        if (m_pointCubeArrayView.isValid()) {
            m_rhiDevice->destroyTextureView(m_pointCubeArrayView);
        }
        if (m_pointCubeArrayTexture.isValid()) {
            m_rhiDevice->destroyTexture(m_pointCubeArrayTexture);
        }
    }
    m_pointFaceViews.clear();
    m_pointCubeArrayView = {};
    m_pointCubeArrayTexture = {};
    m_pointCubeCapacity = 0u;
    m_pointCubeArrayInitialized = false;
}

void LocalShadowPass::destroyResources() {
    destroySpotAtlas();
    destroyPointCubeArray();
    if (m_rhiDevice != nullptr) {
        if (m_sampler.isValid()) {
            m_rhiDevice->destroySampler(m_sampler);
        }
        if (m_metadataBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_metadataBuffer);
        }
    }
    m_sampler = {};
    m_metadataBuffer = {};
}

void LocalShadowPass::shutdown() {
    destroyResources();
    m_allocator.reset();
    m_cacheRecords.clear();
    m_sceneLights.clear();
    m_resolvedLights.clear();
    m_preparedShadows.clear();
    m_allocations.clear();
    m_frameContext = nullptr;
    m_worldView = nullptr;
    m_resourceMgr = nullptr;
    m_rhiDevice = nullptr;
    m_graphFramePrepared = false;
    m_frameStats = {};
    m_pendingFrameStats = {};
    m_lastError.clear();
}
