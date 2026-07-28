#include "DeferredRenderTargets.h"
#include "../../Diagnostics.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiCommandListPool.h"
#include "../rhi/RhiResources.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <vector>

namespace {
[[nodiscard]] uint64_t textureStateKey(const RhiTextureHandle texture) {
    return (static_cast<uint64_t>(texture.generation) << 32u) | texture.index;
}
} // namespace

DeferredRenderTargets::~DeferredRenderTargets() {
    shutdown();
}

bool DeferredRenderTargets::init(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool) {
    if ((m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) ||
        (m_commandListPool != nullptr && m_commandListPool != &commandListPool)) {
        shutdown();
    }
    m_rhiDevice = &rhiDevice;
    m_commandListPool = &commandListPool;
    return true;
}

void DeferredRenderTargets::shutdown() {
    destroyFramebuffers();
    destroyAtmosphereLutTexture();
    m_currentHistoryIndex = 0;
    m_ssaoHistoryIndex = 0;
    m_ssgiHistoryIndex = 0;
    m_width = 0;
    m_height = 0;
    m_shadowResolution = 0;
    m_ready = false;
    m_textureStates.clear();
    m_rhiDevice = nullptr;
    m_commandListPool = nullptr;
}

void DeferredRenderTargets::transitionTexture(RhiCommandList& commandList,
                                              const RhiTextureHandle texture,
                                              const RhiResourceState newState) const {
    assert(texture.isValid());
    const uint64_t key = textureStateKey(texture);
    const auto stateIt = m_textureStates.find(key);
    if (stateIt == m_textureStates.end()) {
        std::abort();
    }
    const RhiResourceState oldState = stateIt->second;
    if (oldState == newState) {
        return;
    }
    commandList.textureBarrier({texture, oldState, newState});
    m_textureStates[key] = newState;
}

void DeferredRenderTargets::initializeTextureState(RhiCommandList& commandList,
                                                   const RhiTextureHandle texture,
                                                   const RhiResourceState stableState) const {
    assert(texture.isValid());
    commandList.textureBarrier({texture, RhiResourceState::Undefined, stableState});
    m_textureStates[textureStateKey(texture)] = stableState;
}

bool DeferredRenderTargets::ensureSize(const int width, const int height, const int shadowResolution) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    const int targetShadow = std::max(256, shadowResolution);
    if (m_ready && m_width == targetWidth && m_height == targetHeight && m_shadowResolution == targetShadow) {
        return true;
    }

    destroyFramebuffers();
    m_rebuiltSinceCheck = true;

    m_width = targetWidth;
    m_height = targetHeight;
    m_shadowResolution = targetShadow;

    if (!createGBufferTextures()) {
        shutdown();
        return false;
    }
    if (!createSceneTextures()) {
        shutdown();
        return false;
    }
    if (!createScreenEffectTextures()) {
        shutdown();
        return false;
    }
    if (!createAtmosphereTextures()) {
        shutdown();
        return false;
    }
    if (!createSceneHistoryTextures()) {
        shutdown();
        return false;
    }
    if (!createEffectHistoryTextures()) {
        shutdown();
        return false;
    }
    if (!createMotionTextures()) {
        shutdown();
        return false;
    }
    if (!createSsaoTextures()) {
        shutdown();
        return false;
    }
    if (!createSsgiTextures()) {
        shutdown();
        return false;
    }
    if (!createCsmShadowTextures()) {
        shutdown();
        return false;
    }
    m_ssaoHistoryIndex = 0;

    m_ssgiHistoryIndex = 0;

    m_currentHistoryIndex = 0;

    if (!registerRhiTextures()) {
        shutdown();
        return false;
    }

    initializePersistentTextureStates();

    m_ready = true;
    return true;
}

void DeferredRenderTargets::initializePersistentTextureStates() {
    assert(m_rhiDevice != nullptr);

    const RhiTextureHandle colorTextures[] = {
        m_gAlbedoHandle,
        m_gNormalAoHandle,
        m_gVoxelLightHandle,
        m_gMaterialHandle,
        m_gMaterialAuxHandle,
        // SceneLighting/SceneComposite/TransparentComposite/Reflection/Cloud
        // are render-graph transients now; the graph tracks their states.
        m_sceneResolvedHandle,
        m_halfResHandle,
        m_reflectionTemporalScratchHandle,
        m_skyCaptureHandle,
        m_historySceneHandle[0],
        m_historySceneHandle[1],
        m_historyReflectionHandle[0],
        m_historyReflectionHandle[1],
        m_historyCloudHandle[0],
        m_historyCloudHandle[1],
        m_historyVolumetricHandle[0],
        m_historyVolumetricHandle[1],
        m_temporalCurrentHandle,
        m_velocityHandle,
        m_perObjectVelocityHandle,
        m_weatherMaskHandle,
        m_reactiveMaskHandle,
        m_transparencyMaskHandle,
        m_ssaoFilteredHandle,
        m_ssaoHalfResHandle,
        m_ssaoHalfResFilteredHandle,
        m_ssaoHistoryHandle[0],
        m_ssaoHistoryHandle[1],
        m_ssaoTemporalHandle,
        m_ssgiHandle,
        m_ssgiHalfResHandle,
        m_ssgiDenoiseHandle[0],
        m_ssgiDenoiseHandle[1],
        m_ssgiHistoryHandle[0],
        m_ssgiHistoryHandle[1],
        m_ssgiMomentsHistoryHandle[0],
        m_ssgiMomentsHistoryHandle[1],
        m_ssgiTemporalHandle,
        m_ssgiTemporalMomentsHandle,
        m_csmShadowColor0Handle,
        m_csmShadowColor1Handle
    };
    const RhiTextureHandle depthTextures[] = {
        m_gDepthHandle,
        m_historyDepthHandle[0],
        m_historyDepthHandle[1],
        m_taaHistoryDepthHandle[0],
        m_taaHistoryDepthHandle[1],
        m_csmShadowDepthHandle,
        m_csmShadowDepthAllHandle
    };

    if (m_commandListPool == nullptr || m_rhiDevice == nullptr) {
        std::abort();
    }
    RhiCommandList* const commandListStorage =
        m_commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin(
            {"DeferredTargets.InitializeStates", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    for (const RhiTextureHandle texture : colorTextures) {
        initializeTextureState(commandList, texture, RhiResourceState::ShaderRead);
    }
    for (const RhiTextureHandle texture : depthTextures) {
        initializeTextureState(commandList, texture, RhiResourceState::DepthRead);
    }
    if (!commandList.end()) {
        std::abort();
    }
    RhiCommandList* submittedCommandLists[] = {&commandList};
    if (!m_rhiDevice->submit(
            {"DeferredTargets.InitializeStates", submittedCommandLists, 1u})) {
        std::abort();
    }
}

void DeferredRenderTargets::publishTransientTarget(
    const DeferredTransientTarget target,
    const RhiTextureHandle texture,
    const RhiTextureViewHandle view) {
    switch (target) {
    case DeferredTransientTarget::SceneLighting:
        m_sceneLightingHandle = texture;
        m_sceneLightingView = view;
        break;
    case DeferredTransientTarget::SceneComposite:
        m_sceneCompositeHandle = texture;
        m_sceneCompositeView = view;
        break;
    case DeferredTransientTarget::TransparentComposite:
        m_transparentCompositeHandle = texture;
        m_transparentCompositeView = view;
        break;
    case DeferredTransientTarget::TransparentCompositeDepth:
        m_transparentCompositeDepthHandle = texture;
        m_transparentCompositeDepthView = view;
        break;
    case DeferredTransientTarget::Reflection:
        m_reflectionHandle = texture;
        m_reflectionView = view;
        break;
    case DeferredTransientTarget::Cloud:
        m_cloudHandle = texture;
        m_cloudView = view;
        break;
    }
}

RhiTextureDesc DeferredRenderTargets::transientTargetDesc(
    const DeferredTransientTarget target) const {
    RhiTextureDesc desc;
    // The caller names the graph resource; the shared pool must not point at
    // a string whose storage may outlive this call site.
    desc.debugName = nullptr;
    desc.memoryCategory = RhiMemoryCategory::Transient;
    desc.dimension = RhiTextureDimension::Texture2D;
    desc.width = static_cast<uint32_t>(m_width);
    desc.height = static_cast<uint32_t>(m_height);
    desc.depthOrLayers = 1u;
    desc.mipLevels = 1u;
    desc.sampleCount = 1u;
    const RhiTextureUsageFlags colorUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);
    switch (target) {
    case DeferredTransientTarget::SceneLighting:
    case DeferredTransientTarget::SceneComposite:
    case DeferredTransientTarget::TransparentComposite:
    case DeferredTransientTarget::Reflection:
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.usage = colorUsage;
        break;
    case DeferredTransientTarget::TransparentCompositeDepth:
        // A separate depth attachment prevents feedback while the G-buffer
        // depth is sampled.
        desc.format = RhiTextureFormat::Depth32Float;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
                     rhiFlag(RhiTextureUsage::TransferSrc) |
                     rhiFlag(RhiTextureUsage::TransferDst);
        break;
    case DeferredTransientTarget::Cloud:
        // Half-resolution raymarch target; Storage lets the async-compute
        // cloud path write it as a storage image.
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = static_cast<uint32_t>(std::max(1, m_width / 2));
        desc.height = static_cast<uint32_t>(std::max(1, m_height / 2));
        desc.usage = colorUsage | rhiFlag(RhiTextureUsage::Storage);
        break;
    }
    return desc;
}

RhiTextureHandle DeferredRenderTargets::ssgiDenoiseTextureHandle(const int slot) const {
    assert(slot >= 0 && slot < 2);
    return m_ssgiDenoiseHandle[slot];
}

RhiTextureViewHandle DeferredRenderTargets::ssgiDenoiseTextureViewHandle(const int slot) const {
    assert(slot >= 0 && slot < 2);
    return m_ssgiDenoiseView[slot];
}

bool DeferredRenderTargets::createGBufferTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const RhiTextureFormat format,
                                      const RhiTextureUsageFlags usage,
                                      RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = format;
        desc.width = static_cast<uint32_t>(m_width);
        desc.height = static_cast<uint32_t>(m_height);
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = usage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const RhiTextureUsageFlags colorUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureUsageFlags depthUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);

    if (!createTexture("DeferredTargets.GBufferAlbedo", RhiTextureFormat::Rgba8Unorm, colorUsage, m_gAlbedoHandle) ||
        !createTexture("DeferredTargets.GBufferNormalAo", RhiTextureFormat::Rgb10A2Unorm, colorUsage, m_gNormalAoHandle) ||
        !createTexture("DeferredTargets.GBufferVoxelLight", RhiTextureFormat::Rg8Unorm, colorUsage, m_gVoxelLightHandle) ||
        !createTexture("DeferredTargets.GBufferMaterial", RhiTextureFormat::Rgba8Unorm, colorUsage, m_gMaterialHandle) ||
        !createTexture("DeferredTargets.GBufferMaterialAux", RhiTextureFormat::Rgba8Unorm, colorUsage, m_gMaterialAuxHandle) ||
        !createTexture("DeferredTargets.GBufferDepth", RhiTextureFormat::Depth32Float, depthUsage, m_gDepthHandle)) {
        destroyGBufferTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroyGBufferTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    RhiTextureHandle* textures[] = {
        &m_gAlbedoHandle,
        &m_gNormalAoHandle,
        &m_gVoxelLightHandle,
        &m_gMaterialHandle,
        &m_gMaterialAuxHandle,
        &m_gDepthHandle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::createSceneTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName, RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = static_cast<uint32_t>(m_width);
        desc.height = static_cast<uint32_t>(m_height);
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::ColorAttachment) |
                     rhiFlag(RhiTextureUsage::TransferSrc) |
                     rhiFlag(RhiTextureUsage::TransferDst);
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    // SceneLighting and SceneComposite are frame-scoped render-graph
    // transients published per frame; only SceneResolved stays persistent
    // because it feeds temporal history across frames.
    if (!createTexture("DeferredTargets.SceneResolved", m_sceneResolvedHandle)) {
        destroySceneTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroySceneTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    if (m_sceneResolvedHandle.isValid()) {
        m_rhiDevice->destroyTexture(m_sceneResolvedHandle);
        m_sceneResolvedHandle = {};
    }
}

bool DeferredRenderTargets::createScreenEffectTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const uint32_t width,
                                      const uint32_t height,
                                      RhiTextureHandle& handle,
                                      const RhiTextureUsageFlags extraUsage =
                                          0u) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = width;
        desc.height = height;
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::ColorAttachment) |
                     rhiFlag(RhiTextureUsage::TransferSrc) |
                     rhiFlag(RhiTextureUsage::TransferDst) | extraUsage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const uint32_t width = static_cast<uint32_t>(m_width);
    const uint32_t height = static_cast<uint32_t>(m_height);
    const uint32_t halfWidth = static_cast<uint32_t>(std::max(1, m_width / 2));
    const uint32_t halfHeight = static_cast<uint32_t>(std::max(1, m_height / 2));
    // Reflection itself is a frame-scoped render-graph transient now; only
    // the persistent helpers remain here.
    if (!createTexture("DeferredTargets.HalfRes", halfWidth, halfHeight, m_halfResHandle) ||
        !createTexture("DeferredTargets.ReflectionTemporalScratch", width, height,
                       m_reflectionTemporalScratchHandle)) {
        destroyScreenEffectTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroyScreenEffectTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    RhiTextureHandle* textures[] = {
        &m_halfResHandle,
        &m_reflectionTemporalScratchHandle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::createAtmosphereTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const uint32_t width,
                                      const uint32_t height,
                                      RhiTextureHandle& handle,
                                      const RhiTextureUsageFlags extraUsage =
                                          0u) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = width;
        desc.height = height;
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::ColorAttachment) |
                     rhiFlag(RhiTextureUsage::TransferSrc) |
                     rhiFlag(RhiTextureUsage::TransferDst) | extraUsage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    // Cloud is a frame-scoped render-graph transient now (half resolution
    // with Storage usage); only the persistent sky capture stays here.
    if (!createTexture("DeferredTargets.SkyCapture",
                       static_cast<uint32_t>(kSkyCaptureWidth),
                       static_cast<uint32_t>(kSkyCaptureHeight),
                       m_skyCaptureHandle)) {
        destroyAtmosphereTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroyAtmosphereTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    if (m_skyCaptureHandle.isValid()) {
        m_rhiDevice->destroyTexture(m_skyCaptureHandle);
        m_skyCaptureHandle = {};
    }
}

bool DeferredRenderTargets::createSceneHistoryTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const RhiTextureFormat format,
                                      const RhiTextureUsageFlags usage,
                                      RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = format;
        desc.width = static_cast<uint32_t>(m_width);
        desc.height = static_cast<uint32_t>(m_height);
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = usage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const RhiTextureUsageFlags colorUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureUsageFlags depthUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);
    for (int i = 0; i < 2; ++i) {
        const char* sceneName = i == 0
            ? "DeferredTargets.HistoryScene[0]"
            : "DeferredTargets.HistoryScene[1]";
        const char* depthName = i == 0
            ? "DeferredTargets.HistoryDepth[0]"
            : "DeferredTargets.HistoryDepth[1]";
        const char* taaDepthName = i == 0
            ? "DeferredTargets.TaaHistoryDepth[0]"
            : "DeferredTargets.TaaHistoryDepth[1]";
        if (!createTexture(sceneName, RhiTextureFormat::Rgba16Float, colorUsage, m_historySceneHandle[i]) ||
            !createTexture(depthName, RhiTextureFormat::Depth32Float, depthUsage, m_historyDepthHandle[i]) ||
            !createTexture(taaDepthName, RhiTextureFormat::Depth32Float, depthUsage,
                           m_taaHistoryDepthHandle[i])) {
            destroySceneHistoryTextures();
            return false;
        }
    }
    return true;
}

void DeferredRenderTargets::destroySceneHistoryTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    for (int i = 0; i < 2; ++i) {
        if (m_historySceneHandle[i].isValid()) {
            m_rhiDevice->destroyTexture(m_historySceneHandle[i]);
            m_historySceneHandle[i] = {};
        }
        if (m_historyDepthHandle[i].isValid()) {
            m_rhiDevice->destroyTexture(m_historyDepthHandle[i]);
            m_historyDepthHandle[i] = {};
        }
        if (m_taaHistoryDepthHandle[i].isValid()) {
            m_rhiDevice->destroyTexture(m_taaHistoryDepthHandle[i]);
            m_taaHistoryDepthHandle[i] = {};
        }
    }
}

bool DeferredRenderTargets::createEffectHistoryTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const uint32_t width,
                                      const uint32_t height,
                                      RhiTextureHandle& handle,
                                      const RhiTextureUsageFlags extraUsage =
                                          0u) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = width;
        desc.height = height;
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::ColorAttachment) |
                     rhiFlag(RhiTextureUsage::TransferSrc) |
                     rhiFlag(RhiTextureUsage::TransferDst) | extraUsage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const uint32_t width = static_cast<uint32_t>(m_width);
    const uint32_t height = static_cast<uint32_t>(m_height);
    const uint32_t halfWidth = static_cast<uint32_t>(std::max(1, m_width / 2));
    const uint32_t halfHeight = static_cast<uint32_t>(std::max(1, m_height / 2));
    for (int i = 0; i < 2; ++i) {
        const char* reflectionName = i == 0
            ? "DeferredTargets.HistoryReflection[0]"
            : "DeferredTargets.HistoryReflection[1]";
        const char* cloudName = i == 0
            ? "DeferredTargets.HistoryCloud[0]"
            : "DeferredTargets.HistoryCloud[1]";
        const char* volumetricName = i == 0
            ? "DeferredTargets.HistoryVolumetric[0]"
            : "DeferredTargets.HistoryVolumetric[1]";
        if (!createTexture(reflectionName, width, height, m_historyReflectionHandle[i]) ||
            !createTexture(cloudName, halfWidth, halfHeight, m_historyCloudHandle[i]) ||
            !createTexture(volumetricName, halfWidth, halfHeight, m_historyVolumetricHandle[i])) {
            destroyEffectHistoryTextures();
            return false;
        }
    }
    return true;
}

void DeferredRenderTargets::destroyEffectHistoryTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    for (int i = 0; i < 2; ++i) {
        RhiTextureHandle* textures[] = {
            &m_historyReflectionHandle[i],
            &m_historyCloudHandle[i],
            &m_historyVolumetricHandle[i]
        };
        for (RhiTextureHandle* texture : textures) {
            if (texture->isValid()) {
                m_rhiDevice->destroyTexture(*texture);
                *texture = {};
            }
        }
    }
}

bool DeferredRenderTargets::createMotionTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const RhiTextureFormat format,
                                      const RhiTextureUsageFlags usage,
                                      RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = format;
        desc.width = static_cast<uint32_t>(m_width);
        desc.height = static_cast<uint32_t>(m_height);
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = usage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const RhiTextureUsageFlags attachmentUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment);
    // TemporalCurrent doubles as the second scene color ping-pong buffer:
    // TAA/motion blur/DoF render into it and downstream copies blit from it,
    // so it needs the same usage set as SceneResolved.
    {
        // Full mip chain down to 1x1; the build pass stops early when a mip
        // would fall below a useful footprint, but allocating the full chain
        // keeps the view bookkeeping trivial across resizes.
        uint32_t mipCount = 1u;
        for (uint32_t extent = static_cast<uint32_t>(
                 std::max(m_width, m_height));
             extent > 1u; extent >>= 1u) {
            ++mipCount;
        }
        m_hiZMipCount = mipCount;
        RhiTextureDesc desc;
        desc.debugName = "DeferredTargets.HiZ";
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::R32Float;
        desc.width = static_cast<uint32_t>(m_width);
        desc.height = static_cast<uint32_t>(m_height);
        desc.depthOrLayers = 1u;
        desc.mipLevels = mipCount;
        desc.sampleCount = 1u;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::Storage);
        m_hiZHandle = m_rhiDevice->createTexture(desc, nullptr);
        if (!m_hiZHandle.isValid()) {
            destroyMotionTextures();
            return false;
        }
    }
    if (!createTexture("DeferredTargets.TemporalCurrent",
                       RhiTextureFormat::Rgba16Float,
                       rhiFlag(RhiTextureUsage::Sampled) |
                           rhiFlag(RhiTextureUsage::ColorAttachment) |
                           rhiFlag(RhiTextureUsage::TransferSrc) |
                           rhiFlag(RhiTextureUsage::TransferDst),
                       m_temporalCurrentHandle) ||
        !createTexture("DeferredTargets.Velocity", RhiTextureFormat::Rg16Float,
                       attachmentUsage, m_velocityHandle) ||
        !createTexture("DeferredTargets.PerObjectVelocity", RhiTextureFormat::Rg16Float,
                       attachmentUsage, m_perObjectVelocityHandle) ||
        !createTexture("DeferredTargets.WeatherMask", RhiTextureFormat::R8Unorm,
                       attachmentUsage, m_weatherMaskHandle) ||
        !createTexture("DeferredTargets.ReactiveMask", RhiTextureFormat::R8Unorm,
                       attachmentUsage, m_reactiveMaskHandle) ||
        !createTexture("DeferredTargets.TransparencyMask", RhiTextureFormat::R8Unorm,
                       attachmentUsage, m_transparencyMaskHandle)) {
        destroyMotionTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroyMotionTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    RhiTextureHandle* textures[] = {
        &m_hiZHandle,
        &m_temporalCurrentHandle,
        &m_velocityHandle,
        &m_perObjectVelocityHandle,
        &m_weatherMaskHandle,
        &m_reactiveMaskHandle,
        &m_transparencyMaskHandle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::createSsaoTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const uint32_t width,
                                      const uint32_t height,
                                      const RhiTextureUsageFlags usage,
                                      RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::R8Unorm;
        desc.width = width;
        desc.height = height;
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = usage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const uint32_t width = static_cast<uint32_t>(m_width);
    const uint32_t height = static_cast<uint32_t>(m_height);
    const uint32_t halfWidth = static_cast<uint32_t>(std::max(1, m_width / 2));
    const uint32_t halfHeight = static_cast<uint32_t>(std::max(1, m_height / 2));
    const RhiTextureUsageFlags attachmentUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment);
    const RhiTextureUsageFlags historyUsage =
        attachmentUsage | rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureUsageFlags temporalUsage =
        attachmentUsage | rhiFlag(RhiTextureUsage::TransferSrc);

    // Storage usage lets the async-compute SSAO chain write these directly.
    const RhiTextureUsageFlags ssaoComputeUsage =
        attachmentUsage | rhiFlag(RhiTextureUsage::Storage);
    if (!createTexture("DeferredTargets.SSAOFiltered", width, height,
                       ssaoComputeUsage, m_ssaoFilteredHandle) ||
        !createTexture("DeferredTargets.SSAOHalfRes", halfWidth, halfHeight,
                       ssaoComputeUsage, m_ssaoHalfResHandle) ||
        !createTexture("DeferredTargets.SSAOHalfResFiltered", halfWidth, halfHeight,
                       ssaoComputeUsage, m_ssaoHalfResFilteredHandle) ||
        !createTexture("DeferredTargets.SSAOHistory[0]", width, height,
                       historyUsage, m_ssaoHistoryHandle[0]) ||
        !createTexture("DeferredTargets.SSAOHistory[1]", width, height,
                       historyUsage, m_ssaoHistoryHandle[1]) ||
        !createTexture("DeferredTargets.SSAOTemporal", width, height,
                       temporalUsage | rhiFlag(RhiTextureUsage::Storage),
                       m_ssaoTemporalHandle)) {
        destroySsaoTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroySsaoTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    RhiTextureHandle* textures[] = {
        &m_ssaoFilteredHandle,
        &m_ssaoHalfResHandle,
        &m_ssaoHalfResFilteredHandle,
        &m_ssaoHistoryHandle[0],
        &m_ssaoHistoryHandle[1],
        &m_ssaoTemporalHandle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::createSsgiTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const uint32_t width,
                                      const uint32_t height,
                                      const RhiTextureUsageFlags usage,
                                      RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = width;
        desc.height = height;
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = usage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const uint32_t width = static_cast<uint32_t>(m_width);
    const uint32_t height = static_cast<uint32_t>(m_height);
    const uint32_t halfWidth = static_cast<uint32_t>(std::max(1, m_width / 2));
    const uint32_t halfHeight = static_cast<uint32_t>(std::max(1, m_height / 2));
    const RhiTextureUsageFlags attachmentUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment);
    const RhiTextureUsageFlags transferSourceUsage =
        attachmentUsage | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureUsageFlags transferDestinationUsage =
        attachmentUsage | rhiFlag(RhiTextureUsage::TransferDst);

    if (!createTexture("DeferredTargets.SSGI", width, height,
                       transferDestinationUsage, m_ssgiHandle) ||
        !createTexture("DeferredTargets.SSGIHalfRes", halfWidth, halfHeight,
                       attachmentUsage, m_ssgiHalfResHandle) ||
        !createTexture("DeferredTargets.SSGIDenoise[0]", width, height,
                       transferSourceUsage, m_ssgiDenoiseHandle[0]) ||
        !createTexture("DeferredTargets.SSGIDenoise[1]", width, height,
                       transferSourceUsage, m_ssgiDenoiseHandle[1]) ||
        !createTexture("DeferredTargets.SSGIHistory[0]", width, height,
                       transferDestinationUsage, m_ssgiHistoryHandle[0]) ||
        !createTexture("DeferredTargets.SSGIHistory[1]", width, height,
                       transferDestinationUsage, m_ssgiHistoryHandle[1]) ||
        !createTexture("DeferredTargets.SSGIMomentsHistory[0]", width, height,
                       transferDestinationUsage, m_ssgiMomentsHistoryHandle[0]) ||
        !createTexture("DeferredTargets.SSGIMomentsHistory[1]", width, height,
                       transferDestinationUsage, m_ssgiMomentsHistoryHandle[1]) ||
        !createTexture("DeferredTargets.SSGITemporal", width, height,
                       transferSourceUsage, m_ssgiTemporalHandle) ||
        !createTexture("DeferredTargets.SSGITemporalMoments", width, height,
                       transferSourceUsage, m_ssgiTemporalMomentsHandle)) {
        destroySsgiTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroySsgiTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    RhiTextureHandle* textures[] = {
        &m_ssgiHandle,
        &m_ssgiHalfResHandle,
        &m_ssgiDenoiseHandle[0],
        &m_ssgiDenoiseHandle[1],
        &m_ssgiHistoryHandle[0],
        &m_ssgiHistoryHandle[1],
        &m_ssgiMomentsHistoryHandle[0],
        &m_ssgiMomentsHistoryHandle[1],
        &m_ssgiTemporalHandle,
        &m_ssgiTemporalMomentsHandle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::createCsmShadowTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const RhiTextureFormat format,
                                      const RhiTextureUsageFlags usage,
                                      RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.memoryCategory = RhiMemoryCategory::GBufferHistory;
        desc.dimension = RhiTextureDimension::Texture2DArray;
        desc.format = format;
        desc.width = static_cast<uint32_t>(m_shadowResolution);
        desc.height = static_cast<uint32_t>(m_shadowResolution);
        desc.depthOrLayers = static_cast<uint32_t>(kShadowCascadeCount);
        desc.usage = usage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const RhiTextureUsageFlags depthUsage = rhiFlag(RhiTextureUsage::Sampled) |
                                            rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
                                            rhiFlag(RhiTextureUsage::TransferSrc) |
                                            rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureUsageFlags colorUsage = rhiFlag(RhiTextureUsage::Sampled) |
                                            rhiFlag(RhiTextureUsage::ColorAttachment);
    if (!createTexture("DeferredTargets.CSMDepthArray", RhiTextureFormat::Depth32Float,
                       depthUsage, m_csmShadowDepthHandle) ||
        !createTexture("DeferredTargets.CSMDepthAll", RhiTextureFormat::Depth32Float,
                       depthUsage, m_csmShadowDepthAllHandle) ||
        !createTexture("DeferredTargets.CSMColor0", RhiTextureFormat::Rgba8Unorm,
                       colorUsage, m_csmShadowColor0Handle) ||
        !createTexture("DeferredTargets.CSMColor1", RhiTextureFormat::Rgba16Float,
                       colorUsage, m_csmShadowColor1Handle)) {
        destroyCsmShadowTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroyCsmShadowTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }
    RhiTextureHandle* textures[] = {
        &m_csmShadowDepthHandle,
        &m_csmShadowDepthAllHandle,
        &m_csmShadowColor0Handle,
        &m_csmShadowColor1Handle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::registerRhiTextures() {
    const bool registered = m_gAlbedoHandle.isValid() &&
                            m_gNormalAoHandle.isValid() &&
                            m_gVoxelLightHandle.isValid() &&
                            m_gMaterialHandle.isValid() &&
                            m_gMaterialAuxHandle.isValid() &&
                            m_gDepthHandle.isValid() &&
                            m_sceneResolvedHandle.isValid() &&
                            m_halfResHandle.isValid() &&
                            m_reflectionTemporalScratchHandle.isValid() &&
                            m_historySceneHandle[0].isValid() &&
                            m_historySceneHandle[1].isValid() &&
                            m_historyDepthHandle[0].isValid() &&
                            m_historyDepthHandle[1].isValid() &&
                            m_taaHistoryDepthHandle[0].isValid() &&
                            m_taaHistoryDepthHandle[1].isValid() &&
                            m_historyReflectionHandle[0].isValid() &&
                            m_historyReflectionHandle[1].isValid() &&
                            m_historyCloudHandle[0].isValid() &&
                            m_historyCloudHandle[1].isValid() &&
                            m_historyVolumetricHandle[0].isValid() &&
                            m_historyVolumetricHandle[1].isValid() &&
                            m_temporalCurrentHandle.isValid() &&
                            m_velocityHandle.isValid() &&
                            m_perObjectVelocityHandle.isValid() &&
                            m_weatherMaskHandle.isValid() &&
                            m_reactiveMaskHandle.isValid() &&
                            m_transparencyMaskHandle.isValid() &&
                            m_skyCaptureHandle.isValid() &&
                            m_atmosphereLutHandle.isValid() &&
                            m_ssaoFilteredHandle.isValid() &&
                            m_ssaoHalfResHandle.isValid() &&
                            m_ssaoHalfResFilteredHandle.isValid() &&
                            m_ssaoHistoryHandle[0].isValid() &&
                            m_ssaoHistoryHandle[1].isValid() &&
                            m_ssaoTemporalHandle.isValid() &&
                            m_ssgiHandle.isValid() &&
                            m_ssgiHalfResHandle.isValid() &&
                            m_ssgiDenoiseHandle[0].isValid() &&
                            m_ssgiDenoiseHandle[1].isValid() &&
                            m_ssgiHistoryHandle[0].isValid() &&
                            m_ssgiHistoryHandle[1].isValid() &&
                            m_ssgiMomentsHistoryHandle[0].isValid() &&
                            m_ssgiMomentsHistoryHandle[1].isValid() &&
                            m_ssgiTemporalHandle.isValid() &&
                            m_ssgiTemporalMomentsHandle.isValid() &&
                            m_csmShadowDepthHandle.isValid() &&
                            m_csmShadowDepthAllHandle.isValid() &&
                            m_csmShadowColor0Handle.isValid() &&
                            m_csmShadowColor1Handle.isValid();
    if (!registered) {
        MECRAFT_LOG_STREAM(std::cerr << "DeferredRenderTargets: failed to register RHI texture handles\n");
        unregisterRhiTextures();
        return false;
    }
    return true;
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowDepthTextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowDepthView[cascadeIndex];
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowDepthAllTextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowDepthAllView[cascadeIndex];
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowColor0TextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowColor0View[cascadeIndex];
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowColor1TextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowColor1View[cascadeIndex];
}

bool DeferredRenderTargets::ensureCsmShadowDepthTextureView(RhiDevice& rhiDevice, const int cascadeIndex) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    RhiTextureViewHandle& view = m_csmShadowDepthView[cascadeIndex];
    if (view.isValid()) {
        return true;
    }

    if (!m_csmShadowDepthHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_csmShadowDepthHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Depth32Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = static_cast<uint32_t>(cascadeIndex);
    desc.layerCount = 1;

    view = rhiDevice.createTextureView(desc);
    if (!view.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureCsmShadowTransparentTextureViews(RhiDevice& rhiDevice,
                                                                   const int cascadeIndex) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    if (m_csmShadowDepthAllView[cascadeIndex].isValid() &&
        m_csmShadowColor0View[cascadeIndex].isValid() &&
        m_csmShadowColor1View[cascadeIndex].isValid()) {
        return true;
    }

    if (!m_csmShadowDepthAllHandle.isValid() ||
        !m_csmShadowColor0Handle.isValid() ||
        !m_csmShadowColor1Handle.isValid()) {
        return false;
    }

    const auto destroyCascadeViews = [&rhiDevice, this, cascadeIndex]() {
        RhiTextureViewHandle& depthView = m_csmShadowDepthAllView[cascadeIndex];
        RhiTextureViewHandle& color0View = m_csmShadowColor0View[cascadeIndex];
        RhiTextureViewHandle& color1View = m_csmShadowColor1View[cascadeIndex];
        if (depthView.isValid()) {
            rhiDevice.destroyTextureView(depthView);
        }
        if (color0View.isValid()) {
            rhiDevice.destroyTextureView(color0View);
        }
        if (color1View.isValid()) {
            rhiDevice.destroyTextureView(color1View);
        }
        depthView = {};
        color0View = {};
        color1View = {};
    };

    if (m_csmShadowDepthAllView[cascadeIndex].isValid() ||
        m_csmShadowColor0View[cascadeIndex].isValid() ||
        m_csmShadowColor1View[cascadeIndex].isValid()) {
        destroyCascadeViews();
    }

    RhiTextureViewDesc desc;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = static_cast<uint32_t>(cascadeIndex);
    desc.layerCount = 1;

    desc.texture = m_csmShadowDepthAllHandle;
    desc.format = RhiTextureFormat::Depth32Float;
    m_csmShadowDepthAllView[cascadeIndex] = rhiDevice.createTextureView(desc);

    desc.texture = m_csmShadowColor0Handle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_csmShadowColor0View[cascadeIndex] = rhiDevice.createTextureView(desc);

    desc.texture = m_csmShadowColor1Handle;
    desc.format = RhiTextureFormat::Rgba16Float;
    m_csmShadowColor1View[cascadeIndex] = rhiDevice.createTextureView(desc);

    if (!m_csmShadowDepthAllView[cascadeIndex].isValid() ||
        !m_csmShadowColor0View[cascadeIndex].isValid() ||
        !m_csmShadowColor1View[cascadeIndex].isValid()) {
        destroyCascadeViews();
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureVolumetricFogTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_atmosphereLutView.isValid() && m_csmShadowDepthArrayView.isValid() &&
        m_csmShadowDepthComparisonArrayView.isValid() &&
        m_csmShadowDepthAllArrayView.isValid() &&
        m_csmShadowDepthAllComparisonArrayView.isValid() &&
        m_csmShadowColor0ArrayView.isValid() && m_csmShadowColor1ArrayView.isValid()) {
        return true;
    }
    if (!m_atmosphereLutHandle.isValid() || !m_csmShadowDepthHandle.isValid() ||
        !m_csmShadowDepthAllHandle.isValid() || !m_csmShadowColor0Handle.isValid() ||
        !m_csmShadowColor1Handle.isValid()) {
        return false;
    }

    auto destroyCreatedViews = [&]() {
        RhiTextureViewHandle* views[] = {
            &m_atmosphereLutView,
            &m_csmShadowDepthArrayView,
            &m_csmShadowDepthComparisonArrayView,
            &m_csmShadowDepthAllArrayView,
            &m_csmShadowDepthAllComparisonArrayView,
            &m_csmShadowColor0ArrayView,
            &m_csmShadowColor1ArrayView
        };
        for (RhiTextureViewHandle* view : views) {
            if (view->isValid()) {
                rhiDevice.destroyTextureView(*view);
                *view = {};
            }
        }
    };

    RhiTextureViewDesc viewDesc;
    viewDesc.viewType = RhiTextureViewType::Texture3D;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = static_cast<uint32_t>(kAtmosphereLutDepth);
    viewDesc.texture = m_atmosphereLutHandle;
    viewDesc.format = RhiTextureFormat::Rgba32Float;
    m_atmosphereLutView = rhiDevice.createTextureView(viewDesc);

    viewDesc.viewType = RhiTextureViewType::Texture2DArray;
    viewDesc.format = RhiTextureFormat::Depth32Float;
    viewDesc.layerCount = static_cast<uint32_t>(kShadowCascadeCount);
    viewDesc.texture = m_csmShadowDepthHandle;
    m_csmShadowDepthArrayView = rhiDevice.createTextureView(viewDesc);
    viewDesc.depthCompare = true;
    m_csmShadowDepthComparisonArrayView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_csmShadowDepthAllHandle;
    viewDesc.depthCompare = false;
    m_csmShadowDepthAllArrayView = rhiDevice.createTextureView(viewDesc);
    viewDesc.depthCompare = true;
    m_csmShadowDepthAllComparisonArrayView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_csmShadowColor0Handle;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    viewDesc.depthCompare = false;
    m_csmShadowColor0ArrayView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_csmShadowColor1Handle;
    viewDesc.format = RhiTextureFormat::Rgba16Float;
    m_csmShadowColor1ArrayView = rhiDevice.createTextureView(viewDesc);

    if (!m_atmosphereLutView.isValid() || !m_csmShadowDepthArrayView.isValid() ||
        !m_csmShadowDepthComparisonArrayView.isValid() ||
        !m_csmShadowDepthAllArrayView.isValid() ||
        !m_csmShadowDepthAllComparisonArrayView.isValid() ||
        !m_csmShadowColor0ArrayView.isValid() || !m_csmShadowColor1ArrayView.isValid()) {
        destroyCreatedViews();
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureGBufferTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    const bool allViewsValid = m_gAlbedoView.isValid() &&
                               m_gNormalAoView.isValid() &&
                               m_gVoxelLightView.isValid() &&
                               m_gMaterialView.isValid() &&
                               m_gMaterialAuxView.isValid() &&
                               m_gDepthView.isValid();
    if (allViewsValid) {
        return true;
    }

    if (!m_gAlbedoHandle.isValid() ||
        !m_gNormalAoHandle.isValid() ||
        !m_gVoxelLightHandle.isValid() ||
        !m_gMaterialHandle.isValid() ||
        !m_gMaterialAuxHandle.isValid() ||
        !m_gDepthHandle.isValid()) {
        return false;
    }

    const auto destroyGBufferViews = [&rhiDevice, this]() {
        if (m_gAlbedoView.isValid()) {
            rhiDevice.destroyTextureView(m_gAlbedoView);
        }
        if (m_gNormalAoView.isValid()) {
            rhiDevice.destroyTextureView(m_gNormalAoView);
        }
        if (m_gVoxelLightView.isValid()) {
            rhiDevice.destroyTextureView(m_gVoxelLightView);
        }
        if (m_gMaterialView.isValid()) {
            rhiDevice.destroyTextureView(m_gMaterialView);
        }
        if (m_gMaterialAuxView.isValid()) {
            rhiDevice.destroyTextureView(m_gMaterialAuxView);
        }
        if (m_gDepthView.isValid()) {
            rhiDevice.destroyTextureView(m_gDepthView);
        }
        m_gAlbedoView = {};
        m_gNormalAoView = {};
        m_gVoxelLightView = {};
        m_gMaterialView = {};
        m_gMaterialAuxView = {};
        m_gDepthView = {};
    };

    const bool anyViewValid = m_gAlbedoView.isValid() ||
                              m_gNormalAoView.isValid() ||
                              m_gVoxelLightView.isValid() ||
                              m_gMaterialView.isValid() ||
                              m_gMaterialAuxView.isValid() ||
                              m_gDepthView.isValid();
    if (anyViewValid) {
        destroyGBufferViews();
    }

    RhiTextureViewDesc desc;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    desc.texture = m_gAlbedoHandle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_gAlbedoView = rhiDevice.createTextureView(desc);

    desc.texture = m_gNormalAoHandle;
    desc.format = RhiTextureFormat::Rgb10A2Unorm;
    m_gNormalAoView = rhiDevice.createTextureView(desc);

    desc.texture = m_gVoxelLightHandle;
    desc.format = RhiTextureFormat::Rg8Unorm;
    m_gVoxelLightView = rhiDevice.createTextureView(desc);

    desc.texture = m_gMaterialHandle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_gMaterialView = rhiDevice.createTextureView(desc);

    desc.texture = m_gMaterialAuxHandle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_gMaterialAuxView = rhiDevice.createTextureView(desc);

    desc.texture = m_gDepthHandle;
    desc.format = RhiTextureFormat::Depth32Float;
    m_gDepthView = rhiDevice.createTextureView(desc);

    if (!m_gAlbedoView.isValid() ||
        !m_gNormalAoView.isValid() ||
        !m_gVoxelLightView.isValid() ||
        !m_gMaterialView.isValid() ||
        !m_gMaterialAuxView.isValid() ||
        !m_gDepthView.isValid()) {
        destroyGBufferViews();
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHiZTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (!m_hiZMipViews.empty() && m_hiZFullView.isValid()) {
        return true;
    }
    if (!m_hiZHandle.isValid() || m_hiZMipCount == 0u) {
        return false;
    }
    m_rhiViewDevice = &rhiDevice;
    m_hiZMipViews.resize(m_hiZMipCount);
    for (uint32_t mip = 0u; mip < m_hiZMipCount; ++mip) {
        RhiTextureViewDesc desc;
        desc.texture = m_hiZHandle;
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::R32Float;
        desc.baseMip = mip;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;
        m_hiZMipViews[mip] = rhiDevice.createTextureView(desc);
        if (!m_hiZMipViews[mip].isValid()) {
            for (RhiTextureViewHandle& view : m_hiZMipViews) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
            }
            m_hiZMipViews.clear();
            return false;
        }
    }
    if (!m_hiZFullView.isValid()) {
        RhiTextureViewDesc desc;
        desc.texture = m_hiZHandle;
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::R32Float;
        desc.baseMip = 0;
        desc.mipCount = m_hiZMipCount;
        desc.baseLayer = 0;
        desc.layerCount = 1;
        m_hiZFullView = rhiDevice.createTextureView(desc);
        if (!m_hiZFullView.isValid()) {
            return false;
        }
    }
    return true;
}

bool DeferredRenderTargets::ensureVelocityTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_velocityView.isValid()) {
        return true;
    }

    if (!m_velocityHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_velocityHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rg16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_velocityView = rhiDevice.createTextureView(desc);
    if (!m_velocityView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensurePerObjectVelocityTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_perObjectVelocityView.isValid()) {
        return true;
    }

    if (!m_perObjectVelocityHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_perObjectVelocityHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rg16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_perObjectVelocityView = rhiDevice.createTextureView(desc);
    if (!m_perObjectVelocityView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoFilteredTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoFilteredView.isValid()) {
        return true;
    }

    if (!m_ssaoFilteredHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoFilteredHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoFilteredView = rhiDevice.createTextureView(desc);
    if (!m_ssaoFilteredView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoHalfResTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoHalfResView.isValid()) {
        return true;
    }

    if (!m_ssaoHalfResHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoHalfResHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoHalfResView = rhiDevice.createTextureView(desc);
    if (!m_ssaoHalfResView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoHalfResFilteredTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoHalfResFilteredView.isValid()) {
        return true;
    }

    if (!m_ssaoHalfResFilteredHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoHalfResFilteredHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoHalfResFilteredView = rhiDevice.createTextureView(desc);
    if (!m_ssaoHalfResFilteredView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoTemporalTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoTemporalView.isValid()) {
        return true;
    }

    if (!m_ssaoTemporalHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoTemporalHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoTemporalView = rhiDevice.createTextureView(desc);
    if (!m_ssaoTemporalView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoHistoryTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoHistoryView[0].isValid() && m_ssaoHistoryView[1].isValid()) {
        return true;
    }
    if (!m_ssaoHistoryHandle[0].isValid() || !m_ssaoHistoryHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_ssaoHistoryView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_ssaoHistoryHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::R8Unorm;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_ssaoHistoryView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_ssaoHistoryView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_ssaoHistoryView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSceneLightingTextureView(RhiDevice&) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    // SceneLighting is a render-graph transient: the graph publishes its
    // default view per frame, so this class never creates or destroys one.
    return m_sceneLightingView.isValid();
}

bool DeferredRenderTargets::ensureSsgiTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiView.isValid()) {
        return true;
    }

    if (!m_ssgiHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssgiHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssgiView = rhiDevice.createTextureView(desc);
    if (!m_ssgiView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiHalfResTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiHalfResView.isValid()) {
        return true;
    }

    if (!m_ssgiHalfResHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssgiHalfResHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssgiHalfResView = rhiDevice.createTextureView(desc);
    if (!m_ssgiHalfResView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiDenoiseTextureView(RhiDevice& rhiDevice, const int slot) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    assert(slot >= 0 && slot < 2);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiDenoiseView[slot].isValid()) {
        return true;
    }

    if (!m_ssgiDenoiseHandle[slot].isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssgiDenoiseHandle[slot];
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssgiDenoiseView[slot] = rhiDevice.createTextureView(desc);
    if (!m_ssgiDenoiseView[slot].isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiTemporalTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiTemporalView.isValid() && m_ssgiTemporalMomentsView.isValid()) {
        return true;
    }

    if (!m_ssgiTemporalHandle.isValid() || !m_ssgiTemporalMomentsHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc temporalDesc;
    temporalDesc.texture = m_ssgiTemporalHandle;
    temporalDesc.viewType = RhiTextureViewType::Texture2D;
    temporalDesc.format = RhiTextureFormat::Rgba16Float;
    temporalDesc.baseMip = 0;
    temporalDesc.mipCount = 1;
    temporalDesc.baseLayer = 0;
    temporalDesc.layerCount = 1;

    RhiTextureViewDesc momentsDesc = temporalDesc;
    momentsDesc.texture = m_ssgiTemporalMomentsHandle;

    m_ssgiTemporalView = rhiDevice.createTextureView(temporalDesc);
    m_ssgiTemporalMomentsView = rhiDevice.createTextureView(momentsDesc);
    if (!m_ssgiTemporalView.isValid() || !m_ssgiTemporalMomentsView.isValid()) {
        if (m_ssgiTemporalView.isValid()) {
            rhiDevice.destroyTextureView(m_ssgiTemporalView);
        }
        if (m_ssgiTemporalMomentsView.isValid()) {
            rhiDevice.destroyTextureView(m_ssgiTemporalMomentsView);
        }
        m_ssgiTemporalView = {};
        m_ssgiTemporalMomentsView = {};
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiHistoryTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiHistoryView[0].isValid() && m_ssgiHistoryView[1].isValid() &&
        m_ssgiMomentsHistoryView[0].isValid() && m_ssgiMomentsHistoryView[1].isValid()) {
        return true;
    }
    if (!m_ssgiHistoryHandle[0].isValid() || !m_ssgiHistoryHandle[1].isValid() ||
        !m_ssgiMomentsHistoryHandle[0].isValid() || !m_ssgiMomentsHistoryHandle[1].isValid()) {
        return false;
    }

    auto destroyCreatedViews = [&]() {
        for (RhiTextureViewHandle& view : m_ssgiHistoryView) {
            if (view.isValid()) {
                rhiDevice.destroyTextureView(view);
            }
            view = {};
        }
        for (RhiTextureViewHandle& view : m_ssgiMomentsHistoryView) {
            if (view.isValid()) {
                rhiDevice.destroyTextureView(view);
            }
            view = {};
        }
    };

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        RhiTextureViewDesc desc;
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0u;
        desc.mipCount = 1u;
        desc.baseLayer = 0u;
        desc.layerCount = 1u;

        if (!m_ssgiHistoryView[historyIndex].isValid()) {
            desc.texture = m_ssgiHistoryHandle[historyIndex];
            m_ssgiHistoryView[historyIndex] = rhiDevice.createTextureView(desc);
        }
        if (!m_ssgiMomentsHistoryView[historyIndex].isValid()) {
            desc.texture = m_ssgiMomentsHistoryHandle[historyIndex];
            m_ssgiMomentsHistoryView[historyIndex] = rhiDevice.createTextureView(desc);
        }
        if (!m_ssgiHistoryView[historyIndex].isValid() ||
            !m_ssgiMomentsHistoryView[historyIndex].isValid()) {
            destroyCreatedViews();
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureCloudTextureView(RhiDevice&) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    // Cloud is a render-graph transient: the graph publishes its default
    // view per frame, so this class never creates or destroys one.
    return m_cloudView.isValid();
}

bool DeferredRenderTargets::ensureSkyCaptureTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_skyCaptureView.isValid()) {
        return true;
    }

    if (!m_skyCaptureHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_skyCaptureHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_skyCaptureView = rhiDevice.createTextureView(desc);
    if (!m_skyCaptureView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureReflectionTextureView(RhiDevice&) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    // Reflection is a render-graph transient: the graph publishes its
    // default view per frame, so this class never creates or destroys one.
    return m_reflectionView.isValid();
}

bool DeferredRenderTargets::ensureReflectionTemporalScratchTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_reflectionTemporalScratchView.isValid()) {
        return true;
    }

    if (!m_reflectionTemporalScratchHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_reflectionTemporalScratchHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_reflectionTemporalScratchView = rhiDevice.createTextureView(desc);
    if (!m_reflectionTemporalScratchView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSceneCompositeTextureView(RhiDevice&) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    // SceneComposite is a render-graph transient: the graph publishes its
    // default view per frame, so this class never creates or destroys one.
    return m_sceneCompositeView.isValid();
}

bool DeferredRenderTargets::ensureSceneResolvedTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_sceneResolvedView.isValid()) {
        return true;
    }

    if (!m_sceneResolvedHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_sceneResolvedHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_sceneResolvedView = rhiDevice.createTextureView(desc);
    if (!m_sceneResolvedView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistorySceneTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    const int historyIndex = m_currentHistoryIndex;
    if (m_historySceneView[historyIndex].isValid()) {
        return true;
    }

    if (!m_historySceneHandle[historyIndex].isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_historySceneHandle[historyIndex];
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_historySceneView[historyIndex] = rhiDevice.createTextureView(desc);
    if (!m_historySceneView[historyIndex].isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistorySceneTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historySceneView[0].isValid() && m_historySceneView[1].isValid()) {
        return true;
    }
    if (!m_historySceneHandle[0].isValid() || !m_historySceneHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historySceneView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historySceneHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historySceneView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historySceneView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historySceneView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryDepthTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyDepthView[0].isValid() && m_historyDepthView[1].isValid()) {
        return true;
    }
    if (!m_historyDepthHandle[0].isValid() || !m_historyDepthHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyDepthView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyDepthHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Depth32Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historyDepthView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyDepthView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyDepthView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureTaaHistoryDepthTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_taaHistoryDepthView[0].isValid() &&
        m_taaHistoryDepthView[1].isValid()) {
        return true;
    }
    if (!m_taaHistoryDepthHandle[0].isValid() ||
        !m_taaHistoryDepthHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_taaHistoryDepthView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_taaHistoryDepthHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Depth32Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_taaHistoryDepthView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_taaHistoryDepthView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_taaHistoryDepthView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryReflectionTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyReflectionView[0].isValid() && m_historyReflectionView[1].isValid()) {
        return true;
    }
    if (!m_historyReflectionHandle[0].isValid() || !m_historyReflectionHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyReflectionView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyReflectionHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historyReflectionView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyReflectionView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyReflectionView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryCloudTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyCloudView[0].isValid() && m_historyCloudView[1].isValid()) {
        return true;
    }
    if (!m_historyCloudHandle[0].isValid() || !m_historyCloudHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyCloudView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyCloudHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0u;
        desc.mipCount = 1u;
        desc.baseLayer = 0u;
        desc.layerCount = 1u;

        m_historyCloudView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyCloudView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyCloudView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureTransparentCompositeTextureViews(RhiDevice&) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    // TransparentComposite and its depth are render-graph transients: the
    // graph publishes their default views per frame, so this class never
    // creates or destroys them.
    return m_transparentCompositeView.isValid() &&
           m_transparentCompositeDepthView.isValid();
}

bool DeferredRenderTargets::ensureHalfResTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_halfResView.isValid()) {
        return true;
    }

    if (!m_halfResHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_halfResHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_halfResView = rhiDevice.createTextureView(desc);
    if (!m_halfResView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureTemporalCurrentTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_temporalCurrentView.isValid()) {
        return true;
    }

    if (!m_temporalCurrentHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_temporalCurrentHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_temporalCurrentView = rhiDevice.createTextureView(desc);
    if (!m_temporalCurrentView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryVolumetricTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    RhiTextureViewHandle& view = m_historyVolumetricView[m_currentHistoryIndex];
    if (view.isValid()) {
        return true;
    }

    const RhiTextureHandle texture = m_historyVolumetricHandle[m_currentHistoryIndex];
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    view = rhiDevice.createTextureView(desc);
    if (!view.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryVolumetricTextureViews(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyVolumetricView[0].isValid() && m_historyVolumetricView[1].isValid()) {
        return true;
    }
    if (!m_historyVolumetricHandle[0].isValid() || !m_historyVolumetricHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyVolumetricView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyVolumetricHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historyVolumetricView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyVolumetricView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyVolumetricView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureWeatherMaskTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_weatherMaskView.isValid()) {
        return true;
    }

    if (!m_weatherMaskHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_weatherMaskHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_weatherMaskView = rhiDevice.createTextureView(desc);
    if (!m_weatherMaskView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureReactiveMaskTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_reactiveMaskView.isValid()) {
        return true;
    }
    if (!m_reactiveMaskHandle.isValid()) {
        return false;
    }
    RhiTextureViewDesc desc;
    desc.texture = m_reactiveMaskHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0u;
    desc.mipCount = 1u;
    desc.baseLayer = 0u;
    desc.layerCount = 1u;
    m_reactiveMaskView = rhiDevice.createTextureView(desc);
    if (!m_reactiveMaskView.isValid()) {
        return false;
    }
    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureTransparencyMaskTextureView(RhiDevice& rhiDevice) {
    const std::lock_guard<std::recursive_mutex> viewLock(m_rhiViewMutex);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_transparencyMaskView.isValid()) {
        return true;
    }
    if (!m_transparencyMaskHandle.isValid()) {
        return false;
    }
    RhiTextureViewDesc desc;
    desc.texture = m_transparencyMaskHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0u;
    desc.mipCount = 1u;
    desc.baseLayer = 0u;
    desc.layerCount = 1u;
    m_transparencyMaskView = rhiDevice.createTextureView(desc);
    if (!m_transparencyMaskView.isValid()) {
        return false;
    }
    m_rhiViewDevice = &rhiDevice;
    return true;
}

void DeferredRenderTargets::destroyRhiTextureViews() {
    if (m_rhiViewDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_hiZMipViews) {
            if (view.isValid()) {
                m_rhiViewDevice->destroyTextureView(view);
            }
        }
        if (m_hiZFullView.isValid()) {
            m_rhiViewDevice->destroyTextureView(m_hiZFullView);
        }
    }
    m_hiZMipViews.clear();
    m_hiZFullView = {};
    if (m_rhiViewDevice != nullptr && m_gAlbedoView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gAlbedoView);
    }
    if (m_rhiViewDevice != nullptr && m_gNormalAoView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gNormalAoView);
    }
    if (m_rhiViewDevice != nullptr && m_gVoxelLightView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gVoxelLightView);
    }
    if (m_rhiViewDevice != nullptr && m_gMaterialView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gMaterialView);
    }
    if (m_rhiViewDevice != nullptr && m_gMaterialAuxView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gMaterialAuxView);
    }
    if (m_rhiViewDevice != nullptr && m_gDepthView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gDepthView);
    }
    for (RhiTextureViewHandle& view : m_csmShadowDepthView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_csmShadowDepthAllView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_csmShadowColor0View) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_csmShadowColor1View) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    RhiTextureViewHandle* volumetricFogViews[] = {
        &m_atmosphereLutView,
        &m_csmShadowDepthArrayView,
        &m_csmShadowDepthComparisonArrayView,
        &m_csmShadowDepthAllArrayView,
        &m_csmShadowDepthAllComparisonArrayView,
        &m_csmShadowColor0ArrayView,
        &m_csmShadowColor1ArrayView
    };
    for (RhiTextureViewHandle* view : volumetricFogViews) {
        if (m_rhiViewDevice != nullptr && view->isValid()) {
            m_rhiViewDevice->destroyTextureView(*view);
        }
        *view = {};
    }
    if (m_rhiViewDevice != nullptr && m_velocityView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_velocityView);
    }
    if (m_rhiViewDevice != nullptr && m_perObjectVelocityView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_perObjectVelocityView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoFilteredView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoFilteredView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoHalfResView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoHalfResView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoHalfResFilteredView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoHalfResFilteredView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoTemporalView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoTemporalView);
    }
    for (RhiTextureViewHandle& view : m_ssaoHistoryView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    // SceneLighting/SceneComposite/TransparentComposite/Reflection/Cloud
    // views are graph-owned per-frame publications; they are only reset
    // below, never destroyed here.
    if (m_rhiViewDevice != nullptr && m_ssgiView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiView);
    }
    if (m_rhiViewDevice != nullptr && m_ssgiHalfResView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiHalfResView);
    }
    for (RhiTextureViewHandle& view : m_ssgiDenoiseView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_ssgiHistoryView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_ssgiMomentsHistoryView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    if (m_rhiViewDevice != nullptr && m_ssgiTemporalView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiTemporalView);
    }
    if (m_rhiViewDevice != nullptr && m_ssgiTemporalMomentsView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiTemporalMomentsView);
    }
    if (m_rhiViewDevice != nullptr && m_sceneResolvedView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_sceneResolvedView);
    }
    if (m_rhiViewDevice != nullptr && m_halfResView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_halfResView);
    }
    if (m_rhiViewDevice != nullptr && m_reflectionTemporalScratchView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_reflectionTemporalScratchView);
    }
    if (m_rhiViewDevice != nullptr && m_skyCaptureView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_skyCaptureView);
    }
    for (RhiTextureViewHandle& view : m_historySceneView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyDepthView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_taaHistoryDepthView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyVolumetricView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyReflectionView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyCloudView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    if (m_rhiViewDevice != nullptr && m_temporalCurrentView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_temporalCurrentView);
    }
    if (m_rhiViewDevice != nullptr && m_weatherMaskView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_weatherMaskView);
    }
    if (m_rhiViewDevice != nullptr && m_reactiveMaskView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_reactiveMaskView);
    }
    if (m_rhiViewDevice != nullptr && m_transparencyMaskView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_transparencyMaskView);
    }
    m_gAlbedoView = {};
    m_gNormalAoView = {};
    m_gVoxelLightView = {};
    m_gMaterialView = {};
    m_gMaterialAuxView = {};
    m_gDepthView = {};
    m_atmosphereLutView = {};
    m_csmShadowDepthArrayView = {};
    m_csmShadowDepthComparisonArrayView = {};
    m_csmShadowDepthAllArrayView = {};
    m_csmShadowDepthAllComparisonArrayView = {};
    m_csmShadowColor0ArrayView = {};
    m_csmShadowColor1ArrayView = {};
    m_velocityView = {};
    m_perObjectVelocityView = {};
    m_ssaoFilteredView = {};
    m_ssaoHalfResView = {};
    m_ssaoHalfResFilteredView = {};
    m_ssaoTemporalView = {};
    m_ssaoHistoryView[0] = {};
    m_ssaoHistoryView[1] = {};
    m_sceneLightingView = {};
    m_ssgiView = {};
    m_ssgiHalfResView = {};
    m_ssgiTemporalView = {};
    m_ssgiTemporalMomentsView = {};
    m_ssgiHistoryView[0] = {};
    m_ssgiHistoryView[1] = {};
    m_ssgiMomentsHistoryView[0] = {};
    m_ssgiMomentsHistoryView[1] = {};
    m_sceneCompositeView = {};
    m_sceneResolvedView = {};
    m_transparentCompositeView = {};
    m_transparentCompositeDepthView = {};
    m_halfResView = {};
    m_reflectionView = {};
    m_reflectionTemporalScratchView = {};
    m_cloudView = {};
    m_skyCaptureView = {};
    m_historySceneView[0] = {};
    m_historySceneView[1] = {};
    m_historyDepthView[0] = {};
    m_historyDepthView[1] = {};
    m_taaHistoryDepthView[0] = {};
    m_taaHistoryDepthView[1] = {};
    m_historyReflectionView[0] = {};
    m_historyReflectionView[1] = {};
    m_historyCloudView[0] = {};
    m_historyCloudView[1] = {};
    m_temporalCurrentView = {};
    m_weatherMaskView = {};
    m_reactiveMaskView = {};
    m_transparencyMaskView = {};
    m_rhiViewDevice = nullptr;
}

void DeferredRenderTargets::unregisterRhiTextures() {
    destroyRhiTextureViews();
    destroyGBufferTextures();
    destroySceneTextures();
    destroyScreenEffectTextures();
    destroyAtmosphereTextures();
    destroySceneHistoryTextures();
    destroyEffectHistoryTextures();
    destroyMotionTextures();
    destroySsaoTextures();
    destroySsgiTextures();
    destroyCsmShadowTextures();
    // Graph-published transient targets are owned by the render graph; drop
    // the non-owning handles without destroying any device resources.
    m_sceneLightingHandle = {};
    m_sceneCompositeHandle = {};
    m_transparentCompositeHandle = {};
    m_transparentCompositeDepthHandle = {};
    m_reflectionHandle = {};
    m_cloudHandle = {};
}

void DeferredRenderTargets::destroyFramebuffers() {
    m_textureStates.clear();
    unregisterRhiTextures();

    m_currentHistoryIndex = 0;
    m_ssaoHistoryIndex = 0;
    m_ssgiHistoryIndex = 0;
    m_ready = false;
}

void DeferredRenderTargets::destroyAtmosphereLutTexture() {
    if (m_atmosphereLutView.isValid() && m_rhiViewDevice != nullptr) {
        m_rhiViewDevice->destroyTextureView(m_atmosphereLutView);
        m_atmosphereLutView = {};
    }
    if (m_atmosphereLutHandle.isValid() && m_rhiDevice != nullptr) {
        m_rhiDevice->destroyTexture(m_atmosphereLutHandle);
        m_atmosphereLutHandle = {};
    }
}

bool DeferredRenderTargets::loadAtmosphereLut(const char* path) {
    destroyAtmosphereLutTexture();

    // Final.lut layout: 256 x 128 x 33, RGBA32F
    constexpr size_t kExpectedSize =
        size_t(kAtmosphereLutWidth) * kAtmosphereLutHeight * kAtmosphereLutDepth * 4 * sizeof(float);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: failed to open " << path << "\n");
        return false;
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != kExpectedSize) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: unexpected file size " << fileSize
                                     << " (expected " << kExpectedSize << ")\n");
        return false;
    }

    file.seekg(0, std::ios::beg);
    std::vector<float> data(kAtmosphereLutWidth * kAtmosphereLutHeight * kAtmosphereLutDepth * 4);
    if (!file.read(reinterpret_cast<char*>(data.data()), kExpectedSize)) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: failed to read data\n");
        return false;
    }

    if (m_rhiDevice == nullptr) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: RHI device is not initialized\n");
        return false;
    }

    RhiTextureDesc desc;
    desc.debugName = "DeferredTargets.AtmosphereLUT";
    desc.memoryCategory = RhiMemoryCategory::Texture;
    desc.dimension = RhiTextureDimension::Texture3D;
    desc.format = RhiTextureFormat::Rgba32Float;
    desc.width = static_cast<uint32_t>(kAtmosphereLutWidth);
    desc.height = static_cast<uint32_t>(kAtmosphereLutHeight);
    desc.depthOrLayers = static_cast<uint32_t>(kAtmosphereLutDepth);
    desc.mipLevels = 1u;
    desc.sampleCount = 1u;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                 rhiFlag(RhiTextureUsage::TransferDst);

    RhiTextureInitialData initialData;
    initialData.pixels = data.data();
    initialData.sizeBytes = kExpectedSize;
    initialData.layerCount = static_cast<uint32_t>(kAtmosphereLutDepth);
    initialData.finalState = RhiResourceState::ShaderRead;
    m_atmosphereLutHandle = m_rhiDevice->createTexture(desc, &initialData);
    if (!m_atmosphereLutHandle.isValid()) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: failed to create RHI texture\n");
        return false;
    }

    MECRAFT_LOG_STREAM(std::cout << "AtmosphereLUT: loaded " << path << " (256x128x33 RGBA32F)\n");
    return true;
}
