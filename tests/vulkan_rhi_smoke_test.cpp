#include "renderer/core/GlobalBindlessSet.h"
#include "renderer/core/GpuSceneBufferSet.h"
#include "renderer/core/RenderScene.h"
#include "renderer/contracts/CubeMapContract.h"
#include "renderer/contracts/GpuLightContract.h"
#include "renderer/contracts/GpuMaterialContract.h"
#include "renderer/contracts/LocalShadowContract.h"
#include "renderer/contracts/RtgiNrdSignalContract.h"
#include "renderer/contracts/RtgiSamplingContract.h"
#include "renderer/contracts/StaticMeshRayTracingContract.h"
#include "renderer/mesh/TerrainBlasCache.h"
#include "renderer/passes/ClusteredLightingPass.h"
#include "renderer/passes/NrdGuidePrepPass.h"
#include "renderer/passes/RtgiSignalPackPass.h"
#include "renderer/passes/RtgiTracePass.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiRenderGraph.h"
#include "renderer/rhi/SceneTlasCache.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"
#include "renderer/rhi/StaticMeshBlasCache.h"
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"
#include "renderer/passes/TemporalUpscalePass.h"
#include "renderer/presentation/PresentationController.h"
#include "thread/ThreadPool.h"

#if defined(MECRAFT_ENABLE_FSR31)
#include "renderer/upscaling/Fsr31VulkanContext.h"
#endif
#if defined(MECRAFT_ENABLE_NRD)
#include "renderer/nrd/NrdRenderGraphBridge.h"
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
#include "renderer/upscaling/DlssVulkanContext.h"
#include "renderer/upscaling/StreamlineRuntime.h"
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

namespace {

enum class FrameAttempt { Success, Retry, Error };

struct RtgiTraceSmokeExpectedPixel final {
    renderer::contracts::RtgiTraceClassification classification = renderer::contracts::RtgiTraceClassification::Miss;
    uint32_t candidateCount = 0u;
    uint32_t confirmedCount = 0u;
    uint32_t hitIdentityHash = 0u;
    float minimumHitDistance = 0.0f;
    float maximumHitDistance = 0.0f;
    glm::vec3 minimumRadiance{0.0f};
    glm::vec3 maximumRadiance{0.0f};
};

struct RtgiTraceSmokeCase final {
    const char* label = nullptr;
    uint32_t width = 0u;
    uint32_t height = 0u;
    std::vector<float> depth;
    std::vector<float> normalAo;
    std::vector<float> materialAux;
    std::vector<uint8_t> voxelLight;
    std::vector<float> blueNoise;
    uint32_t terrainAlbedoWidth = 0u;
    uint32_t terrainAlbedoHeight = 0u;
    uint32_t terrainAlbedoLayers = 0u;
    std::vector<uint8_t> terrainAlbedo;
    std::array<uint8_t, 4u> grassColormap{255u, 255u, 255u, 255u};
    std::array<uint8_t, 4u> foliageColormap{255u, 255u, 255u, 255u};
    glm::vec3 skyCaptureRadiance{0.0f};
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 moonDirection{0.0f, -1.0f, 0.0f};
    glm::vec3 sunRadiance{0.0f};
    glm::vec3 moonRadiance{0.0f};
    glm::vec3 skyAmbientRadiance{0.0f};
    float sunVisibility = 0.0f;
    float moonVisibility = 0.0f;
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    float animationTime = 0.0f;
    float preExposure = 1.0f;
    float previousPreExposure = 1.0f;
    float maxRayDistance = 10.0f;
    float maxShadowRayDistance = 16.0f;
    float minimumRayOriginBias = 0.001f;
    uint8_t instanceMask = 0u;
    uint8_t shadowInstanceMask = renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::ShadowCaster);
    bool terrainNormalMapsEnabled = true;
    bool terrainSpecularMapsEnabled = true;
    std::vector<renderer::contracts::GpuLight> lights;
    std::vector<RtgiTraceSmokeExpectedPixel> expectedPixels;
};

[[nodiscard]] bool validateRtgiTraceCase(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                         renderer::rt::SceneTlasCache& sceneTlas,
                                         const renderer::rt::SceneTlasView& activeTlas,
                                         renderer::core::GlobalBindlessSet& globalBindlessSet,
                                         const RtgiTraceSmokeCase& smokeCase);

[[nodiscard]] bool validateGpuBufferContents(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                             const RhiBufferHandle source, const RhiResourceState sourceState,
                                             const void* expected, const uint64_t byteCount, const char* debugName) {
    if (!source.isValid() || expected == nullptr || byteCount == 0u ||
        byteCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) || debugName == nullptr) {
        return false;
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = debugName;
    readbackDesc.size = byteCount;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return false;
    }

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    bool valid = commands != nullptr && commands->begin({debugName, RhiCommandListType::Graphics});
    if (valid) {
        commands->bufferBarrier({source, sourceState, RhiResourceState::TransferSrc});
        commands->copyBuffer({source, readback, 0u, 0u, byteCount});
        commands->bufferBarrier({source, RhiResourceState::TransferSrc, sourceState});
        commands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
        valid = commands->end();
    }

    RhiSubmissionToken token;
    if (valid) {
        RhiCommandList* submissions[] = {commands};
        valid = device.submit({debugName, submissions, 1u, RhiQueueType::Graphics}, &token) &&
                device.waitForSubmission(token);
    }
    if (valid) {
        const void* mapped = device.mapBuffer(readback, 0u, byteCount);
        valid = mapped != nullptr && std::memcmp(mapped, expected, static_cast<size_t>(byteCount)) == 0;
        if (mapped != nullptr) {
            device.unmapBuffer(readback);
        }
    }
    device.destroyBuffer(readback);
    return valid;
}

[[nodiscard]] bool validateIndependentUiPresentation(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                                     GLFWwindow* window) {
    std::unique_ptr<PresentationBackend> backend = createNativePresentationBackend(device);
    PresentationController controller(*backend);
    if (!controller.initUiComposition(device)) {
        return false;
    }

    RhiTextureHandle firstUiTexture;
    for (uint32_t frameIndex = 0u; frameIndex < 4u; ++frameIndex) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        const PresentationFrame frame = controller.beginFrame(width, height);
        if (!frame.shouldRender()) {
            return false;
        }
        const std::optional<PresentationUiFrame> uiFrame = controller.acquireUiFrame(frame);
        if (!uiFrame.has_value() || uiFrame->width != frame.acquired.width ||
            uiFrame->height != frame.acquired.height || uiFrame->colorFormat != device.swapchainColorFormat() ||
            !uiFrame->premultipliedAlpha || !uiFrame->colorTexture.isValid() || !uiFrame->colorView.isValid()) {
            return false;
        }
        const std::optional<PresentationFrameResources> resources = controller.frameResources(frame, *uiFrame);
        if (!resources.has_value() || resources->realFrameNumber != frame.realFrameNumber ||
            resources->width != frame.acquired.width || resources->height != frame.acquired.height ||
            !resources->uiPremultipliedAlpha) {
            return false;
        }
        if (frameIndex == 0u) {
            firstUiTexture = uiFrame->colorTexture;
        } else if (frameIndex == 1u && firstUiTexture.index == uiFrame->colorTexture.index &&
                   firstUiTexture.generation == uiFrame->colorTexture.generation) {
            return false;
        }

        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
        if (commands == nullptr ||
            !commands->begin({"VulkanSmoke.IndependentUiPresentation", RhiCommandListType::Graphics}) ||
            !controller.beginUiRendering(*commands, *uiFrame) ||
            !controller.endUiRenderingAndComposite(*commands, frame, *uiFrame) ||
            !controller.submitUiFrame(*commands, *uiFrame)) {
            return false;
        }
        const PresentationCompleteResult presented = controller.presentFrame(frame);
        if (presented.result != PresentationResult::Presented) {
            return false;
        }
    }

    const PresentationStatistics& statistics = controller.statistics();
    const bool validStatistics = statistics.realFramesAcquired == 4u && statistics.realFramesPresented == 4u &&
                                 statistics.displayedFrames == 4u && statistics.generatedFramesPresented == 0u &&
                                 statistics.failedOperations == 0u;
    controller.shutdownUiComposition();
    return validStatistics;
}

[[nodiscard]] bool validateVulkanInterop(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                         const RhiTextureHandle texture, const RhiTextureViewHandle view,
                                         const uint32_t width, const uint32_t height, const uint32_t depth) {
    const auto deviceInfo = VkRhiInterop::deviceInfo(device);
    const auto textureInfo = VkRhiInterop::textureInfo(device, texture, view);
    constexpr VkPipelineStageFlags2 kAccelerationStructureWriteStages =
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR;
    constexpr VkPipelineStageFlags2 kAccelerationStructureReadStages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                                                                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                                                       kAccelerationStructureWriteStages;
    if (!deviceInfo.has_value() || deviceInfo->instance == VK_NULL_HANDLE ||
        deviceInfo->physicalDevice == VK_NULL_HANDLE || deviceInfo->device == VK_NULL_HANDLE ||
        deviceInfo->graphicsQueue == VK_NULL_HANDLE || deviceInfo->graphicsQueueFamily == VK_QUEUE_FAMILY_IGNORED ||
        !textureInfo.has_value() || textureInfo->image == VK_NULL_HANDLE || textureInfo->view == VK_NULL_HANDLE ||
        textureInfo->format != VK_FORMAT_R32G32B32A32_SFLOAT || textureInfo->extent.width != width ||
        textureInfo->extent.height != height || textureInfo->extent.depth != depth || textureInfo->arrayLayers != 1u ||
        textureInfo->imageType != VK_IMAGE_TYPE_3D || textureInfo->viewType != VK_IMAGE_VIEW_TYPE_3D ||
        textureInfo->mipCount != 1u || textureInfo->layerCount != 1u ||
        textureInfo->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        VkRhiInterop::textureInfo(device, {}, view).has_value() ||
        VkRhiInterop::resourceLayout(RhiResourceState::ShaderRead) != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        VkRhiInterop::resourceStages(RhiResourceState::AccelerationStructureBuildWrite) !=
            kAccelerationStructureWriteStages ||
        VkRhiInterop::resourceStages(RhiResourceState::AccelerationStructureRead) != kAccelerationStructureReadStages ||
        VkRhiInterop::resourceAccess(RhiResourceState::AccelerationStructureBuildInput) !=
            VK_ACCESS_2_SHADER_READ_BIT) {
        return false;
    }

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr || !commands->begin({"VulkanSmoke.Interop", RhiCommandListType::Graphics})) {
        return false;
    }
    const auto nativeCommands = VkRhiInterop::commandBuffer(device, *commands);
    if (!nativeCommands.has_value() || *nativeCommands == VK_NULL_HANDLE || !commands->end() ||
        VkRhiInterop::commandBuffer(device, *commands).has_value()) {
        return false;
    }
    return true;
}

[[nodiscard]] bool validateTemporalOutputTarget(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    TemporalUpscalePass pass;
    pass.init(device, commandPool);
    UpscaleSettings settings;
    settings.type = TemporalUpscalerType::Fsr31;
    settings.quality = TemporalUpscaleQuality::Quality;
    constexpr TemporalExtent kInitialExtent{640u, 360u};
    if (!pass.prepareOutputTarget(settings, kInitialExtent, kInitialExtent)) {
        return false;
    }
    const RhiTextureHandle initialTexture = pass.outputTextureHandle();
    const RhiTextureViewHandle initialView = pass.outputTextureViewHandle();
    const auto initialInfo = VkRhiInterop::textureInfo(device, initialTexture, initialView);
    if (!initialInfo.has_value() || initialInfo->format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        initialInfo->extent.width != kInitialExtent.width || initialInfo->extent.height != kInitialExtent.height ||
        (initialInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0u ||
        (initialInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0u ||
        !pass.prepareOutputTarget(settings, kInitialExtent, kInitialExtent) ||
        pass.outputTextureHandle().index != initialTexture.index ||
        pass.outputTextureHandle().generation != initialTexture.generation) {
        return false;
    }

    constexpr TemporalExtent kResizedExtent{960u, 540u};
    if (!pass.prepareOutputTarget(settings, kResizedExtent, kResizedExtent)) {
        return false;
    }
    const RhiTextureHandle resizedTexture = pass.outputTextureHandle();
    const bool targetRecreated =
        resizedTexture.index != initialTexture.index || resizedTexture.generation != initialTexture.generation;
    const auto resizedInfo = VkRhiInterop::textureInfo(device, resizedTexture, pass.outputTextureViewHandle());
    settings.type = TemporalUpscalerType::Native;
    if (!targetRecreated || !resizedInfo.has_value() || resizedInfo->extent.width != kResizedExtent.width ||
        resizedInfo->extent.height != kResizedExtent.height ||
        !pass.prepareOutputTarget(settings, kResizedExtent, kResizedExtent) || pass.outputTextureHandle().isValid() ||
        pass.outputTextureViewHandle().isValid()) {
        return false;
    }
    pass.shutdown();
    return true;
}

#if defined(MECRAFT_ENABLE_FSR31)
[[nodiscard]] bool validateFsr31VulkanContext(VkRhiDevice& device) {
    Fsr31VulkanContext context;
    const Fsr31VulkanContextCreateResult invalid = context.initialize(device, {{}, {1920u, 1080u}, false, false});
    if (invalid.status != Fsr31VulkanContextCreateStatus::InvalidRenderExtent) {
        return false;
    }

    constexpr TemporalExtent kRenderExtent{1280u, 720u};
    constexpr TemporalExtent kOutputExtent{1920u, 1080u};
    const Fsr31VulkanContextCreateResult created =
        context.initialize(device, {kRenderExtent, kOutputExtent, false, false});
    if (!created.succeeded() || !context.isInitialized() || context.maxRenderExtent() != kRenderExtent ||
        context.maxOutputExtent() != kOutputExtent || context.scratchMemorySize() == 0u ||
        context.initialize(device, {kRenderExtent, kOutputExtent, false, false}).status !=
            Fsr31VulkanContextCreateStatus::AlreadyInitialized) {
        std::cerr << "FSR 3.1 Vulkan context creation failed: status " << static_cast<uint32_t>(created.status)
                  << ", SDK error " << created.sdkError << '\n';
        return false;
    }
    const Fsr31VulkanContextDestroyResult destroyed = context.shutdown();
    if (!destroyed.succeeded() || context.isInitialized() || context.scratchMemorySize() != 0u ||
        context.shutdown().status != Fsr31VulkanContextDestroyStatus::NotInitialized) {
        return false;
    }
    for (uint32_t iteration = 0u; iteration < 3u; ++iteration) {
        const Fsr31VulkanContextCreateResult recreated =
            context.initialize(device, {kRenderExtent, kOutputExtent, false, iteration == 1u});
        if (!recreated.succeeded() || !context.shutdown().succeeded()) {
            return false;
        }
    }
    return !context.isInitialized() && context.scratchMemorySize() == 0u;
}

struct Fsr31SmokeTexture {
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
};

[[nodiscard]] bool createFsr31SmokeTexture(VkRhiDevice& device, const char* const debugName,
                                           const RhiTextureFormat format, const TemporalExtent extent,
                                           const RhiTextureUsageFlags usage, const void* const pixels,
                                           const size_t sizeBytes, const RhiResourceState finalState,
                                           Fsr31SmokeTexture& output) {
    RhiTextureDesc textureDesc;
    textureDesc.debugName = debugName;
    textureDesc.format = format;
    textureDesc.width = extent.width;
    textureDesc.height = extent.height;
    textureDesc.usage = usage;
    RhiTextureInitialData initialData;
    initialData.pixels = pixels;
    initialData.sizeBytes = sizeBytes;
    initialData.finalState = finalState;
    output.texture = device.createTexture(textureDesc, &initialData);
    if (!output.texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = output.texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = format;
    output.view = device.createTextureView(viewDesc);
    if (!output.view.isValid()) {
        device.destroyTexture(output.texture);
        output.texture = {};
        return false;
    }
    return true;
}

void destroyFsr31SmokeTexture(VkRhiDevice& device, Fsr31SmokeTexture& resource) {
    if (resource.view.isValid()) {
        device.destroyTextureView(resource.view);
    }
    if (resource.texture.isValid()) {
        device.destroyTexture(resource.texture);
    }
    resource = {};
}

[[nodiscard]] bool validateFsr31VulkanDispatch(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr TemporalExtent kRenderExtent{320u, 180u};
    constexpr TemporalExtent kOutputExtent{480u, 270u};
    const size_t renderPixelCount = static_cast<size_t>(kRenderExtent.width) * kRenderExtent.height;
    std::vector<uint16_t> hdrPixels(renderPixelCount * 4u, 0u);
    std::vector<float> depthPixels(renderPixelCount, 1.0f);
    std::vector<uint16_t> velocityPixels(renderPixelCount * 2u, 0u);
    const float exposurePixels[1] = {1.0f};
    std::vector<uint8_t> maskPixels(renderPixelCount, 0u);

    Fsr31SmokeTexture hdr;
    Fsr31SmokeTexture depth;
    Fsr31SmokeTexture velocity;
    Fsr31SmokeTexture exposure;
    Fsr31SmokeTexture reactive;
    Fsr31SmokeTexture transparency;
    const auto destroyInputs = [&]() {
        destroyFsr31SmokeTexture(device, transparency);
        destroyFsr31SmokeTexture(device, reactive);
        destroyFsr31SmokeTexture(device, exposure);
        destroyFsr31SmokeTexture(device, velocity);
        destroyFsr31SmokeTexture(device, depth);
        destroyFsr31SmokeTexture(device, hdr);
    };

    const RhiTextureUsageFlags sampledUsage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const bool inputsCreated =
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Hdr", RhiTextureFormat::Rgba16Float, kRenderExtent,
                                sampledUsage, hdrPixels.data(), hdrPixels.size() * sizeof(uint16_t),
                                RhiResourceState::ShaderRead, hdr) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Depth", RhiTextureFormat::Depth32Float, kRenderExtent,
                                sampledUsage | rhiFlag(RhiTextureUsage::DepthStencilAttachment), depthPixels.data(),
                                depthPixels.size() * sizeof(float), RhiResourceState::DepthRead, depth) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Velocity", RhiTextureFormat::Rg16Float, kRenderExtent,
                                sampledUsage, velocityPixels.data(), velocityPixels.size() * sizeof(uint16_t),
                                RhiResourceState::ShaderRead, velocity) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Exposure", RhiTextureFormat::R32Float, {1u, 1u},
                                sampledUsage, exposurePixels, sizeof(exposurePixels), RhiResourceState::ShaderRead,
                                exposure) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Reactive", RhiTextureFormat::R8Unorm, kRenderExtent,
                                sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                                reactive) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Transparency", RhiTextureFormat::R8Unorm, kRenderExtent,
                                sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                                transparency);
    if (!inputsCreated) {
        std::cerr << "FSR 3.1 smoke test failed to create input resources\n";
        destroyInputs();
        return false;
    }

    TemporalUpscalePass pass;
    pass.init(device, commandPool);
    UpscaleSettings settings;
    settings.type = TemporalUpscalerType::Fsr31;
    settings.quality = TemporalUpscaleQuality::Quality;
    settings.debugVisualizationEnabled = true;
    if (!pass.prepareOutputTarget(settings, kRenderExtent, kOutputExtent)) {
        std::cerr << "FSR 3.1 smoke test failed to prepare the output target: texture "
                  << pass.outputTextureHandle().index << ", view " << pass.outputTextureViewHandle().index << '\n';
        pass.shutdown();
        destroyInputs();
        return false;
    }

    TemporalFrameInput frame;
    frame.extents = makeTemporalFrameExtents(kRenderExtent, kRenderExtent, kRenderExtent, kOutputExtent);
    frame.motionVectorScale =
        glm::vec2(static_cast<float>(kRenderExtent.width), static_cast<float>(kRenderExtent.height));
    frame.frameDeltaMilliseconds = 1000.0f / 60.0f;
    frame.preExposure = 1.0f;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 1000.0f;
    frame.verticalFovRadians = 1.0f;
    frame.resetReasons = temporalResetReasonBit(TemporalResetReason::FirstFrame);
    frame.textures.hdrColor = hdr.texture;
    frame.textures.hdrColorView = hdr.view;
    frame.textures.depth = depth.texture;
    frame.textures.depthView = depth.view;
    frame.textures.velocity = velocity.texture;
    frame.textures.velocityView = velocity.view;
    frame.textures.exposure = exposure.texture;
    frame.textures.exposureView = exposure.view;
    frame.textures.reactiveMask = reactive.texture;
    frame.textures.reactiveMaskView = reactive.view;
    frame.textures.transparencyMask = transparency.texture;
    frame.textures.transparencyMaskView = transparency.view;
    frame.textures.outputHdrColor = pass.outputTextureHandle();
    frame.textures.outputHdrColorView = pass.outputTextureViewHandle();

    const TemporalUpscaleResult firstResult = pass.execute(settings, frame);
    TemporalUpscaleResult secondResult;
    if (firstResult.succeeded()) {
        frame.resetReasons = temporalResetReasonBit(TemporalResetReason::None);
        secondResult = pass.execute(settings, frame);
    }
    device.waitIdle();
    const bool dispatched = firstResult.succeeded() && secondResult.succeeded() &&
                            secondResult.outputHdrColor.isValid() && secondResult.outputHdrColorView.isValid() &&
                            secondResult.outputExtent == kOutputExtent;
    if (!dispatched) {
        std::cerr << "FSR 3.1 smoke dispatch failed: "
                  << TemporalUpscalePass::statusText(firstResult.succeeded() ? secondResult.status : firstResult.status)
                  << ", SDK error " << (firstResult.succeeded() ? secondResult.sdkError : firstResult.sdkError) << '\n';
    }
    pass.shutdown();
    destroyInputs();
    return dispatched;
}
#endif

#if defined(MECRAFT_ENABLE_NRD)
struct NrdSmokeTexture final {
    RhiTextureDesc desc;
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
};

[[nodiscard]] bool createNrdSmokeTexture(VkRhiDevice& device, const char* const debugName,
                                         const RhiTextureFormat format, const uint32_t width, const uint32_t height,
                                         const RhiTextureUsageFlags usage, const void* const pixels,
                                         const size_t sizeBytes, const RhiResourceState finalState,
                                         NrdSmokeTexture& output) {
    output.desc.debugName = debugName;
    output.desc.format = format;
    output.desc.width = width;
    output.desc.height = height;
    output.desc.usage = usage;
    output.desc.memoryCategory = RhiMemoryCategory::Nrd;
    RhiTextureInitialData initialData;
    initialData.pixels = pixels;
    initialData.sizeBytes = sizeBytes;
    initialData.finalState = finalState;
    output.texture = device.createTexture(output.desc, pixels != nullptr ? &initialData : nullptr);
    if (!output.texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = output.texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = format;
    output.view = device.createTextureView(viewDesc);
    if (!output.view.isValid()) {
        device.destroyTexture(output.texture);
        output.texture = {};
        return false;
    }
    return true;
}

void destroyNrdSmokeTexture(VkRhiDevice& device, NrdSmokeTexture& resource) {
    if (resource.view.isValid()) {
        device.destroyTextureView(resource.view);
    }
    if (resource.texture.isValid()) {
        device.destroyTexture(resource.texture);
    }
    resource = {};
}

void setNrdIdentityMatrix(float (&matrix)[16]) {
    std::fill(std::begin(matrix), std::end(matrix), 0.0f);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

[[nodiscard]] bool validateNrdGuidePrepPass(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr uint32_t kWidth = 2u;
    constexpr uint32_t kHeight = 1u;
    constexpr size_t kPixelCount = static_cast<size_t>(kWidth) * kHeight;
    constexpr size_t kMotionBytes = kPixelCount * sizeof(uint16_t) * 4u;
    constexpr size_t kViewZBytes = kPixelCount * sizeof(float);
    constexpr size_t kCoverageBytes = kPixelCount * sizeof(uint8_t);
    constexpr uint64_t kViewZOffset = kMotionBytes;
    constexpr uint64_t kCoverageOffset = kViewZOffset + kViewZBytes;
    constexpr uint64_t kReadbackBytes = kCoverageOffset + kCoverageBytes;
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    std::vector<float> depth(kPixelCount, 0.25f);
    const uint32_t packedNormalAo = glm::packUnorm3x10_1x2(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
    std::vector<uint32_t> normalAo(kPixelCount, packedNormalAo);
    std::vector<uint32_t> material(kPixelCount, 0x000000ffu);
    std::vector<uint16_t> velocity(kPixelCount * 2u, 0u);
    for (size_t pixel = 0u; pixel < kPixelCount; ++pixel) {
        velocity[pixel * 2u + 0u] = glm::packHalf1x16(0.125f);
        velocity[pixel * 2u + 1u] = glm::packHalf1x16(-0.125f);
    }

    constexpr RhiTextureUsageFlags kSampledUsage = rhiFlag(RhiTextureUsage::Sampled);
    constexpr RhiTextureUsageFlags kDepthUsage =
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    constexpr RhiTextureUsageFlags kGuideOutputUsage =
        rhiFlag(RhiTextureUsage::Storage) | rhiFlag(RhiTextureUsage::TransferSrc);
    NrdSmokeTexture depthTexture;
    NrdSmokeTexture normalAoTexture;
    NrdSmokeTexture materialTexture;
    NrdSmokeTexture velocityTexture;
    NrdSmokeTexture motionTexture;
    NrdSmokeTexture normalRoughnessTexture;
    NrdSmokeTexture viewZTexture;
    NrdSmokeTexture reprojectionCoverageTexture;
    RhiBufferHandle readback;
    RenderGraph graph;
    NrdGuidePrepPass pass;
    const auto cleanup = [&]() {
        device.waitIdle();
        pass.shutdown();
        graph.releaseTransientResources(device);
        if (readback.isValid()) {
            device.destroyBuffer(readback);
        }
        destroyNrdSmokeTexture(device, reprojectionCoverageTexture);
        destroyNrdSmokeTexture(device, viewZTexture);
        destroyNrdSmokeTexture(device, normalRoughnessTexture);
        destroyNrdSmokeTexture(device, motionTexture);
        destroyNrdSmokeTexture(device, velocityTexture);
        destroyNrdSmokeTexture(device, materialTexture);
        destroyNrdSmokeTexture(device, normalAoTexture);
        destroyNrdSmokeTexture(device, depthTexture);
    };

    const auto requireTexture = [](const bool created, const char* const name) {
        if (!created) {
            std::cerr << "NRD Guide Prep smoke test failed to create " << name << '\n';
        }
        return created;
    };
    bool valid =
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.Depth", RhiTextureFormat::Depth32Float,
                                             kWidth, kHeight, kDepthUsage, depth.data(), depth.size() * sizeof(float),
                                             RhiResourceState::DepthRead, depthTexture),
                       "current depth") &&
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.NormalAo", RhiTextureFormat::Rgb10A2Unorm,
                                             kWidth, kHeight, kSampledUsage, normalAo.data(),
                                             normalAo.size() * sizeof(uint32_t), RhiResourceState::ShaderRead,
                                             normalAoTexture),
                       "normal/AO") &&
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.Material", RhiTextureFormat::Rgba8Unorm,
                                             kWidth, kHeight, kSampledUsage, material.data(),
                                             material.size() * sizeof(uint32_t), RhiResourceState::ShaderRead,
                                             materialTexture),
                       "material") &&
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.Velocity", RhiTextureFormat::Rg16Float,
                                             kWidth, kHeight, kSampledUsage, velocity.data(),
                                             velocity.size() * sizeof(uint16_t), RhiResourceState::ShaderRead,
                                             velocityTexture),
                       "velocity") &&
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.Motion", RhiTextureFormat::Rgba16Float,
                                             kWidth, kHeight, kGuideOutputUsage, nullptr, 0u,
                                             RhiResourceState::Undefined, motionTexture),
                       "motion output") &&
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.NormalRoughness",
                                             RhiTextureFormat::Rgb10A2Unorm, kWidth, kHeight,
                                             rhiFlag(RhiTextureUsage::Storage), nullptr, 0u,
                                             RhiResourceState::Undefined, normalRoughnessTexture),
                       "normal/roughness output") &&
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.ViewZ", RhiTextureFormat::R32Float, kWidth,
                                             kHeight, kGuideOutputUsage, nullptr, 0u, RhiResourceState::Undefined,
                                             viewZTexture),
                       "View-Z output") &&
        requireTexture(createNrdSmokeTexture(device, "VulkanSmoke.NRD.Guide.ReprojectionCoverage",
                                             RhiTextureFormat::R8Unorm, kWidth, kHeight, kGuideOutputUsage, nullptr, 0u,
                                             RhiResourceState::Undefined, reprojectionCoverageTexture),
                       "reprojection-coverage output");
    if (!valid) {
        std::cerr << "NRD Guide Prep smoke test failed to create textures\n";
        cleanup();
        return false;
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.NRD.Guide.Readback";
    readbackDesc.size = kReadbackBytes;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    readback = device.createBuffer(readbackDesc, nullptr, 0u);
    valid = readback.isValid();

    SharedRenderResources shared;
    shared.rhiDevice = &device;
    shared.commandListPool = &commandPool;
    FrameContext frame;
    frame.shared = &shared;
    frame.temporalExtents =
        makeTemporalFrameExtents({kWidth, kHeight}, {kWidth, kHeight}, {kWidth, kHeight}, {kWidth, kHeight});
    frame.camera.view = glm::mat4(1.0f);
    frame.camera.projection = glm::mat4(1.0f);
    frame.camera.projection[2][2] = 0.25f;
    frame.camera.invViewProj = glm::inverse(frame.camera.projection);
    frame.camera.jitteredInvViewProj = frame.camera.invViewProj;
    frame.prevCamera = frame.camera;
    frame.prevCamera.position.z = 1.0f;
    frame.prevCamera.view[3][2] = -1.0f;

    NrdGuidePrepPass::GraphResources resources;
    RgBufferHandle readbackResource;
    RgPassHandle guideHandle;
    if (valid) {
        const auto importTexture = [&](const NrdSmokeTexture& texture, const RhiResourceState initialState,
                                       const RhiResourceState finalState) {
            return graph.importTexture({texture.desc.debugName, texture.texture, texture.desc, initialState, finalState,
                                        texture.view, RhiQueueType::Graphics, RhiQueueType::Graphics});
        };
        resources.depth = importTexture(depthTexture, RhiResourceState::DepthRead, RhiResourceState::DepthRead);
        resources.normalAo = importTexture(normalAoTexture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.material = importTexture(materialTexture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.velocity = importTexture(velocityTexture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.motion = importTexture(motionTexture, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        resources.normalRoughness =
            importTexture(normalRoughnessTexture, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        resources.viewZ = importTexture(viewZTexture, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        resources.reprojectionCoverage =
            importTexture(reprojectionCoverageTexture, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        readbackResource =
            graph.importBuffer({readbackDesc.debugName, readback, readbackDesc, RhiResourceState::TransferDst,
                                RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});
        const RgPassHandle inputReady = graph.addPass({"NRD.GuideInputReady", RgPassType::Copy, RhiQueueType::Graphics})
                                            .setExecute([](RgPassContext&) { return true; })
                                            .handle();
        NrdGuidePrepPass::Settings settings;
        settings.denoisingRange = 10.0f;
        settings.historyValid = true;
        guideHandle = pass.addGraphPass(graph, frame, settings, resources, inputReady);
        valid = resources.depth.isValid() && resources.normalAo.isValid() && resources.material.isValid() &&
                resources.velocity.isValid() && resources.motion.isValid() && resources.normalRoughness.isValid() &&
                resources.viewZ.isValid() && resources.reprojectionCoverage.isValid() && readbackResource.isValid() &&
                guideHandle.isValid();
    }
    if (valid) {
        graph.addPass({"NRD.GuideReadback", RgPassType::Copy, RhiQueueType::Graphics})
            .dependsOn(guideHandle)
            .readTexture(resources.motion, RhiResourceState::TransferSrc)
            .readTexture(resources.viewZ, RhiResourceState::TransferSrc)
            .readTexture(resources.reprojectionCoverage, RhiResourceState::TransferSrc)
            .writeBuffer(readbackResource, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                RhiTextureBufferCopy motionCopy;
                motionCopy.srcTexture = context.texture(resources.motion);
                motionCopy.dstBuffer = context.buffer(readbackResource);
                motionCopy.bytesPerRow = sizeof(uint16_t) * 4u * kWidth;
                motionCopy.rowsPerImage = kHeight;
                motionCopy.width = kWidth;
                motionCopy.height = kHeight;
                context.commandList().copyTextureToBuffer(motionCopy);
                RhiTextureBufferCopy viewZCopy;
                viewZCopy.srcTexture = context.texture(resources.viewZ);
                viewZCopy.dstBuffer = context.buffer(readbackResource);
                viewZCopy.bufferOffset = kViewZOffset;
                viewZCopy.bytesPerRow = sizeof(float) * kWidth;
                viewZCopy.rowsPerImage = kHeight;
                viewZCopy.width = kWidth;
                viewZCopy.height = kHeight;
                context.commandList().copyTextureToBuffer(viewZCopy);
                RhiTextureBufferCopy coverageCopy;
                coverageCopy.srcTexture = context.texture(resources.reprojectionCoverage);
                coverageCopy.dstBuffer = context.buffer(readbackResource);
                coverageCopy.bufferOffset = kCoverageOffset;
                coverageCopy.bytesPerRow = sizeof(uint8_t) * kWidth;
                coverageCopy.rowsPerImage = kHeight;
                coverageCopy.width = kWidth;
                coverageCopy.height = kHeight;
                context.commandList().copyTextureToBuffer(coverageCopy);
                return true;
            });
        const RgCompileResult compiled = graph.compile();
        valid = compiled.succeeded();
        if (!valid) {
            std::cerr << "NRD Guide Prep Render Graph compile failed: " << compiled.message << '\n';
        }
    }
    if (valid) {
        const RgExecuteResult executed = graph.execute(device, commandPool);
        valid = executed.succeeded() && executed.completionToken().isValid() &&
                device.waitForSubmission(executed.completionToken());
        if (!executed.succeeded()) {
            std::cerr << "NRD Guide Prep Render Graph execution failed: " << executed.message << '\n';
        }
    }
    if (valid) {
        const auto* bytes = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, readbackDesc.size));
        valid = bytes != nullptr;
        if (bytes != nullptr) {
            const auto* motion = reinterpret_cast<const uint16_t*>(bytes);
            const auto* viewZ = reinterpret_cast<const float*>(bytes + kViewZOffset);
            const auto* coverage = bytes + kCoverageOffset;
            for (size_t pixel = 0u; pixel < kPixelCount; ++pixel) {
                const float motionX = glm::unpackHalf1x16(motion[pixel * 4u + 0u]);
                const float motionY = glm::unpackHalf1x16(motion[pixel * 4u + 1u]);
                const float motionZ = glm::unpackHalf1x16(motion[pixel * 4u + 2u]);
                const float motionW = glm::unpackHalf1x16(motion[pixel * 4u + 3u]);
                // The guide contract publishes 2.5D motion: current positive
                // View-Z is 2, previous positive View-Z is 3, and NRD receives
                // the previous-minus-current delta after its handedness conversion.
                const bool pixelValid = std::abs(motionX + 0.125f) < 0.001f && std::abs(motionY - 0.125f) < 0.001f &&
                                        std::abs(motionZ - 1.0f) < 0.001f && std::abs(motionW) < 0.001f &&
                                        std::abs(viewZ[pixel] + 2.0f) < 0.001f && coverage[pixel] == 255u;
                if (!pixelValid) {
                    std::cerr << "NRD Guide Prep pixel " << pixel << " mismatch: motion=(" << motionX << ", " << motionY
                              << ", " << motionZ << ", " << motionW << "), View-Z=" << viewZ[pixel]
                              << ", reprojection coverage=" << static_cast<uint32_t>(coverage[pixel]) << '\n';
                }
                valid = valid && pixelValid;
            }
            device.unmapBuffer(readback);
        }
        valid = valid && pass.stats().dispatched && pass.stats().width == kWidth && pass.stats().height == kHeight &&
                std::abs(pass.stats().denoisingRange - 10.0f) < 0.001f;
    }

    cleanup();
    valid = valid && device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << "NRD Guide Prep Vulkan readback validation failed\n";
    }
    return valid;
}

[[nodiscard]] bool validateNrdRenderGraphDispatch(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    using renderer::nrd::NrdBridgeError;
    using renderer::nrd::NrdDiffuseMethod;
    using renderer::nrd::NrdExternalResources;
    using renderer::nrd::NrdGraphDispatchResult;
    using renderer::nrd::NrdRenderGraphBridge;

    constexpr uint32_t kWidth = 16u;
    constexpr uint32_t kHeight = 16u;
    constexpr size_t kPixelCount = static_cast<size_t>(kWidth) * kHeight;
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    std::vector<uint16_t> motion(kPixelCount * 4u, 0u);
    const uint32_t packedNormalRoughness = glm::packUnorm3x10_1x2(glm::vec4(0.5f, 0.5f, 1.0f, 0.0f));
    std::vector<uint32_t> normalRoughness(kPixelCount, packedNormalRoughness);
    std::vector<float> viewZ(kPixelCount, 1.0f);
    std::vector<uint16_t> rawSignal(kPixelCount * 4u, 0u);
    for (size_t pixel = 0u; pixel < kPixelCount; ++pixel) {
        const size_t x = pixel % kWidth;
        if (x >= kWidth / 2u) {
            viewZ[pixel] = 2000.0f;
        }
        rawSignal[pixel * 4u + 0u] = glm::packHalf1x16(0.25f);
        rawSignal[pixel * 4u + 1u] = glm::packHalf1x16(0.5f);
        rawSignal[pixel * 4u + 2u] = glm::packHalf1x16(0.75f);
        rawSignal[pixel * 4u + 3u] = glm::packHalf1x16(0.5f);
    }
    std::vector<uint16_t> outputSignal(kPixelCount * 4u, 0x7e00u);

    constexpr RhiTextureUsageFlags kInputUsage = rhiFlag(RhiTextureUsage::Sampled);
    constexpr RhiTextureUsageFlags kOutputUsage =
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::Storage) | rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::ColorAttachment);
    NrdSmokeTexture motionTexture;
    NrdSmokeTexture normalRoughnessTexture;
    NrdSmokeTexture viewZTexture;
    NrdSmokeTexture rawSignalTexture;
    NrdSmokeTexture outputTexture;
    RhiBufferHandle readback;
    RenderGraph graph;
    NrdRenderGraphBridge bridge;
    const auto cleanup = [&]() {
        device.waitIdle();
        bridge.shutdown();
        graph.releaseTransientResources(device);
        if (readback.isValid()) {
            device.destroyBuffer(readback);
        }
        destroyNrdSmokeTexture(device, outputTexture);
        destroyNrdSmokeTexture(device, rawSignalTexture);
        destroyNrdSmokeTexture(device, viewZTexture);
        destroyNrdSmokeTexture(device, normalRoughnessTexture);
        destroyNrdSmokeTexture(device, motionTexture);
    };

    bool valid =
        createNrdSmokeTexture(device, "VulkanSmoke.NRD.Motion", RhiTextureFormat::Rgba16Float, kWidth, kHeight,
                              kInputUsage, motion.data(), motion.size() * sizeof(uint16_t),
                              RhiResourceState::ShaderRead, motionTexture) &&
        createNrdSmokeTexture(device, "VulkanSmoke.NRD.NormalRoughness", RhiTextureFormat::Rgb10A2Unorm, kWidth,
                              kHeight, kInputUsage, normalRoughness.data(), normalRoughness.size() * sizeof(uint32_t),
                              RhiResourceState::ShaderRead, normalRoughnessTexture) &&
        createNrdSmokeTexture(device, "VulkanSmoke.NRD.ViewZ", RhiTextureFormat::R32Float, kWidth, kHeight, kInputUsage,
                              viewZ.data(), viewZ.size() * sizeof(float), RhiResourceState::ShaderRead, viewZTexture) &&
        createNrdSmokeTexture(device, "VulkanSmoke.NRD.RawDiffuse", RhiTextureFormat::Rgba16Float, kWidth, kHeight,
                              kInputUsage, rawSignal.data(), rawSignal.size() * sizeof(uint16_t),
                              RhiResourceState::ShaderRead, rawSignalTexture) &&
        createNrdSmokeTexture(device, "VulkanSmoke.NRD.OutputDiffuse", RhiTextureFormat::Rgba16Float, kWidth, kHeight,
                              kOutputUsage, outputSignal.data(), outputSignal.size() * sizeof(uint16_t),
                              RhiResourceState::ShaderWrite, outputTexture);
    if (!valid) {
        std::cerr << "NRD smoke test failed to create external textures\n";
        cleanup();
        return false;
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.NRD.Readback";
    readbackDesc.size = outputSignal.size() * sizeof(uint16_t);
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    readback = device.createBuffer(readbackDesc, nullptr, 0u);
    valid = readback.isValid();

    ::nrd::CommonSettings commonSettings{};
    setNrdIdentityMatrix(commonSettings.viewToClipMatrix);
    setNrdIdentityMatrix(commonSettings.viewToClipMatrixPrev);
    setNrdIdentityMatrix(commonSettings.worldToViewMatrix);
    setNrdIdentityMatrix(commonSettings.worldToViewMatrixPrev);
    commonSettings.resourceSize[0] = kWidth;
    commonSettings.resourceSize[1] = kHeight;
    commonSettings.resourceSizePrev[0] = kWidth;
    commonSettings.resourceSizePrev[1] = kHeight;
    commonSettings.rectSize[0] = kWidth;
    commonSettings.rectSize[1] = kHeight;
    commonSettings.rectSizePrev[0] = kWidth;
    commonSettings.rectSizePrev[1] = kHeight;
    commonSettings.motionVectorScale[0] = 1.0f;
    commonSettings.motionVectorScale[1] = 1.0f;
    commonSettings.motionVectorScale[2] = 1.0f;
    commonSettings.timeDeltaBetweenFrames = 1000.0f / 60.0f;
    commonSettings.denoisingRange = 1000.0f;
    const ::nrd::RelaxSettings relaxSettings{};

    const auto initializeBridge = [&]() {
        const NrdBridgeError initialized = bridge.initialize(device, NrdDiffuseMethod::Relax, kWidth, kHeight);
        const std::optional<NrdDiffuseMethod> initializedMethod = bridge.method();
        const bool initializedContract =
            initialized == NrdBridgeError::None && bridge.initialized() && initializedMethod.has_value() &&
            *initializedMethod == NrdDiffuseMethod::Relax && bridge.pipelineCount() == 15u &&
            bridge.permanentPoolSize() == 6u && bridge.transientPoolSize() == 4u;
        if (!initializedContract) {
            const std::optional<std::string_view> error = renderer::nrd::nrdBridgeErrorStableId(initialized);
            if (error.has_value()) {
                std::cerr << "NRD bridge initialization failed: " << *error << '\n';
            }
        }
        return initializedContract;
    };

    valid = valid && initializeBridge();
    if (valid) {
        RenderGraph rejectedGraph;
        commonSettings.frameIndex = 0u;
        commonSettings.accumulationMode = ::nrd::AccumulationMode::CLEAR_AND_RESTART;
        const NrdExternalResources missingExternalResources;
        const NrdGraphDispatchResult missingResourceResult =
            bridge.addGraphDispatches(rejectedGraph, commonSettings, relaxSettings, missingExternalResources);
        valid = missingResourceResult.error == NrdBridgeError::MissingExternalResource &&
                !missingResourceResult.succeeded() && !bridge.framePending() &&
                bridge.lastError() == NrdBridgeError::MissingExternalResource;
        if (valid) {
            const NrdGraphDispatchResult invalidStateResult =
                bridge.addGraphDispatches(rejectedGraph, commonSettings, relaxSettings, missingExternalResources);
            valid = invalidStateResult.error == NrdBridgeError::ExecutionStateInvalid &&
                    !invalidStateResult.succeeded() && !bridge.framePending() &&
                    bridge.lastError() == NrdBridgeError::ExecutionStateInvalid;
        }
    }
    bridge.shutdown();
    valid = valid && initializeBridge();

    const auto executeFrame = [&](const uint32_t frameIndex, const ::nrd::AccumulationMode accumulationMode,
                                  const uint32_t expectedDispatchCount, const RhiResourceState readbackInitialState) {
        graph.reset();
        const auto importTexture = [&](const NrdSmokeTexture& texture, const RhiResourceState initialState,
                                       const RhiResourceState finalState) {
            return graph.importTexture({texture.desc.debugName, texture.texture, texture.desc, initialState, finalState,
                                        texture.view, RhiQueueType::Graphics, RhiQueueType::Graphics});
        };
        const RgTextureHandle motionResource =
            importTexture(motionTexture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        const RgTextureHandle normalRoughnessResource =
            importTexture(normalRoughnessTexture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        const RgTextureHandle viewZResource =
            importTexture(viewZTexture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        const RgTextureHandle rawSignalResource =
            importTexture(rawSignalTexture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        const RgTextureHandle outputResource =
            importTexture(outputTexture, RhiResourceState::ShaderWrite, RhiResourceState::ShaderWrite);
        const RgBufferHandle readbackResource =
            graph.importBuffer({readbackDesc.debugName, readback, readbackDesc, readbackInitialState,
                                RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});
        NrdExternalResources externalResources;
        const bool resourcesValid =
            motionResource.isValid() && normalRoughnessResource.isValid() && viewZResource.isValid() &&
            rawSignalResource.isValid() && outputResource.isValid() && readbackResource.isValid() &&
            externalResources.bind(::nrd::ResourceType::IN_MV, motionResource) &&
            externalResources.bind(::nrd::ResourceType::IN_NORMAL_ROUGHNESS, normalRoughnessResource) &&
            externalResources.bind(::nrd::ResourceType::IN_VIEWZ, viewZResource) &&
            externalResources.bind(::nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, rawSignalResource) &&
            externalResources.bind(::nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, outputResource);
        if (!resourcesValid) {
            return false;
        }

        RenderGraphPassBuilder outputInit =
            graph.addPass({"NRD.OutputInit", RgPassType::Graphics, RhiQueueType::Graphics});
        if (!outputInit.handle().isValid()) {
            return false;
        }
        outputInit.writeTexture(outputResource, RhiResourceState::RenderTarget)
            .setExecute([outputResource](RgPassContext& context) {
                RhiColorAttachment attachment;
                attachment.view = context.textureView(outputResource);
                attachment.loadOp = RhiLoadOp::Clear;
                attachment.storeOp = RhiStoreOp::Store;
                attachment.clearColor[0] = 0.0f;
                attachment.clearColor[1] = 0.0f;
                attachment.clearColor[2] = 0.0f;
                attachment.clearColor[3] = 0.0f;

                RhiRenderingInfo renderingInfo;
                renderingInfo.debugName = "NRD.OutputInit";
                renderingInfo.renderArea = {0u, 0u, kWidth, kHeight};
                renderingInfo.colorAttachments = &attachment;
                renderingInfo.colorAttachmentCount = 1u;
                context.commandList().beginRendering(renderingInfo);
                context.commandList().endRendering();
                return true;
            });

        commonSettings.frameIndex = frameIndex;
        commonSettings.accumulationMode = accumulationMode;
        const NrdGraphDispatchResult dispatchResult =
            bridge.addGraphDispatches(graph, commonSettings, relaxSettings, externalResources, outputInit.handle());
        if (!dispatchResult.succeeded() || dispatchResult.dispatchCount != expectedDispatchCount ||
            !bridge.framePending()) {
            return false;
        }

        RenderGraphPassBuilder readbackPass =
            graph.addPass({"NRD.OutputReadback", RgPassType::Copy, RhiQueueType::Graphics});
        if (!readbackPass.handle().isValid()) {
            RgExecuteResult failedExecution;
            failedExecution.error = RgExecuteError::NotCompiled;
            bridge.completeGraphExecution(failedExecution);
            return false;
        }
        readbackPass.dependsOn(dispatchResult.lastPass)
            .readTexture(outputResource, RhiResourceState::TransferSrc)
            .writeBuffer(readbackResource, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                RhiTextureBufferCopy copy;
                copy.srcTexture = context.texture(outputResource);
                copy.dstBuffer = context.buffer(readbackResource);
                copy.bytesPerRow = sizeof(uint16_t) * 4u * kWidth;
                copy.rowsPerImage = kHeight;
                copy.width = kWidth;
                copy.height = kHeight;
                context.commandList().copyTextureToBuffer(copy);
                return true;
            });

        const RgCompileResult compiled = graph.compile();
        if (!compiled.succeeded()) {
            RgExecuteResult failedExecution;
            failedExecution.error = RgExecuteError::NotCompiled;
            failedExecution.message = compiled.message;
            bridge.completeGraphExecution(failedExecution);
            std::cerr << "NRD Render Graph compile failed: " << compiled.message << '\n';
            return false;
        }

        const RgExecuteResult executed = graph.execute(device, commandPool);
        bridge.completeGraphExecution(executed);
        if (!executed.succeeded()) {
            std::cerr << "NRD Render Graph execution failed: " << executed.message << '\n';
            return false;
        }
        return executed.completionToken().isValid() && device.waitForSubmission(executed.completionToken()) &&
               !bridge.framePending() && bridge.lastError() == NrdBridgeError::None;
    };

    if (valid) {
        valid = executeFrame(0u, ::nrd::AccumulationMode::CLEAR_AND_RESTART, 21u, RhiResourceState::TransferDst);
    }
    if (valid) {
        valid = executeFrame(1u, ::nrd::AccumulationMode::CONTINUE, 10u, RhiResourceState::HostRead);
    }
    if (valid) {
        const auto* pixels = static_cast<const uint16_t*>(device.mapBuffer(readback, 0u, readbackDesc.size));
        valid = pixels != nullptr;
        bool nonZeroRadiance = false;
        if (pixels != nullptr) {
            for (size_t pixel = 0u; pixel < kPixelCount; ++pixel) {
                const size_t x = pixel % kWidth;
                const bool skyPixel = x >= kWidth / 2u;
                for (uint32_t component = 0u; component < 4u; ++component) {
                    const float value = glm::unpackHalf1x16(pixels[pixel * 4u + component]);
                    valid = valid && std::isfinite(value);
                    if (skyPixel) {
                        valid = valid && pixels[pixel * 4u + component] == 0u;
                    } else if (component < 3u && value > 0.0f) {
                        nonZeroRadiance = true;
                    }
                }
            }
            device.unmapBuffer(readback);
        }
        valid = valid && nonZeroRadiance;
    }

    cleanup();
    valid = valid && device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << "NRD Vulkan Render Graph dispatch validation failed\n";
    }
    return valid;
}
#endif

#if defined(MECRAFT_ENABLE_STREAMLINE)
struct DlssSmokeTexture {
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
};

[[nodiscard]] bool renderStableFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, GLFWwindow* window);

[[nodiscard]] bool createDlssSmokeTexture(VkRhiDevice& device, const char* const debugName,
                                          const RhiTextureFormat format, const TemporalExtent extent,
                                          const RhiTextureUsageFlags usage, const void* const pixels,
                                          const size_t sizeBytes, const RhiResourceState finalState,
                                          DlssSmokeTexture& output) {
    RhiTextureDesc textureDesc;
    textureDesc.debugName = debugName;
    textureDesc.format = format;
    textureDesc.width = extent.width;
    textureDesc.height = extent.height;
    textureDesc.usage = usage;
    RhiTextureInitialData initialData;
    initialData.pixels = pixels;
    initialData.sizeBytes = sizeBytes;
    initialData.finalState = finalState;
    output.texture = device.createTexture(textureDesc, &initialData);
    if (!output.texture.isValid()) {
        return false;
    }
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = output.texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = format;
    output.view = device.createTextureView(viewDesc);
    if (!output.view.isValid()) {
        device.destroyTexture(output.texture);
        output.texture = {};
        return false;
    }
    return true;
}

void destroyDlssSmokeTexture(VkRhiDevice& device, DlssSmokeTexture& resource) {
    if (resource.view.isValid()) {
        device.destroyTextureView(resource.view);
    }
    if (resource.texture.isValid()) {
        device.destroyTexture(resource.texture);
    }
    resource = {};
}

[[nodiscard]] bool validateDlssVulkanDispatch(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                              GLFWwindow* window) {
    constexpr TemporalExtent kOutputExtent{1280u, 720u};
    const DlssRenderExtentResult queried = queryDlssRenderExtent(TemporalUpscaleQuality::Quality, kOutputExtent);
    if (!queried.succeeded()) {
        std::cerr << "DLSS smoke test failed to query the render extent: " << StreamlineRuntime::instance().lastError()
                  << '\n';
        return false;
    }
    const TemporalExtent renderExtent = queried.extent;
    const size_t renderPixelCount = static_cast<size_t>(renderExtent.width) * renderExtent.height;
    std::vector<uint16_t> hdrPixels(renderPixelCount * 4u, 0u);
    std::vector<float> depthPixels(renderPixelCount, 1.0f);
    std::vector<uint16_t> velocityPixels(renderPixelCount * 2u, 0u);
    constexpr uint16_t kHalfFloatOne = 0x3c00u;
    const uint16_t exposurePixels[4] = {kHalfFloatOne, 0u, 0u, 0u};
    std::vector<uint8_t> maskPixels(renderPixelCount, 0u);

    DlssSmokeTexture hdr;
    DlssSmokeTexture depth;
    DlssSmokeTexture velocity;
    DlssSmokeTexture exposure;
    DlssSmokeTexture reactive;
    DlssSmokeTexture transparency;
    const auto destroyInputs = [&]() {
        destroyDlssSmokeTexture(device, transparency);
        destroyDlssSmokeTexture(device, reactive);
        destroyDlssSmokeTexture(device, exposure);
        destroyDlssSmokeTexture(device, velocity);
        destroyDlssSmokeTexture(device, depth);
        destroyDlssSmokeTexture(device, hdr);
    };
    const RhiTextureUsageFlags sampledUsage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const bool inputsCreated =
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Hdr", RhiTextureFormat::Rgba16Float, renderExtent,
                               sampledUsage, hdrPixels.data(), hdrPixels.size() * sizeof(uint16_t),
                               RhiResourceState::ShaderRead, hdr) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Depth", RhiTextureFormat::Depth32Float, renderExtent,
                               sampledUsage | rhiFlag(RhiTextureUsage::DepthStencilAttachment), depthPixels.data(),
                               depthPixels.size() * sizeof(float), RhiResourceState::DepthRead, depth) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Velocity", RhiTextureFormat::Rg16Float, renderExtent,
                               sampledUsage, velocityPixels.data(), velocityPixels.size() * sizeof(uint16_t),
                               RhiResourceState::ShaderRead, velocity) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Exposure", RhiTextureFormat::Rgba16Float, {1u, 1u},
                               sampledUsage, exposurePixels, sizeof(exposurePixels), RhiResourceState::ShaderRead,
                               exposure) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Reactive", RhiTextureFormat::R8Unorm, renderExtent,
                               sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                               reactive) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Transparency", RhiTextureFormat::R8Unorm, renderExtent,
                               sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                               transparency);
    if (!inputsCreated) {
        std::cerr << "DLSS smoke test failed to create input resources\n";
        destroyInputs();
        return false;
    }

    TemporalUpscalePass pass;
    pass.init(device, commandPool);
    UpscaleSettings settings;
    settings.type = TemporalUpscalerType::Dlss;
    settings.quality = TemporalUpscaleQuality::Quality;
    if (!pass.prepareOutputTarget(settings, renderExtent, kOutputExtent)) {
        std::cerr << "DLSS smoke test failed to prepare the output target: "
                  << StreamlineRuntime::instance().lastError() << '\n';
        pass.shutdown();
        destroyInputs();
        return false;
    }

    TemporalFrameInput frame;
    frame.extents = makeTemporalFrameExtents(renderExtent, renderExtent, renderExtent, kOutputExtent);
    frame.motionVectorScale =
        glm::vec2(static_cast<float>(renderExtent.width), static_cast<float>(renderExtent.height));
    frame.frameDeltaMilliseconds = 1000.0f / 60.0f;
    frame.preExposure = 1.0f;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 1000.0f;
    frame.verticalFovRadians = 1.0f;
    frame.cameraAspectRatio = static_cast<float>(kOutputExtent.width) / static_cast<float>(kOutputExtent.height);
    frame.resetReasons = temporalResetReasonBit(TemporalResetReason::FirstFrame);
    frame.textures.hdrColor = hdr.texture;
    frame.textures.hdrColorView = hdr.view;
    frame.textures.depth = depth.texture;
    frame.textures.depthView = depth.view;
    frame.textures.velocity = velocity.texture;
    frame.textures.velocityView = velocity.view;
    frame.textures.exposure = exposure.texture;
    frame.textures.exposureView = exposure.view;
    frame.textures.reactiveMask = reactive.texture;
    frame.textures.reactiveMaskView = reactive.view;
    frame.textures.transparencyMask = transparency.texture;
    frame.textures.transparencyMaskView = transparency.view;
    frame.textures.outputHdrColor = pass.outputTextureHandle();
    frame.textures.outputHdrColorView = pass.outputTextureViewHandle();

    const DlssJitterResult firstJitter = queryDlssJitter(frame.frameIndex, renderExtent, kOutputExtent);
    frame.jitter = firstJitter.jitter;
    const TemporalUpscaleResult firstResult = pass.execute(settings, frame);
    TemporalUpscaleResult secondResult;
    if (firstResult.succeeded()) {
        frame.frameIndex = 1u;
        frame.jitter = queryDlssJitter(frame.frameIndex, renderExtent, kOutputExtent).jitter;
        frame.resetReasons = temporalResetReasonBit(TemporalResetReason::None);
        secondResult = pass.execute(settings, frame);
    }
    device.waitIdle();
    const bool dispatched = firstResult.succeeded() && secondResult.succeeded() &&
                            secondResult.outputHdrColor.isValid() && secondResult.outputHdrColorView.isValid() &&
                            secondResult.outputExtent == kOutputExtent &&
                            renderStableFrame(device, commandPool, window);
    if (!dispatched) {
        std::cerr << "DLSS smoke dispatch failed: "
                  << TemporalUpscalePass::statusText(firstResult.succeeded() ? secondResult.status : firstResult.status)
                  << ", " << StreamlineRuntime::instance().lastError() << '\n';
    }
    pass.shutdown();
    destroyInputs();
    return dispatched;
}
#endif

[[nodiscard]] FrameAttempt renderFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, const uint32_t width,
                                       const uint32_t height) {
    if (!device.resizeSwapchain(width, height)) {
        return FrameAttempt::Error;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return frame.status == RhiFrameStatus::OutOfDate || frame.status == RhiFrameStatus::Minimized
                   ? FrameAttempt::Retry
                   : FrameAttempt::Error;
    }

    RhiCommandList* clearCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (clearCommands == nullptr || !clearCommands->begin({"VulkanSmoke.Clear", RhiCommandListType::Graphics})) {
        return FrameAttempt::Error;
    }
    clearCommands->textureBarrier({frame.colorTexture, RhiResourceState::Present, RhiResourceState::RenderTarget});
    RhiColorAttachment colorAttachment;
    colorAttachment.view = frame.colorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.02f;
    colorAttachment.clearColor[1] = 0.08f;
    colorAttachment.clearColor[2] = 0.16f;
    colorAttachment.clearColor[3] = 1.0f;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VulkanSmoke.Clear";
    renderingInfo.renderArea = {0, 0, frame.width, frame.height};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    clearCommands->beginRendering(renderingInfo);
    clearCommands->endRendering();
    clearCommands->textureBarrier({frame.colorTexture, RhiResourceState::RenderTarget, RhiResourceState::Present});
    if (!clearCommands->end()) {
        return FrameAttempt::Error;
    }
    RhiCommandList* firstSubmission[] = {clearCommands};
    if (!device.submit({"VulkanSmoke.ClearSubmit", firstSubmission, 1u})) {
        return FrameAttempt::Error;
    }

    RhiCommandList* tailCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (tailCommands == nullptr || !tailCommands->begin({"VulkanSmoke.Tail", RhiCommandListType::Graphics}) ||
        !tailCommands->end()) {
        return FrameAttempt::Error;
    }
    RhiCommandList* secondSubmission[] = {tailCommands};
    RhiSubmissionToken secondToken;
    if (!device.submit({"VulkanSmoke.TailSubmit", secondSubmission, 1u}, &secondToken)) {
        return FrameAttempt::Error;
    }
    const RhiFrameStatus presentStatus = device.presentFrame({frame.frameIndex, frame.imageIndex});
    if (presentStatus != RhiFrameStatus::Success && presentStatus != RhiFrameStatus::Suboptimal) {
        return presentStatus == RhiFrameStatus::OutOfDate || presentStatus == RhiFrameStatus::Minimized
                   ? FrameAttempt::Retry
                   : FrameAttempt::Error;
    }
    return device.waitForSubmission(secondToken) ? FrameAttempt::Success : FrameAttempt::Error;
}

[[nodiscard]] bool renderStableFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, GLFWwindow* window) {
    for (uint32_t attempt = 0u; attempt < 20u; ++attempt) {
        glfwWaitEventsTimeout(0.02);
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            continue;
        }
        const FrameAttempt result = renderFrame(device, commandPool, static_cast<uint32_t>(framebufferWidth),
                                                static_cast<uint32_t>(framebufferHeight));
        if (result == FrameAttempt::Success) {
            return true;
        }
        if (result == FrameAttempt::Error) {
            return false;
        }
    }
    return false;
}

#if defined(MECRAFT_ENABLE_STREAMLINE)
[[nodiscard]] bool validateDlssFrameGenerationSwapchainLifecycle(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                                                 GLFWwindow* window) {
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (!streamline.dlssFrameGenerationSupported()) {
        return !streamline.dlssFrameGenerationLoaded();
    }
    if (streamline.dlssFrameGenerationLoaded()) {
        return false;
    }

    const bool restoreVsync = device.vsyncEnabled();
    if (restoreVsync && !device.setVsyncEnabled(false)) {
        return false;
    }
    if (!VkRhiInterop::recreateFrameGenerationSwapchain(device, true) || !streamline.dlssFrameGenerationLoaded()) {
        return false;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    StreamlineDlssFrameGenerationOptions options;
    options.enabled = false;
    options.renderWidth = static_cast<uint32_t>(width);
    options.renderHeight = static_cast<uint32_t>(height);
    options.outputWidth = static_cast<uint32_t>(width);
    options.outputHeight = static_cast<uint32_t>(height);
    options.backBufferCount = device.capabilities().swapchainImageCount;
    options.colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    options.depthFormat = VK_FORMAT_D32_SFLOAT;
    options.motionVectorFormat = VK_FORMAT_R16G16_SFLOAT;
    options.hudlessFormat = VK_FORMAT_B8G8R8A8_UNORM;
    options.uiFormat = VK_FORMAT_B8G8R8A8_UNORM;
    if (width <= 0 || height <= 0 || !streamline.configureDlssFrameGeneration(0u, options)) {
        return false;
    }
    const bool loadedFramePresented = renderStableFrame(device, commandPool, window);
    if (!VkRhiInterop::recreateFrameGenerationSwapchain(device, false) || streamline.dlssFrameGenerationLoaded()) {
        return false;
    }
    if (device.vsyncEnabled() != restoreVsync && !device.setVsyncEnabled(restoreVsync)) {
        return false;
    }
    return loadedFramePresented && renderStableFrame(device, commandPool, window);
}
#endif

[[nodiscard]] bool rejectDestroyedResourceSubmission(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "VulkanSmoke.DestroyedBuffer";
    bufferDesc.size = 256u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0u);
    if (!buffer.isValid())
        return false;

    RhiCommandList* referencingCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (referencingCommands == nullptr ||
        !referencingCommands->begin({"VulkanSmoke.DestroyedResource", RhiCommandListType::Graphics})) {
        device.destroyBuffer(buffer);
        return false;
    }
    referencingCommands->bufferBarrier({buffer, RhiResourceState::Undefined, RhiResourceState::TransferDst});
    if (!referencingCommands->end()) {
        device.destroyBuffer(buffer);
        return false;
    }
    device.destroyBuffer(buffer);

    RhiCommandList* unrelatedCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (unrelatedCommands == nullptr ||
        !unrelatedCommands->begin({"VulkanSmoke.Unrelated", RhiCommandListType::Graphics}) ||
        !unrelatedCommands->end()) {
        return false;
    }
    RhiCommandList* unrelatedSubmission[] = {unrelatedCommands};
    RhiSubmissionToken unrelatedToken;
    if (!device.submit({"VulkanSmoke.UnrelatedSubmit", unrelatedSubmission, 1u}, &unrelatedToken) ||
        !device.waitForSubmission(unrelatedToken)) {
        return false;
    }
    RhiCommandList* invalidSubmission[] = {referencingCommands};
    return !device.submit({"VulkanSmoke.DestroyedResourceSubmit", invalidSubmission, 1u});
}

[[nodiscard]] bool cancelAcquiredFrame(VkRhiDevice& device, GLFWwindow* window) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0 ||
        !device.resizeSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }
    if (!device.cancelFrame({frame.frameIndex, frame.imageIndex})) {
        std::cerr << "Vulkan cancel-frame smoke check failed\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool cancelSubmittedFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, GLFWwindow* window) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0 ||
        !device.resizeSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }
    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr || !commands->begin({"VulkanSmoke.CancelSubmitted", RhiCommandListType::Graphics}) ||
        !commands->end()) {
        return false;
    }
    RhiCommandList* submissions[] = {commands};
    if (!device.submit({"VulkanSmoke.CancelSubmitted", submissions, 1u}) ||
        !device.cancelFrame({frame.frameIndex, frame.imageIndex})) {
        std::cerr << "Vulkan submitted cancel-frame smoke check failed\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool createDrawParametersShader(VkRhiDevice& device) {
    constexpr char kVertexShader[] = R"glsl(
#version 450 core
#extension GL_ARB_shader_draw_parameters : require

void main() {
    const float drawOffset = float(gl_DrawIDARB + gl_BaseInstanceARB) * 0.001;
    gl_Position = vec4(drawOffset, 0.0, 0.0, 1.0);
}
)glsl";
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.DrawParameters";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kVertexShader;
    shaderDesc.sourceSize = sizeof(kVertexShader) - 1u;
    const RhiShaderHandle shader = device.createShader(shaderDesc);
    if (!shader.isValid()) {
        return false;
    }
    device.destroyShader(shader);
    return true;
}

[[nodiscard]] bool validateIndependentBlendPipeline(VkRhiDevice& device) {
    constexpr char kVertexSource[] = R"glsl(
#version 450 core
void main() {
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)glsl";
    constexpr char kFragmentSource[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor0;
layout(location = 1) out vec4 outColor1;
void main() {
    if (gl_FragCoord.x < 0.0) {
        discard;
    }
    outColor0 = vec4(1.0);
    outColor1 = vec4(0.5);
}
)glsl";

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.IndependentBlend.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kVertexSource;
    shaderDesc.sourceSize = sizeof(kVertexSource) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(shaderDesc);
    shaderDesc.debugName = "VulkanSmoke.IndependentBlend.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = kFragmentSource;
    shaderDesc.sourceSize = sizeof(kFragmentSource) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "VulkanSmoke.IndependentBlend.Layout";
    const RhiPipelineLayoutHandle layout = device.createPipelineLayout(layoutDesc);
    if (!vertexShader.isValid() || !fragmentShader.isValid() || !layout.isValid()) {
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VulkanSmoke.IndependentBlend.Pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = layout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm};
    pipelineDesc.blend.attachments.resize(2u);
    pipelineDesc.blend.attachments[1].blendEnabled = true;
    pipelineDesc.blend.attachments[1].srcColor = RhiBlendFactor::One;
    pipelineDesc.blend.attachments[1].dstColor = RhiBlendFactor::Zero;
    pipelineDesc.blend.attachments[1].srcAlpha = RhiBlendFactor::One;
    pipelineDesc.blend.attachments[1].dstAlpha = RhiBlendFactor::Zero;

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    const bool valid = pipeline.isValid() && device.validationErrorCount() == validationErrorsBefore;
    if (pipeline.isValid())
        device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(layout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    return valid;
}

[[nodiscard]] bool validateDescriptorArrayContract(VkRhiDevice& device) {
    const RhiCapabilities& capabilities = device.capabilities();
    if (!capabilities.descriptorBindingPartiallyBound || !capabilities.descriptorBindingVariableDescriptorCount ||
        !capabilities.descriptorBindingUpdateUnusedWhilePending ||
        !capabilities.descriptorBindingSampledImageUpdateAfterBind) {
        std::cerr << "Vulkan descriptor array features are unavailable\n";
        return false;
    }

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.DescriptorArray.Texture";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 1u;
    textureDesc.height = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled);
    std::array<RhiTextureHandle, 2u> textures{};
    std::array<RhiTextureViewHandle, 2u> views{};
    for (size_t index = 0u; index < textures.size(); ++index) {
        textures[index] = device.createTexture(textureDesc, nullptr);
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = textures[index];
        viewDesc.format = textureDesc.format;
        views[index] = device.createTextureView(viewDesc);
    }
    const RhiSamplerHandle sampler = device.createSampler({});

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "VulkanSmoke.DescriptorArray.Layout";
    RhiBindGroupLayoutEntry layoutEntry;
    layoutEntry.binding = 0u;
    layoutEntry.type = RhiBindingType::CombinedTextureSampler;
    layoutEntry.stages = rhiFlag(RhiShaderStage::Fragment);
    layoutEntry.arrayCount = 4u;
    layoutEntry.flags = rhiFlag(RhiBindingFlag::PartiallyBound) | rhiFlag(RhiBindingFlag::UpdateAfterBind) |
                        rhiFlag(RhiBindingFlag::UpdateUnusedWhilePending) |
                        rhiFlag(RhiBindingFlag::VariableDescriptorCount);
    RhiBindGroupLayoutEntry updateUnusedEntry;
    updateUnusedEntry.binding = 0u;
    updateUnusedEntry.type = RhiBindingType::Sampler;
    updateUnusedEntry.stages = rhiFlag(RhiShaderStage::Fragment);
    updateUnusedEntry.flags = rhiFlag(RhiBindingFlag::UpdateUnusedWhilePending);
    layoutEntry.binding = 1u;
    layoutDesc.entries.push_back(updateUnusedEntry);
    layoutDesc.entries.push_back(layoutEntry);
    const RhiBindGroupLayoutHandle layout = device.createBindGroupLayout(layoutDesc);

    RhiBindGroupDesc groupDesc;
    groupDesc.layout = layout;
    groupDesc.variableDescriptorCount = 2u;
    RhiBindGroupEntry entry;
    entry.binding = 1u;
    entry.arrayElement = 1u;
    entry.resource.combinedTextureSampler = {views[1], sampler};
    RhiBindGroupEntry updateUnusedGroupEntry;
    updateUnusedGroupEntry.binding = 0u;
    updateUnusedGroupEntry.resource.sampler = sampler;
    groupDesc.entries.push_back(updateUnusedGroupEntry);
    groupDesc.entries.push_back(entry);
    const RhiBindGroupHandle group = device.createBindGroup(groupDesc);
    const bool valid = layout.isValid() && group.isValid() && textures[0].isValid() && textures[1].isValid() &&
                       views[0].isValid() && views[1].isValid() && sampler.isValid() &&
                       device.validationErrorCount() == validationErrorsBefore;

    device.destroyBindGroup(group);
    device.destroyBindGroupLayout(layout);
    device.destroySampler(sampler);
    for (size_t index = 0u; index < textures.size(); ++index) {
        device.destroyTextureView(views[index]);
        device.destroyTexture(textures[index]);
    }
    if (!valid) {
        std::cerr << "Vulkan descriptor arrays, flags, or variable counts violated the native contract\n";
    }
    return valid;
}

[[nodiscard]] bool validateBindGroupUpdateLifecycle(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    const RhiCapabilities& capabilities = device.capabilities();
    if (!capabilities.descriptorBindingPartiallyBound || !capabilities.descriptorBindingVariableDescriptorCount ||
        !capabilities.descriptorBindingUpdateUnusedWhilePending ||
        !capabilities.descriptorBindingStorageBufferUpdateAfterBind) {
        std::cerr << "Vulkan bind-group update lifecycle features are unavailable\n";
        return false;
    }

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "VulkanSmoke.BindGroupUpdate.Buffer";
    bufferDesc.size = 256u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Storage);
    bufferDesc.initialState = RhiResourceState::StorageBuffer;
    std::array<RhiBufferHandle, 3u> buffers{};
    for (RhiBufferHandle& buffer : buffers) {
        buffer = device.createBuffer(bufferDesc, nullptr, 0u);
    }

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "VulkanSmoke.BindGroupUpdate.Layout";
    layoutDesc.entries.push_back({0u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u, 0u});
    layoutDesc.entries.push_back({1u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u,
                                  rhiFlag(RhiBindingFlag::UpdateAfterBind)});
    layoutDesc.entries.push_back({2u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 2u,
                                  rhiFlag(RhiBindingFlag::PartiallyBound) | rhiFlag(RhiBindingFlag::UpdateAfterBind) |
                                      rhiFlag(RhiBindingFlag::UpdateUnusedWhilePending) |
                                      rhiFlag(RhiBindingFlag::VariableDescriptorCount)});
    const RhiBindGroupLayoutHandle bindGroupLayout = device.createBindGroupLayout(layoutDesc);

    const auto makeBufferResource = [&](const RhiBufferHandle buffer) {
        RhiBindingResource resource;
        resource.buffer.buffer = buffer;
        resource.buffer.range = bufferDesc.size;
        return resource;
    };
    RhiBindGroupDesc groupDesc;
    groupDesc.layout = bindGroupLayout;
    groupDesc.variableDescriptorCount = 2u;
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource = makeBufferResource(buffers[0]);
        groupDesc.entries.push_back(entry);
    }
    const RhiBindGroupHandle bindGroup = device.createBindGroup(groupDesc);

    constexpr char kComputeSource[] = R"glsl(
#version 450 core
layout(local_size_x = 64) in;
void main() {}
)glsl";
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.BindGroupUpdate.Compute";
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = kComputeSource;
    shaderDesc.sourceSize = sizeof(kComputeSource) - 1u;
    const RhiShaderHandle computeShader = device.createShader(shaderDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "VulkanSmoke.BindGroupUpdate.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(bindGroupLayout);
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VulkanSmoke.BindGroupUpdate.Pipeline";
    pipelineDesc.computeShader = computeShader;
    pipelineDesc.layout = pipelineLayout;
    const RhiPipelineHandle pipeline = device.createComputePipeline(pipelineDesc);

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Compute);
    bool valid = buffers[0].isValid() && buffers[1].isValid() && buffers[2].isValid() && bindGroupLayout.isValid() &&
                 bindGroup.isValid() && computeShader.isValid() && pipelineLayout.isValid() && pipeline.isValid() &&
                 commands != nullptr &&
                 commands->begin({"VulkanSmoke.BindGroupUpdate.Commands", RhiCommandListType::Compute});
    if (valid) {
        commands->setComputePipeline(pipeline);
        for (uint32_t dispatchIndex = 0u; dispatchIndex < 32u; ++dispatchIndex) {
            commands->dispatch(65535u, 1u, 1u);
        }
        commands->setBindGroup(0u, bindGroup);
        valid = commands->end();
    }

    RhiBindingResource replacement = makeBufferResource(buffers[1]);
    const RhiBindGroupUpdate normalRecordedUpdate{bindGroup, 0u, 0u, &replacement, 1u};
    const RhiBindGroupUpdate updateAfterBindRecordedUpdate{bindGroup, 1u, 0u, &replacement, 1u};
    std::array<RhiBindingResource, 2u> rangeResources{makeBufferResource(buffers[1]), makeBufferResource(buffers[2])};
    const RhiBindGroupUpdate rangeRecordedUpdate{bindGroup, 2u, 0u, rangeResources.data(),
                                                 static_cast<uint32_t>(rangeResources.size())};
    const std::array<RhiBindGroupUpdate, 2u> recordedBatch{{updateAfterBindRecordedUpdate, rangeRecordedUpdate}};
    const std::array<RhiBindGroupUpdate, 2u> overlappingBatch{
        {{bindGroup, 2u, 0u, rangeResources.data(), 1u}, {bindGroup, 2u, 0u, rangeResources.data() + 1u, 1u}}};
    valid = valid && !device.updateBindGroups(overlappingBatch.data(), overlappingBatch.size()) &&
            !device.updateBindGroups(&normalRecordedUpdate, 1u) &&
            device.updateBindGroups(recordedBatch.data(), recordedBatch.size());

    RhiSubmissionToken token;
    if (valid) {
        RhiCommandList* submissions[] = {commands};
        RhiSubmitInfo submitInfo{"VulkanSmoke.BindGroupUpdate.Submit", submissions, 1u};
        submitInfo.queue = RhiQueueType::Compute;
        valid = device.submit(submitInfo, &token);
    }

    replacement = makeBufferResource(buffers[2]);
    const RhiBindGroupUpdate updateAfterBindPendingUpdate{bindGroup, 1u, 0u, &replacement, 1u};
    const RhiBindGroupUpdate updateUnusedPendingUpdate{bindGroup, 2u, 0u, &replacement, 1u};
    valid = valid && !device.updateBindGroups(&updateAfterBindPendingUpdate, 1u) &&
            device.updateBindGroups(&updateUnusedPendingUpdate, 1u);

    device.destroyBuffer(buffers[1]);
    const RhiBufferHandle reusedBuffer = device.createBuffer(bufferDesc, nullptr, 0u);
    const bool handleSlotReused =
        reusedBuffer.index == buffers[1].index && reusedBuffer.generation != buffers[1].generation;
    replacement = makeBufferResource(reusedBuffer);
    const RhiBindGroupUpdate reusedPendingUpdate{bindGroup, 2u, 0u, &replacement, 1u};
    valid = valid && handleSlotReused && device.updateBindGroups(&reusedPendingUpdate, 1u);
    device.destroyBuffer(reusedBuffer);
    valid = valid && device.waitForSubmission(token) && commandPool.reset();

    replacement = makeBufferResource(buffers[2]);
    const RhiBindGroupUpdate completedUpdate{bindGroup, 0u, 0u, &replacement, 1u};
    valid = valid && device.updateBindGroups(&completedUpdate, 1u);

    device.destroyBindGroup(bindGroup);
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyShader(computeShader);
    device.destroyBindGroupLayout(bindGroupLayout);
    device.destroyBuffer(buffers[2]);
    device.destroyBuffer(buffers[0]);
    device.waitIdle();
    if (!valid || device.validationErrorCount() != validationErrorsBefore) {
        std::cerr << "Vulkan bind-group batch updates or submission lifetimes violated the native contract\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool validateOffscreenCoordinateContract(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                                       GLFWwindow* window) {
    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;
    constexpr uint32_t kRenderY = 1u;
    constexpr uint32_t kRenderHeight = 2u;
    constexpr uint32_t kBytesPerPixel = 4u;
    constexpr uint32_t kBytesPerRow = kWidth * kBytesPerPixel;
    constexpr uint32_t kReadbackSize = kBytesPerRow * kHeight;

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.OffscreenOrientation";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = kWidth;
    textureDesc.height = kHeight;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = textureDesc.format;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!view.isValid()) {
        return false;
    }

    constexpr char kVertexSource[] = R"glsl(
#version 450 core
layout(location = 0) out vec2 vTexCoord;
void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 position = positions[gl_VertexIndex];
    vTexCoord = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";
    constexpr char kFragmentSource[] = R"glsl(
#version 450 core
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vTexCoord.y < 0.5
        ? vec4(1.0, 0.0, 0.0, 1.0)
        : vec4(0.0, 0.0, 1.0, 1.0);
}
)glsl";

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.OffscreenOrientation.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kVertexSource;
    shaderDesc.sourceSize = sizeof(kVertexSource) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(shaderDesc);
    shaderDesc.debugName = "VulkanSmoke.OffscreenOrientation.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = kFragmentSource;
    shaderDesc.sourceSize = sizeof(kFragmentSource) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(shaderDesc);
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        return false;
    }

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "VulkanSmoke.OffscreenOrientation.Layout";
    const RhiPipelineLayoutHandle layout = device.createPipelineLayout(layoutDesc);
    if (!layout.isValid()) {
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VulkanSmoke.OffscreenOrientation.Pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = layout;
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.raster.frontFace = RhiFrontFace::CounterClockwise;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(textureDesc.format);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!pipeline.isValid()) {
        return false;
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.OffscreenOrientation.Readback";
    readbackDesc.size = kReadbackSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return false;
    }

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr ||
        !commands->begin({"VulkanSmoke.OffscreenOrientation.Commands", RhiCommandListType::Graphics})) {
        return false;
    }
    commands->textureBarrier({texture, RhiResourceState::Undefined, RhiResourceState::RenderTarget});
    RhiColorAttachment colorAttachment;
    colorAttachment.view = view;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VulkanSmoke.OffscreenOrientation.Rendering";
    renderingInfo.renderArea = {0, static_cast<int32_t>(kRenderY), kWidth, kRenderHeight};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commands->beginRendering(renderingInfo);
    commands->setGraphicsPipeline(pipeline);
    commands->draw(3u, 1u, 0u, 0u);
    commands->endRendering();
    commands->textureBarrier({texture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});
    RhiTextureBufferCopy copy;
    copy.srcTexture = texture;
    copy.dstBuffer = readback;
    copy.bytesPerRow = kBytesPerRow;
    copy.rowsPerImage = kHeight;
    copy.width = kWidth;
    copy.height = kHeight;
    commands->copyTextureToBuffer(copy);
    commands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
    if (!commands->end()) {
        return false;
    }
    RhiCommandList* submissions[] = {commands};
    RhiSubmissionToken token;
    if (!device.submit({"VulkanSmoke.OffscreenOrientation.Submit", submissions, 1u}, &token) ||
        !device.waitForSubmission(token)) {
        return false;
    }

    const auto* pixels = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, kReadbackSize));
    if (pixels == nullptr) {
        return false;
    }
    const auto isRed = [](const uint8_t* pixel) {
        return pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    };
    const auto isBlue = [](const uint8_t* pixel) {
        return pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    };
    const bool orientationCorrect = isBlue(pixels + static_cast<size_t>(kRenderY) * kBytesPerRow) &&
                                    isRed(pixels + static_cast<size_t>(kRenderY + kRenderHeight - 1u) * kBytesPerRow);
    device.unmapBuffer(readback);
    if (!orientationCorrect) {
        return false;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0 ||
        !device.resizeSwapchain(static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }

    RhiGraphicsPipelineDesc presentationPipelineDesc = pipelineDesc;
    presentationPipelineDesc.debugName = "VulkanSmoke.PresentationOrientation.Pipeline";
    presentationPipelineDesc.colorFormats.clear();
    presentationPipelineDesc.colorFormats.push_back(device.swapchainColorFormat());
    const RhiPipelineHandle presentationPipeline = device.createGraphicsPipeline(presentationPipelineDesc);
    if (!presentationPipeline.isValid()) {
        return false;
    }

    RhiCommandList* presentationCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (presentationCommands == nullptr ||
        !presentationCommands->begin({"VulkanSmoke.PresentationOrientation.Commands", RhiCommandListType::Graphics})) {
        return false;
    }
    presentationCommands->bufferBarrier({readback, RhiResourceState::HostRead, RhiResourceState::TransferDst});
    presentationCommands->textureBarrier({texture, RhiResourceState::TransferSrc, RhiResourceState::RenderTarget});
    renderingInfo.renderArea = {0, 0, kWidth, kHeight};
    presentationCommands->beginRendering(renderingInfo);
    presentationCommands->setGraphicsPipeline(pipeline);
    presentationCommands->draw(3u, 1u, 0u, 0u);
    presentationCommands->endRendering();
    presentationCommands->textureBarrier({texture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});

    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::Present, RhiResourceState::RenderTarget});
    RhiColorAttachment presentationAttachment;
    presentationAttachment.view = frame.colorView;
    presentationAttachment.loadOp = RhiLoadOp::Clear;
    presentationAttachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo presentationRendering;
    presentationRendering.debugName = "VulkanSmoke.PresentationOrientation.Rendering";
    presentationRendering.renderArea = {0, 0, frame.width, frame.height};
    presentationRendering.colorAttachments = &presentationAttachment;
    presentationRendering.colorAttachmentCount = 1u;
    presentationCommands->beginRendering(presentationRendering);
    presentationCommands->setGraphicsPipeline(presentationPipeline);
    presentationCommands->draw(3u, 1u, 0u, 0u);
    presentationCommands->endRendering();
    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});

    RhiTextureBufferCopy presentationCopy;
    presentationCopy.srcTexture = frame.colorTexture;
    presentationCopy.dstBuffer = readback;
    presentationCopy.bytesPerRow = kBytesPerPixel;
    presentationCopy.rowsPerImage = 1u;
    presentationCopy.srcX = frame.width / 2u;
    presentationCopy.width = 1u;
    presentationCopy.height = 1u;
    presentationCopy.bufferOffset = 0u;
    presentationCopy.srcY = 0u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCopy.bufferOffset = kBytesPerPixel;
    presentationCopy.srcY = frame.height - 1u;
    presentationCommands->copyTextureToBuffer(presentationCopy);

    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::TransferSrc, RhiResourceState::TransferDst});
    RhiTextureBlit presentationBlit;
    presentationBlit.src = texture;
    presentationBlit.dstView = frame.colorView;
    presentationCommands->blitTexture(presentationBlit);
    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::TransferDst, RhiResourceState::TransferSrc});
    presentationCopy.bufferOffset = kBytesPerPixel * 2u;
    presentationCopy.srcY = 0u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCopy.bufferOffset = kBytesPerPixel * 3u;
    presentationCopy.srcY = frame.height - 1u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCommands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::TransferSrc, RhiResourceState::Present});
    if (!presentationCommands->end()) {
        return false;
    }
    RhiCommandList* presentationSubmissions[] = {presentationCommands};
    RhiSubmissionToken presentationToken;
    if (!device.submit({"VulkanSmoke.PresentationOrientation.Submit", presentationSubmissions, 1u},
                       &presentationToken)) {
        return false;
    }
    const RhiFrameStatus presentStatus = device.presentFrame({frame.frameIndex, frame.imageIndex});
    if ((presentStatus != RhiFrameStatus::Success && presentStatus != RhiFrameStatus::Suboptimal) ||
        !device.waitForSubmission(presentationToken)) {
        return false;
    }

    const auto* presentationPixels = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, kBytesPerPixel * 4u));
    if (presentationPixels == nullptr) {
        return false;
    }
    const auto isBgraRed = [](const uint8_t* pixel) {
        return pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    };
    const auto isBgraBlue = [](const uint8_t* pixel) {
        return pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    };
    const bool presentationCorrect = isBgraBlue(presentationPixels) && isBgraRed(presentationPixels + kBytesPerPixel) &&
                                     isBgraBlue(presentationPixels + kBytesPerPixel * 2u) &&
                                     isBgraRed(presentationPixels + kBytesPerPixel * 3u);
    device.unmapBuffer(readback);

    device.destroyBuffer(readback);
    device.destroyPipeline(presentationPipeline);
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(layout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.destroyTextureView(view);
    device.destroyTexture(texture);
    return presentationCorrect;
}

[[nodiscard]] bool validateRenderGraphMultiQueue(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr std::array<uint32_t, 4u> kPayload{0x13579BDFu, 0x2468ACE0u, 0x10203040u, 0x55667788u};
    constexpr uint64_t kPayloadSize = sizeof(kPayload);

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.RenderGraph.Readback";
    readbackDesc.size = kPayloadSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return false;
    }

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    RenderGraph graph;
    RhiBufferHandle firstTransientBuffer;
    RhiTextureHandle firstTransientTexture;
    const auto executeFrame = [&](const RhiResourceState readbackInitialState, const bool expectReuse,
                                  const size_t expectedSubmissionCount) {
        graph.reset();

        RhiBufferDesc transientBufferDesc;
        transientBufferDesc.debugName = "VulkanSmoke.RenderGraph.TransientBuffer";
        transientBufferDesc.size = kPayloadSize;
        transientBufferDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::TransferSrc) |
                                    rhiFlag(RhiBufferUsage::Storage);
        const RgBufferHandle transientBuffer =
            graph.createBuffer({"MultiQueueBuffer", transientBufferDesc, RhiResourceState::Undefined});

        RhiTextureDesc transientTextureDesc;
        transientTextureDesc.debugName = "VulkanSmoke.RenderGraph.TransientTexture";
        transientTextureDesc.format = RhiTextureFormat::Rgba8Unorm;
        transientTextureDesc.width = 8u;
        transientTextureDesc.height = 8u;
        transientTextureDesc.usage = rhiFlag(RhiTextureUsage::TransferDst) | rhiFlag(RhiTextureUsage::Storage) |
                                     rhiFlag(RhiTextureUsage::Sampled);
        const RgTextureHandle transientTexture =
            graph.createTexture({"MultiQueueTexture", transientTextureDesc, RhiResourceState::Undefined});

        RhiBufferDesc importedReadbackDesc = readbackDesc;
        importedReadbackDesc.initialState = readbackInitialState;
        const RgBufferHandle importedReadback =
            graph.importBuffer({"Readback", readback, importedReadbackDesc, readbackInitialState,
                                RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});

        RhiBufferHandle resolvedBuffer;
        RhiTextureHandle resolvedTexture;
        graph.addPass({"MultiQueue.Transfer", RgPassType::Copy, RhiQueueType::Transfer})
            .writeBuffer(transientBuffer, RhiResourceState::TransferDst)
            .writeTexture(transientTexture, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                resolvedBuffer = context.buffer(transientBuffer);
                resolvedTexture = context.texture(transientTexture);
                context.commandList().updateBuffer(resolvedBuffer, 0u, kPayload.data(), sizeof(kPayload));
                return resolvedBuffer.isValid() && resolvedTexture.isValid();
            });
        graph.addPass({"MultiQueue.Compute", RgPassType::Compute, RhiQueueType::Compute})
            .readWriteBuffer(transientBuffer, RhiResourceState::StorageBuffer)
            .readWriteTexture(transientTexture, RhiResourceState::ShaderWrite)
            .setExecute([](RgPassContext&) { return true; });
        graph.addPass({"MultiQueue.Graphics", RgPassType::Copy, RhiQueueType::Graphics})
            .readBuffer(transientBuffer, RhiResourceState::TransferSrc)
            .readTexture(transientTexture, RhiResourceState::ShaderRead)
            .writeBuffer(importedReadback, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                context.commandList().copyBuffer(
                    {context.buffer(transientBuffer), context.buffer(importedReadback), 0u, 0u, kPayloadSize});
                return true;
            });

        const RgCompileResult compiled = graph.compile();
        if (!compiled.succeeded()) {
            std::cerr << "Render Graph multi-queue compile failed: " << compiled.message << '\n';
            return false;
        }
        const auto& batches = graph.submissionBatches();
        if (batches.size() != 3u || batches[0].queue != RhiQueueType::Transfer ||
            batches[1].queue != RhiQueueType::Compute || batches[2].queue != RhiQueueType::Graphics ||
            batches[1].dependencies.size() != 1u || batches[1].dependencies[0] != 0u ||
            batches[2].dependencies.size() != 1u || batches[2].dependencies[0] != 1u) {
            std::cerr << "Render Graph multi-queue batch plan is invalid\n";
            return false;
        }

        const RgExecuteResult executed = graph.execute(device, commandPool);
        if (!executed.succeeded()) {
            std::cerr << "Render Graph multi-queue execution failed: " << executed.message << '\n';
            return false;
        }
        if (executed.submissions.size() != expectedSubmissionCount) {
            std::cerr << "Render Graph multi-queue submission count mismatch\n";
            return false;
        }
        const size_t queueTokenOffset = expectedSubmissionCount - 3u;
        if (executed.submissions[queueTokenOffset].queue != RhiQueueType::Transfer ||
            executed.submissions[queueTokenOffset + 1u].queue != RhiQueueType::Compute ||
            executed.submissions[queueTokenOffset + 2u].queue != RhiQueueType::Graphics ||
            executed.submissions[queueTokenOffset].timelineValue() == 0u ||
            executed.submissions[queueTokenOffset + 1u].timelineValue() == 0u ||
            executed.submissions[queueTokenOffset + 2u].timelineValue() == 0u) {
            std::cerr << "Render Graph multi-queue timeline chain is invalid\n";
            return false;
        }
        if (!expectReuse) {
            firstTransientBuffer = resolvedBuffer;
            firstTransientTexture = resolvedTexture;
            return true;
        }
        if (!device.waitForSubmission(executed.completionToken())) {
            std::cerr << "Render Graph multi-frame timeline chain is invalid\n";
            return false;
        }
        if (graph.pollTimings(device) != RgTimingPollResult::Available) {
            std::cerr << "Render Graph multi-queue timings are unavailable\n";
            return false;
        }
        const RgTimingSnapshot& timings = graph.latestTimings();
        if (!timings.isValid() || timings.execution != 2u || timings.passes.size() != 3u ||
            timings.passes[0].name != "MultiQueue.Transfer" || timings.passes[0].queue != RhiQueueType::Transfer ||
            timings.passes[1].name != "MultiQueue.Compute" || timings.passes[1].queue != RhiQueueType::Compute ||
            timings.passes[2].name != "MultiQueue.Graphics" || timings.passes[2].queue != RhiQueueType::Graphics) {
            std::cerr << "Render Graph multi-queue timing snapshot is invalid\n";
            return false;
        }

        const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(readback, 0u, kPayloadSize));
        if (mapped == nullptr) {
            return false;
        }
        const bool payloadMatches = std::equal(kPayload.begin(), kPayload.end(), mapped);
        device.unmapBuffer(readback);
        if (!payloadMatches) {
            std::cerr << "Render Graph multi-queue payload mismatch\n";
            return false;
        }

        return resolvedBuffer.index == firstTransientBuffer.index &&
               resolvedBuffer.generation == firstTransientBuffer.generation &&
               resolvedTexture.index == firstTransientTexture.index &&
               resolvedTexture.generation == firstTransientTexture.generation;
    };

    const bool valid =
        executeFrame(RhiResourceState::TransferDst, false, 3u) && executeFrame(RhiResourceState::HostRead, true, 4u);
    graph.releaseTransientResources(device);
    device.destroyBuffer(readback);
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

// Builds a graph of independent thread-safe copy passes spanning multiple
// submission batches and executes it with worker-thread recording enabled.
// Validates payload correctness through a readback and that at least one
// batch was actually recorded off the calling thread.
[[nodiscard]] bool validateRenderGraphMultithreadedRecord(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr uint32_t kPassCount = 12u;
    constexpr uint64_t kChunkSize = 16u;
    constexpr uint64_t kPayloadSize = kPassCount * kChunkSize;

    std::array<uint32_t, kPayloadSize / sizeof(uint32_t)> payload{};
    for (uint32_t index = 0u; index < payload.size(); ++index) {
        payload[index] = 0x9E3779B9u * (index + 1u);
    }

    RhiBufferDesc sourceDesc;
    sourceDesc.debugName = "VulkanSmoke.MtRecord.Source";
    sourceDesc.size = kPayloadSize;
    sourceDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst);
    sourceDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    sourceDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle source = device.createBuffer(sourceDesc, payload.data(), kPayloadSize);
    if (!source.isValid()) {
        return false;
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.MtRecord.Readback";
    readbackDesc.size = kPayloadSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        device.destroyBuffer(source);
        return false;
    }

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    ThreadPool workerPool(2);
    workerPool.start();
    bool valid = true;
    {
        RenderGraph graph;
        graph.setRecordThreading(&workerPool);

        // Two frames: the second one exercises worker command-list pool
        // reuse and the cross-thread completion recycling path.
        const auto executeFrame = [&](const RhiResourceState readbackInitialState) {
            graph.reset();
            const RgBufferHandle importedSource =
                graph.importBuffer({"MtSource", source, sourceDesc, RhiResourceState::TransferSrc,
                                    RhiResourceState::TransferSrc, RhiQueueType::Graphics, RhiQueueType::Graphics});
            RhiBufferDesc importedReadbackDesc = readbackDesc;
            importedReadbackDesc.initialState = readbackInitialState;
            const RgBufferHandle importedReadback =
                graph.importBuffer({"MtReadback", readback, importedReadbackDesc, readbackInitialState,
                                    RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});

            // Twelve independent chunk copies exceed the per-batch pass cap,
            // producing at least two worker-eligible submission batches.
            char passNames[kPassCount][32];
            for (uint32_t pass = 0u; pass < kPassCount; ++pass) {
                std::snprintf(passNames[pass], sizeof(passNames[pass]), "MtRecord.Copy%u", pass);
                const uint64_t offset = pass * kChunkSize;
                graph
                    .addPass({passNames[pass], RgPassType::Copy, RhiQueueType::Graphics,
                              /*threadSafeRecord=*/true})
                    .readBuffer(importedSource, RhiResourceState::TransferSrc, {offset, kChunkSize})
                    .writeBuffer(importedReadback, RhiResourceState::TransferDst, {offset, kChunkSize})
                    .setExecute([&, offset](RgPassContext& context) {
                        context.commandList().copyBuffer({context.buffer(importedSource),
                                                          context.buffer(importedReadback), offset, offset,
                                                          kChunkSize});
                        return true;
                    });
            }

            const RgCompileResult compiled = graph.compile();
            if (!compiled.succeeded()) {
                std::cerr << "Render Graph MT record compile failed: " << compiled.message << '\n';
                return false;
            }
            const size_t batchCount = graph.submissionBatches().size();
            if (batchCount < 2u) {
                std::cerr << "Render Graph MT record produced too few "
                             "batches\n";
                return false;
            }
            const RgExecuteResult executed = graph.execute(device, commandPool);
            if (!executed.succeeded()) {
                std::cerr << "Render Graph MT record execution failed: " << executed.message << '\n';
                return false;
            }
            if (executed.workerRecordedBatchCount != static_cast<uint32_t>(batchCount)) {
                std::cerr << "Render Graph MT record worker batch count " << executed.workerRecordedBatchCount
                          << " does not match batch count " << batchCount << '\n';
                return false;
            }
            if (!device.waitForSubmission(executed.completionToken())) {
                std::cerr << "Render Graph MT record wait failed\n";
                return false;
            }
            // Collecting timings forces submission-completion polling, which
            // marks worker-recorded lists for owner-thread recycling.
            if (graph.pollTimings(device) == RgTimingPollResult::Error) {
                std::cerr << "Render Graph MT record timing poll failed\n";
                return false;
            }
            const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(readback, 0u, kPayloadSize));
            if (mapped == nullptr) {
                return false;
            }
            const bool payloadMatches = std::equal(payload.begin(), payload.end(), mapped);
            device.unmapBuffer(readback);
            if (!payloadMatches) {
                std::cerr << "Render Graph MT record payload mismatch\n";
                return false;
            }
            return true;
        };

        valid = executeFrame(RhiResourceState::TransferDst) && executeFrame(RhiResourceState::HostRead);
        graph.releaseTransientResources(device);
    }
    workerPool.shutdown();
    device.destroyBuffer(readback);
    device.destroyBuffer(source);
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateRenderGraphTextureAliasing(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr uint32_t kSizeA = 64u;
    constexpr uint32_t kSizeB = 32u;
    constexpr uint64_t kPixelBytesA = kSizeA * kSizeA * 4u;
    constexpr uint64_t kPixelBytesB = kSizeB * kSizeB * 4u;
    constexpr uint8_t kFillA = 0xA5u;
    constexpr uint8_t kFillB = 0x3Cu;
    const std::vector<uint8_t> payloadA(kPixelBytesA, kFillA);
    const std::vector<uint8_t> payloadB(kPixelBytesB, kFillB);

    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiBufferDesc uploadDesc;
    uploadDesc.debugName = "VulkanSmoke.GraphAlias.Upload";
    uploadDesc.size = kPixelBytesA;
    uploadDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc);
    uploadDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    uploadDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle uploadA = device.createBuffer(uploadDesc, payloadA.data(), payloadA.size());
    uploadDesc.size = kPixelBytesB;
    const RhiBufferHandle uploadB = device.createBuffer(uploadDesc, payloadB.data(), payloadB.size());
    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.GraphAlias.Readback";
    readbackDesc.size = kPixelBytesA + kPixelBytesB;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!uploadA.isValid() || !uploadB.isValid() || !readback.isValid()) {
        return false;
    }

    RenderGraph graph;
    RhiTextureHandle resolvedFirst;
    RhiTextureHandle resolvedSecond;
    const auto executeFrame = [&]() {
        graph.reset();
        RhiTextureDesc transientDesc;
        transientDesc.format = RhiTextureFormat::Rgba8Unorm;
        transientDesc.width = kSizeA;
        transientDesc.height = kSizeA;
        transientDesc.usage = rhiFlag(RhiTextureUsage::TransferDst) | rhiFlag(RhiTextureUsage::TransferSrc) |
                              rhiFlag(RhiTextureUsage::Sampled);
        // Two graphics-only transients with different descriptions and
        // disjoint pass intervals: the aliasing allocator must land them on
        // one shared page as distinct placed images while their payloads
        // stay isolated by the serial barrier chain.
        const RgTextureHandle first = graph.createTexture({"AliasFirst", transientDesc, RhiResourceState::Undefined});
        transientDesc.width = kSizeB;
        transientDesc.height = kSizeB;
        const RgTextureHandle second = graph.createTexture({"AliasSecond", transientDesc, RhiResourceState::Undefined});
        RhiBufferDesc importedUploadDesc = uploadDesc;
        const RgBufferHandle importedA =
            graph.importBuffer({"UploadA", uploadA, importedUploadDesc, RhiResourceState::TransferSrc,
                                RhiResourceState::TransferSrc, RhiQueueType::Graphics, RhiQueueType::Graphics});
        const RgBufferHandle importedB =
            graph.importBuffer({"UploadB", uploadB, importedUploadDesc, RhiResourceState::TransferSrc,
                                RhiResourceState::TransferSrc, RhiQueueType::Graphics, RhiQueueType::Graphics});
        RhiBufferDesc importedReadbackDesc = readbackDesc;
        const RgBufferHandle importedReadback =
            graph.importBuffer({"Readback", readback, importedReadbackDesc, RhiResourceState::TransferDst,
                                RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});

        const auto fillPass = [&](const char* name, const RgTextureHandle target, const RgBufferHandle source,
                                  const uint32_t extent, RhiTextureHandle* resolved) {
            graph.addPass({name, RgPassType::Copy, RhiQueueType::Graphics})
                .readBuffer(source, RhiResourceState::TransferSrc)
                .writeTexture(target, RhiResourceState::TransferDst)
                .setExecute([&, target, source, extent, resolved](RgPassContext& context) {
                    if (resolved != nullptr) {
                        *resolved = context.texture(target);
                    }
                    RhiBufferTextureCopy copy;
                    copy.srcBuffer = context.buffer(source);
                    copy.dstTexture = context.texture(target);
                    copy.width = extent;
                    copy.height = extent;
                    context.commandList().copyBufferToTexture(copy);
                    return true;
                });
        };
        const auto drainPass = [&](const char* name, const RgTextureHandle target, const uint32_t extent,
                                   const uint64_t readbackOffset) {
            graph.addPass({name, RgPassType::Copy, RhiQueueType::Graphics})
                .readTexture(target, RhiResourceState::TransferSrc)
                .writeBuffer(importedReadback, RhiResourceState::TransferDst)
                .setExecute([&, target, extent, readbackOffset](RgPassContext& context) {
                    RhiTextureBufferCopy copy;
                    copy.srcTexture = context.texture(target);
                    copy.dstBuffer = context.buffer(importedReadback);
                    copy.bufferOffset = readbackOffset;
                    copy.width = extent;
                    copy.height = extent;
                    context.commandList().copyTextureToBuffer(copy);
                    return true;
                });
        };
        fillPass("Alias.FillFirst", first, importedA, kSizeA, &resolvedFirst);
        drainPass("Alias.DrainFirst", first, kSizeA, 0u);
        fillPass("Alias.FillSecond", second, importedB, kSizeB, &resolvedSecond);
        drainPass("Alias.DrainSecond", second, kSizeB, kPixelBytesA);

        const RgCompileResult compiled = graph.compile();
        if (!compiled.succeeded()) {
            std::cerr << "Alias graph compile failed: " << compiled.message << '\n';
            return false;
        }
        const RgExecuteResult executed = graph.execute(device, commandPool);
        if (!executed.succeeded()) {
            std::cerr << "Alias graph execution failed: " << executed.message << '\n';
            return false;
        }
        if (!device.waitForSubmission(executed.completionToken())) {
            return false;
        }
        const auto* mapped = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, kPixelBytesA + kPixelBytesB));
        if (mapped == nullptr) {
            return false;
        }
        const bool isolated =
            std::all_of(mapped, mapped + kPixelBytesA, [](const uint8_t value) { return value == kFillA; }) &&
            std::all_of(mapped + kPixelBytesA, mapped + kPixelBytesA + kPixelBytesB,
                        [](const uint8_t value) { return value == kFillB; });
        device.unmapBuffer(readback);
        if (!isolated) {
            std::cerr << "Aliased transient payloads were not isolated\n";
            return false;
        }
        return true;
    };

    bool valid = executeFrame();
    const RgTransientMemoryStats statsFirstFrame = graph.transientMemoryStats();
    if (valid && (statsFirstFrame.aliasedTextureCount != 2u || statsFirstFrame.pageCount != 1u ||
                  statsFirstFrame.aliasedPageBytes >= statsFirstFrame.aliasedRequestBytes)) {
        std::cerr << "Alias stats mismatch: textures=" << statsFirstFrame.aliasedTextureCount
                  << " pages=" << statsFirstFrame.pageCount << '\n';
        valid = false;
    }
    const RhiTextureHandle firstFrameFirst = resolvedFirst;
    const RhiTextureHandle firstFrameSecond = resolvedSecond;
    if (valid && (firstFrameFirst.index == firstFrameSecond.index &&
                  firstFrameFirst.generation == firstFrameSecond.generation)) {
        std::cerr << "Aliased transients resolved to one texture\n";
        valid = false;
    }
    // A second identical frame must reuse the same placed textures and page.
    valid = valid && executeFrame();
    if (valid &&
        (graph.transientMemoryStats().pageCount != 1u || resolvedFirst.index != firstFrameFirst.index ||
         resolvedFirst.generation != firstFrameFirst.generation || resolvedSecond.index != firstFrameSecond.index ||
         resolvedSecond.generation != firstFrameSecond.generation)) {
        std::cerr << "Alias page reuse across frames failed\n";
        valid = false;
    }

    graph.releaseTransientResources(device);
    device.destroyBuffer(readback);
    device.destroyBuffer(uploadB);
    device.destroyBuffer(uploadA);
    device.waitIdle();
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateResourceDescriptionNameOwnership(VkRhiDevice& device) {
    constexpr const char* kBufferDebugName = "VulkanSmoke.DescriptionQuery.Buffer";
    std::array<char, 64u> bufferDebugName{};
    std::strcpy(bufferDebugName.data(), kBufferDebugName);
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = bufferDebugName.data();
    bufferDesc.size = 256u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Storage);
    bufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    bufferDesc.initialState = RhiResourceState::StorageBuffer;
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0u);

    constexpr const char* kTextureDebugName = "VulkanSmoke.DescriptionQuery.Texture";
    std::array<char, 64u> textureDebugName{};
    std::strcpy(textureDebugName.data(), kTextureDebugName);
    RhiTextureDesc textureDesc;
    textureDesc.debugName = textureDebugName.data();
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 8u;
    textureDesc.height = 8u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);

    bufferDebugName.fill('B');
    bufferDebugName.back() = '\0';
    textureDebugName.fill('T');
    textureDebugName.back() = '\0';

    RhiBufferDesc queriedBuffer;
    RhiTextureDesc queriedTexture;
    const bool valid = buffer.isValid() && texture.isValid() && device.getBufferDesc(buffer, queriedBuffer) &&
                       queriedBuffer.debugName != nullptr &&
                       std::strcmp(queriedBuffer.debugName, kBufferDebugName) == 0 &&
                       device.getTextureDesc(texture, queriedTexture) && queriedTexture.debugName != nullptr &&
                       std::strcmp(queriedTexture.debugName, kTextureDebugName) == 0;
    device.destroyBuffer(buffer);
    device.destroyTexture(texture);
    return valid;
}

[[nodiscard]] bool validateTextureAliasing(VkRhiDevice& device) {
    if (!device.capabilities().textureAliasing) {
        std::cerr << "Vulkan device must report textureAliasing\n";
        return false;
    }
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiTextureDesc colorDesc;
    colorDesc.debugName = "VulkanSmoke.Aliasing.Color";
    colorDesc.format = RhiTextureFormat::Rgba16Float;
    colorDesc.width = 256u;
    colorDesc.height = 256u;
    colorDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::Sampled);
    colorDesc.memoryCategory = RhiMemoryCategory::Transient;
    RhiTextureDesc storageDesc;
    storageDesc.debugName = "VulkanSmoke.Aliasing.Storage";
    storageDesc.format = RhiTextureFormat::R32Float;
    storageDesc.width = 128u;
    storageDesc.height = 128u;
    storageDesc.usage = rhiFlag(RhiTextureUsage::Storage) | rhiFlag(RhiTextureUsage::Sampled);
    storageDesc.memoryCategory = RhiMemoryCategory::Transient;

    RhiTextureMemoryRequirements colorRequirements;
    RhiTextureMemoryRequirements storageRequirements;
    if (!device.getTextureMemoryRequirements(colorDesc, colorRequirements) ||
        !device.getTextureMemoryRequirements(storageDesc, storageRequirements)) {
        std::cerr << "Texture memory requirements query failed\n";
        return false;
    }
    RhiTextureMemoryRequirements blockRequirements;
    blockRequirements.sizeBytes = std::max(colorRequirements.sizeBytes, storageRequirements.sizeBytes);
    blockRequirements.alignment = std::max(colorRequirements.alignment, storageRequirements.alignment);
    blockRequirements.memoryTypeBits = colorRequirements.memoryTypeBits & storageRequirements.memoryTypeBits;
    if (blockRequirements.memoryTypeBits == 0u) {
        // No shared memory type on this device; aliasing simply won't group
        // these formats, which is a valid outcome.
        return true;
    }

    const RhiMemoryStats memoryStatsBefore = device.memoryStats();
    const RhiMemoryCategoryStats transientBefore =
        memoryStatsBefore.categories[static_cast<size_t>(RhiMemoryCategory::Transient)];
    const RhiMemoryHandle memory =
        device.allocateTextureMemory(blockRequirements, RhiMemoryCategory::Transient, "VulkanSmoke.Aliasing.Block");
    if (!memory.isValid()) {
        std::cerr << "Texture memory allocation failed\n";
        return false;
    }
    const RhiTextureHandle colorTexture = device.createPlacedTexture(colorDesc, memory);
    const RhiTextureHandle storageTexture = device.createPlacedTexture(storageDesc, memory);
    bool valid = colorTexture.isValid() && storageTexture.isValid();
    const RhiMemoryStats memoryStatsPlaced = device.memoryStats();
    const RhiMemoryCategoryStats transientPlaced =
        memoryStatsPlaced.categories[static_cast<size_t>(RhiMemoryCategory::Transient)];
    if (!memoryStatsPlaced.valid || memoryStatsPlaced.accuracy != RhiMemoryStatsAccuracy::Exact ||
        transientPlaced.allocationCount != transientBefore.allocationCount + 1u ||
        transientPlaced.resourceCount != transientBefore.resourceCount + 2u ||
        transientPlaced.bytes < transientBefore.bytes + blockRequirements.sizeBytes) {
        std::cerr << "Placed textures must count one exact shared allocation\n";
        valid = false;
    }

    // Views must be creatable on placed textures like any other texture.
    RhiTextureViewHandle colorView;
    RhiTextureViewHandle storageView;
    if (valid) {
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = colorTexture;
        colorView = device.createTextureView(viewDesc);
        viewDesc.texture = storageTexture;
        storageView = device.createTextureView(viewDesc);
        valid = colorView.isValid() && storageView.isValid();
    }

    // A description larger than the block must be rejected, not bound.
    RhiTextureDesc oversizedDesc = colorDesc;
    oversizedDesc.debugName = "VulkanSmoke.Aliasing.Oversized";
    oversizedDesc.width = 4096u;
    oversizedDesc.height = 4096u;
    if (device.createPlacedTexture(oversizedDesc, memory).isValid()) {
        std::cerr << "Oversized placed texture was not rejected\n";
        valid = false;
    }

    if (colorView.isValid())
        device.destroyTextureView(colorView);
    if (storageView.isValid())
        device.destroyTextureView(storageView);
    if (colorTexture.isValid())
        device.destroyTexture(colorTexture);
    if (storageTexture.isValid())
        device.destroyTexture(storageTexture);
    device.destroyTextureMemory(memory);
    device.waitIdle();
    const RhiMemoryCategoryStats transientAfter =
        device.memoryStats().categories[static_cast<size_t>(RhiMemoryCategory::Transient)];
    if (transientAfter.bytes != transientBefore.bytes ||
        transientAfter.allocationCount != transientBefore.allocationCount ||
        transientAfter.resourceCount != transientBefore.resourceCount) {
        std::cerr << "Released shared allocation remained in the live memory snapshot\n";
        valid = false;
    }
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateRg32UintAttachmentClear(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr std::array<uint32_t, 2> kClearValue{0x13579bdfu, 0x2468ace0u};
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.Rg32UintClear.Target";
    textureDesc.format = RhiTextureFormat::Rg32Uint;
    textureDesc.width = 1u;
    textureDesc.height = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.format = textureDesc.format;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.Rg32UintClear.Readback";
    readbackDesc.size = sizeof(kClearValue);
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);

    bool valid = texture.isValid() && view.isValid() && readback.isValid();
    RhiCommandList* commands = valid ? commandPool.acquire(RhiCommandListType::Graphics) : nullptr;
    valid = valid && commands != nullptr &&
            commands->begin({"VulkanSmoke.Rg32UintClear.Commands", RhiCommandListType::Graphics});
    if (valid) {
        commands->textureBarrier({texture, RhiResourceState::Undefined, RhiResourceState::RenderTarget});
        RhiColorAttachment attachment;
        attachment.view = view;
        attachment.loadOp = RhiLoadOp::Clear;
        attachment.storeOp = RhiStoreOp::Store;
        attachment.clearValueType = RhiColorClearValueType::Uint;
        attachment.clearColorUint[0] = kClearValue[0];
        attachment.clearColorUint[1] = kClearValue[1];
        RhiRenderingInfo renderingInfo;
        renderingInfo.debugName = "VulkanSmoke.Rg32UintClear.Rendering";
        renderingInfo.renderArea = {0, 0, 1u, 1u};
        renderingInfo.colorAttachments = &attachment;
        renderingInfo.colorAttachmentCount = 1u;
        commands->beginRendering(renderingInfo);
        commands->endRendering();
        commands->textureBarrier({texture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});
        RhiTextureBufferCopy copy;
        copy.srcTexture = texture;
        copy.dstBuffer = readback;
        copy.bytesPerRow = static_cast<uint32_t>(sizeof(kClearValue));
        copy.rowsPerImage = 1u;
        copy.width = 1u;
        copy.height = 1u;
        commands->copyTextureToBuffer(copy);
        commands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
        valid = commands->end();
    }

    RhiSubmissionToken token;
    if (valid) {
        RhiCommandList* submissions[] = {commands};
        valid = device.submit({"VulkanSmoke.Rg32UintClear.Submit", submissions, 1u}, &token) &&
                device.waitForSubmission(token);
    }
    if (valid) {
        const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(readback, 0u, sizeof(kClearValue)));
        valid = mapped != nullptr && mapped[0] == kClearValue[0] && mapped[1] == kClearValue[1];
        if (mapped != nullptr) {
            device.unmapBuffer(readback);
        }
    }

    if (readback.isValid())
        device.destroyBuffer(readback);
    if (view.isValid())
        device.destroyTextureView(view);
    if (texture.isValid())
        device.destroyTexture(texture);
    device.waitIdle();
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateGlobalBindlessGpuScene(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    using namespace renderer::contracts;
    using namespace renderer::core;

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    GlobalBindlessSet bindlessSet;
    GlobalBindlessSetConfig bindlessConfig;
    bindlessConfig.sampledTexture2DCapacity = 2u;
    bindlessConfig.sampledTextureCubeCapacity = 2u;
    bindlessConfig.samplerCapacity = 2u;
    bindlessConfig.storageBufferCapacity = 8u;
    if (bindlessSet.initialize(device, bindlessConfig) != GlobalBindlessSetError::None) {
        std::cerr << "Global Bindless Set failed to initialize\n";
        return false;
    }

    constexpr std::array<uint8_t, 4u> kTexture2DPixel{17u, 31u, 47u, 255u};
    RhiTextureDesc texture2DDesc;
    texture2DDesc.debugName = "VulkanSmoke.GlobalBindless.Texture2D";
    texture2DDesc.format = RhiTextureFormat::Rgba8Unorm;
    texture2DDesc.width = 1u;
    texture2DDesc.height = 1u;
    texture2DDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureInitialData texture2DInitialData;
    texture2DInitialData.pixels = kTexture2DPixel.data();
    texture2DInitialData.sizeBytes = kTexture2DPixel.size();
    texture2DInitialData.finalState = RhiResourceState::ShaderRead;
    const RhiTextureHandle texture2D = device.createTexture(texture2DDesc, &texture2DInitialData);
    RhiTextureViewDesc texture2DViewDesc;
    texture2DViewDesc.texture = texture2D;
    texture2DViewDesc.format = texture2DDesc.format;
    const RhiTextureViewHandle texture2DView = device.createTextureView(texture2DViewDesc);

    constexpr std::array<uint8_t, 24u> kTextureCubePixels{61u, 67u, 71u, 255u, 61u, 67u, 71u, 255u,
                                                          61u, 67u, 71u, 255u, 61u, 67u, 71u, 255u,
                                                          61u, 67u, 71u, 255u, 61u, 67u, 71u, 255u};
    RhiTextureDesc textureCubeDesc;
    textureCubeDesc.debugName = "VulkanSmoke.GlobalBindless.TextureCube";
    textureCubeDesc.dimension = RhiTextureDimension::Cube;
    textureCubeDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureCubeDesc.width = 1u;
    textureCubeDesc.height = 1u;
    textureCubeDesc.depthOrLayers = 6u;
    textureCubeDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureInitialData textureCubeInitialData;
    textureCubeInitialData.pixels = kTextureCubePixels.data();
    textureCubeInitialData.sizeBytes = kTextureCubePixels.size();
    textureCubeInitialData.layerCount = 6u;
    textureCubeInitialData.finalState = RhiResourceState::ShaderRead;
    const RhiTextureHandle textureCube = device.createTexture(textureCubeDesc, &textureCubeInitialData);
    RhiTextureViewDesc textureCubeViewDesc;
    textureCubeViewDesc.texture = textureCube;
    textureCubeViewDesc.viewType = RhiTextureViewType::Cube;
    textureCubeViewDesc.format = textureCubeDesc.format;
    textureCubeViewDesc.layerCount = 6u;
    const RhiTextureViewHandle textureCubeView = device.createTextureView(textureCubeViewDesc);
    const RhiSamplerHandle sampler = device.createSampler({});

    RhiBufferDesc outputDesc;
    outputDesc.debugName = "VulkanSmoke.GlobalBindless.Output";
    outputDesc.size = sizeof(uint32_t) * 8u;
    outputDesc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::MapRead);
    outputDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    outputDesc.initialState = RhiResourceState::StorageBuffer;
    outputDesc.memoryCategory = RhiMemoryCategory::Readback;
    const RhiBufferHandle output = device.createBuffer(outputDesc, nullptr, 0u);

    const auto invalidCubePublication = bindlessSet.publishTextureCube(texture2DView);
    const auto texture2DPublication = bindlessSet.publishTexture2D(texture2DView);
    const auto textureCubePublication = bindlessSet.publishTextureCube(textureCubeView);
    const auto samplerPublication = bindlessSet.publishSampler(sampler);
    const auto outputPublication = bindlessSet.publishStorageBuffer(output);

    GpuSceneBufferSet sceneBuffers;
    GpuSceneBufferSetConfig sceneConfig;
    sceneConfig.materialCapacity = 2u;
    sceneConfig.geometryCapacity = 2u;
    sceneConfig.instanceCapacity = 2u;
    bool valid = texture2D.isValid() && texture2DView.isValid() && textureCube.isValid() && textureCubeView.isValid() &&
                 sampler.isValid() && output.isValid() &&
                 invalidCubePublication.error == GlobalBindlessSetError::InvalidResource &&
                 texture2DPublication.succeeded() && textureCubePublication.succeeded() &&
                 samplerPublication.succeeded() && outputPublication.succeeded() &&
                 sceneBuffers.initialize(device, bindlessSet, sceneConfig) == GpuSceneBufferSetError::None;

    GpuMaterial material;
    material.baseColorFactor.x = 0.25f;
    GpuSceneGeometry geometry;
    geometry.vertexAddress.low = 0x12345678u;
    GpuSceneInstance recordedInstance;
    recordedInstance.identityAndVersion.x = 0x9abcdef0u;
    GpuSceneInstance latestInstance = recordedInstance;
    latestInstance.identityAndVersion.x = 0x0badc0deu;
    valid = valid && sceneBuffers.writeMaterial(0u, material) == GpuSceneBufferSetError::None &&
            sceneBuffers.writeGeometry(0u, geometry) == GpuSceneBufferSetError::None &&
            sceneBuffers.writeInstance(0u, recordedInstance) == GpuSceneBufferSetError::None &&
            sceneBuffers.writeInstance(2u, recordedInstance) == GpuSceneBufferSetError::CapacityExceeded;

    const std::optional<std::string> computeSource =
        renderer::rhi::loadShaderSource("tests/shaders/global_bindless_gpu_scene_test.comp");
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.GlobalBindless.Compute";
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = computeSource.has_value() ? computeSource->c_str() : nullptr;
    shaderDesc.sourceSize = computeSource.has_value() ? computeSource->size() : 0u;
    const RhiShaderHandle shader =
        valid && computeSource.has_value() ? device.createShader(shaderDesc) : RhiShaderHandle{};
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "VulkanSmoke.GlobalBindless.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(bindlessSet.layout());
    pipelineLayoutDesc.pushConstantBytes = sizeof(uint32_t) * 8u;
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    const RhiPipelineLayoutHandle pipelineLayout =
        shader.isValid() ? device.createPipelineLayout(pipelineLayoutDesc) : RhiPipelineLayoutHandle{};
    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VulkanSmoke.GlobalBindless.Pipeline";
    pipelineDesc.computeShader = shader;
    pipelineDesc.layout = pipelineLayout;
    const RhiPipelineHandle pipeline =
        pipelineLayout.isValid() ? device.createComputePipeline(pipelineDesc) : RhiPipelineHandle{};

    std::array<uint32_t, 8u> pushConstants{sceneBuffers.materialHandle().index, sceneBuffers.geometryHandle().index,
                                           sceneBuffers.instanceHandle().index, outputPublication.handle.index,
                                           texture2DPublication.handle.index,   textureCubePublication.handle.index,
                                           samplerPublication.handle.index,     0u};
    bool uploadAwaitingSubmission = false;
    uint64_t lastUseSequence = 0u;
    RhiCommandList* firstCommands = pipeline.isValid() ? commandPool.acquire(RhiCommandListType::Compute) : nullptr;
    const bool firstCommandsBegan =
        valid && shader.isValid() && pipelineLayout.isValid() && pipeline.isValid() && firstCommands != nullptr &&
        firstCommands->begin({"VulkanSmoke.GlobalBindless.FirstCommands", RhiCommandListType::Compute});
    valid = firstCommandsBegan;
    if (firstCommandsBegan) {
        const GpuSceneBufferSetError uploadError = sceneBuffers.recordUploads(*firstCommands);
        uploadAwaitingSubmission = uploadError == GpuSceneBufferSetError::None;
        const GpuSceneBufferSetError rewriteError = sceneBuffers.writeInstance(0u, latestInstance);
        firstCommands->setComputePipeline(pipeline);
        firstCommands->setBindGroup(0u, bindlessSet.bindGroup());
        firstCommands->pushConstants(pushConstants.data(), sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
        firstCommands->dispatch(1u, 1u, 1u);
        firstCommands->bufferBarrier({output, RhiResourceState::StorageBuffer, RhiResourceState::HostRead});
        const bool ended = firstCommands->end();
        valid = uploadError == GpuSceneBufferSetError::None && rewriteError == GpuSceneBufferSetError::None && ended;
    }

    RhiSubmissionToken firstToken;
    if (valid) {
        RhiCommandList* submissions[] = {firstCommands};
        const bool submitted = device.submit(
            {"VulkanSmoke.GlobalBindless.FirstSubmit", submissions, 1u, RhiQueueType::Compute}, &firstToken);
        if (submitted) {
            lastUseSequence = firstToken.sequence;
            const bool marked = sceneBuffers.markSubmitted(firstToken) == GpuSceneBufferSetError::None;
            if (marked) {
                uploadAwaitingSubmission = false;
            }
            const bool completed = device.waitForSubmission(firstToken);
            valid = marked && completed;
        } else {
            valid = false;
        }
    }

    uint32_t expectedMaterialWord = 0u;
    std::memcpy(&expectedMaterialWord, &material.baseColorFactor.x, sizeof(expectedMaterialWord));
    if (valid) {
        const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(output, 0u, outputDesc.size));
        valid = mapped != nullptr && mapped[0] == expectedMaterialWord && mapped[1] == geometry.vertexAddress.low &&
                mapped[2] == recordedInstance.identityAndVersion.x && mapped[3] == kTexture2DPixel[0] &&
                mapped[4] == kTextureCubePixels[0];
        if (mapped != nullptr) {
            device.unmapBuffer(output);
        }
    }

    const GpuSceneBufferSetStats firstUploadStats = sceneBuffers.stats();
    valid = valid && firstUploadStats.materials.dirtyRecordCount == 0u &&
            firstUploadStats.geometries.dirtyRecordCount == 0u && firstUploadStats.instances.dirtyRecordCount == 1u &&
            firstUploadStats.uploadedBytes == sizeof(GpuMaterial) + sizeof(GpuSceneGeometry) + sizeof(GpuSceneInstance);

    RhiCommandList* secondCommands = valid ? commandPool.acquire(RhiCommandListType::Compute) : nullptr;
    const bool secondCommandsBegan =
        valid && secondCommands != nullptr &&
        secondCommands->begin({"VulkanSmoke.GlobalBindless.SecondCommands", RhiCommandListType::Compute});
    valid = secondCommandsBegan;
    if (secondCommandsBegan) {
        const GpuSceneBufferSetError uploadError = sceneBuffers.recordUploads(*secondCommands);
        uploadAwaitingSubmission = uploadError == GpuSceneBufferSetError::None;
        secondCommands->bufferBarrier({output, RhiResourceState::HostRead, RhiResourceState::StorageBuffer});
        secondCommands->setComputePipeline(pipeline);
        secondCommands->setBindGroup(0u, bindlessSet.bindGroup());
        secondCommands->pushConstants(pushConstants.data(), sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
        secondCommands->dispatch(1u, 1u, 1u);
        secondCommands->bufferBarrier({output, RhiResourceState::StorageBuffer, RhiResourceState::HostRead});
        const bool ended = secondCommands->end();
        valid = uploadError == GpuSceneBufferSetError::None && ended;
    }

    RhiSubmissionToken secondToken;
    if (valid) {
        RhiCommandList* submissions[] = {secondCommands};
        const bool submitted = device.submit(
            {"VulkanSmoke.GlobalBindless.SecondSubmit", submissions, 1u, RhiQueueType::Compute}, &secondToken);
        if (submitted) {
            lastUseSequence = secondToken.sequence;
            const bool marked = sceneBuffers.markSubmitted(secondToken) == GpuSceneBufferSetError::None;
            if (marked) {
                uploadAwaitingSubmission = false;
            }
            const bool completed = device.waitForSubmission(secondToken);
            valid = marked && completed;
        } else {
            valid = false;
        }
    }

    if (valid) {
        const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(output, 0u, outputDesc.size));
        valid = mapped != nullptr && mapped[2] == latestInstance.identityAndVersion.x;
        if (mapped != nullptr) {
            device.unmapBuffer(output);
        }
    }

    const GpuSceneBufferSetStats uploadedStats = sceneBuffers.stats();
    const GlobalBindlessSetStats liveStats = bindlessSet.stats();
    valid =
        valid && uploadedStats.materials.dirtyRecordCount == 0u && uploadedStats.geometries.dirtyRecordCount == 0u &&
        uploadedStats.instances.dirtyRecordCount == 0u &&
        uploadedStats.uploadedBytes == sizeof(GpuMaterial) + sizeof(GpuSceneGeometry) + sizeof(GpuSceneInstance) * 2u &&
        liveStats.sampledTexture2D.liveCount == 1u && liveStats.sampledTextureCube.liveCount == 1u &&
        liveStats.samplers.liveCount == 1u && liveStats.storageBuffers.liveCount == 4u;

    if (valid) {
        const std::array<BindlessStorageBufferHandle, 2u> duplicateHandles{sceneBuffers.materialHandle(),
                                                                           sceneBuffers.materialHandle()};
        const BindlessDescriptorSlotStats storageStatsBefore = bindlessSet.stats().storageBuffers;
        const GlobalBindlessSetError duplicateRetirement = bindlessSet.retireStorageBuffers(
            duplicateHandles.data(), static_cast<uint32_t>(duplicateHandles.size()), lastUseSequence);
        const BindlessDescriptorSlotStats storageStatsAfter = bindlessSet.stats().storageBuffers;
        valid = duplicateRetirement == GlobalBindlessSetError::InvalidHandle &&
                storageStatsAfter.capacity == storageStatsBefore.capacity &&
                storageStatsAfter.liveCount == storageStatsBefore.liveCount &&
                storageStatsAfter.retiredCount == storageStatsBefore.retiredCount &&
                storageStatsAfter.availableCount == storageStatsBefore.availableCount &&
                storageStatsAfter.exhaustedCount == storageStatsBefore.exhaustedCount &&
                storageStatsAfter.peakLiveCount == storageStatsBefore.peakLiveCount;
    }

    device.waitIdle();
    if (uploadAwaitingSubmission && sceneBuffers.initialized()) {
        const bool discarded = sceneBuffers.discardRecordedUploads() == GpuSceneBufferSetError::None;
        uploadAwaitingSubmission = false;
        valid = discarded && valid;
    }
    if (sceneBuffers.initialized()) {
        const bool sceneShutdown = sceneBuffers.shutdown() == GpuSceneBufferSetError::None;
        valid = sceneShutdown && valid;
    }

    bool descriptorRetirementValid = true;
    if (texture2DPublication.handle.isValid()) {
        descriptorRetirementValid &=
            bindlessSet.retire(texture2DPublication.handle, lastUseSequence) == GlobalBindlessSetError::None;
    }
    if (textureCubePublication.handle.isValid()) {
        descriptorRetirementValid &=
            bindlessSet.retire(textureCubePublication.handle, lastUseSequence) == GlobalBindlessSetError::None;
    }
    if (samplerPublication.handle.isValid()) {
        descriptorRetirementValid &=
            bindlessSet.retire(samplerPublication.handle, lastUseSequence) == GlobalBindlessSetError::None;
    }
    if (outputPublication.handle.isValid()) {
        descriptorRetirementValid &=
            bindlessSet.retire(outputPublication.handle, lastUseSequence) == GlobalBindlessSetError::None;
    }
    valid = descriptorRetirementValid && valid;

    if (lastUseSequence != 0u) {
        const GlobalBindlessReclaimResult earlyReclaim = bindlessSet.reclaim(lastUseSequence - 1u);
        const GlobalBindlessReclaimResult completedReclaim = bindlessSet.reclaim(lastUseSequence);
        valid = valid && earlyReclaim.sampledTexture2D.reclaimedCount == 0u &&
                earlyReclaim.sampledTextureCube.reclaimedCount == 0u && earlyReclaim.samplers.reclaimedCount == 0u &&
                earlyReclaim.storageBuffers.reclaimedCount == 0u &&
                completedReclaim.sampledTexture2D.reclaimedCount == 1u &&
                completedReclaim.sampledTextureCube.reclaimedCount == 1u &&
                completedReclaim.samplers.reclaimedCount == 1u && completedReclaim.storageBuffers.reclaimedCount == 4u;
    } else {
        (void)bindlessSet.reclaim(0u);
    }

    if (output.isValid())
        device.destroyBuffer(output);
    if (sampler.isValid())
        device.destroySampler(sampler);
    if (textureCubeView.isValid())
        device.destroyTextureView(textureCubeView);
    if (textureCube.isValid())
        device.destroyTexture(textureCube);
    if (texture2DView.isValid())
        device.destroyTextureView(texture2DView);
    if (texture2D.isValid())
        device.destroyTexture(texture2D);

    if (valid) {
        const RhiSamplerHandle replacementSampler = device.createSampler({});
        const auto replacementPublication = bindlessSet.publishSampler(replacementSampler);
        bool replacementValid =
            replacementSampler.isValid() && replacementPublication.succeeded() &&
            replacementPublication.handle ==
                BindlessSamplerHandle{samplerPublication.handle.index, samplerPublication.handle.generation + 1u};
        if (replacementPublication.handle.isValid()) {
            replacementValid &=
                bindlessSet.retire(replacementPublication.handle, lastUseSequence) == GlobalBindlessSetError::None;
            (void)bindlessSet.reclaim(lastUseSequence);
        }
        valid = replacementValid && valid;
        if (replacementSampler.isValid())
            device.destroySampler(replacementSampler);
    }

    if (pipeline.isValid())
        device.destroyPipeline(pipeline);
    if (pipelineLayout.isValid())
        device.destroyPipelineLayout(pipelineLayout);
    if (shader.isValid())
        device.destroyShader(shader);
    bindlessSet.shutdown();
    if (!valid) {
        std::cerr << "Global Bindless GPU Scene upload or lifecycle validation failed\n";
    }
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

} // namespace

[[nodiscard]] bool validateAccelerationStructures(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    using namespace renderer::core;

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    const RhiCapabilities& capabilities = device.capabilities();
    if (!capabilities.accelerationStructure || !capabilities.rayQuery || !capabilities.bufferDeviceAddress ||
        !capabilities.descriptorBindingAccelerationStructureUpdateAfterBind ||
        capabilities.maxAccelerationStructureGeometryCount == 0u ||
        capabilities.maxAccelerationStructureInstanceCount == 0u ||
        capabilities.maxAccelerationStructurePrimitiveCount == 0u ||
        capabilities.minAccelerationStructureScratchOffsetAlignment == 0u) {
        std::cerr << "Acceleration-structure capabilities are incomplete\n";
        return false;
    }

    std::array<RhiBufferHandle, 8u> scratchAlignmentBuffers{};
    RhiBufferDesc scratchAlignmentDesc;
    scratchAlignmentDesc.debugName = "VulkanSmoke.AS.ScratchAlignment";
    scratchAlignmentDesc.size = 257u;
    scratchAlignmentDesc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
    scratchAlignmentDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    scratchAlignmentDesc.initialState = RhiResourceState::AccelerationStructureBuildScratch;
    scratchAlignmentDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    bool scratchAlignmentValid = true;
    for (RhiBufferHandle& buffer : scratchAlignmentBuffers) {
        buffer = device.createBuffer(scratchAlignmentDesc, nullptr, 0u);
        const uint64_t address = buffer.isValid() ? device.bufferDeviceAddress(buffer) : 0u;
        scratchAlignmentValid = scratchAlignmentValid && address != 0u &&
                                address % capabilities.minAccelerationStructureScratchOffsetAlignment == 0u;
    }
    for (const RhiBufferHandle buffer : scratchAlignmentBuffers) {
        if (buffer.isValid()) {
            device.destroyBuffer(buffer);
        }
    }
    if (!scratchAlignmentValid) {
        std::cerr << "Acceleration-structure scratch allocation alignment validation failed\n";
        return false;
    }

    RhiBufferHandle vertexBuffer;
    RhiBufferHandle indexBuffer;
    RhiBufferHandle blasStorage;
    RhiBufferHandle blasScratch;
    RhiBufferHandle cloneStorage;
    RhiBufferHandle compactStorage;
    RhiBufferHandle instanceBuffer;
    RhiBufferHandle tlasStorage;
    RhiBufferHandle tlasScratch;
    RhiAccelerationStructureHandle blas;
    RhiAccelerationStructureHandle cloneBlas;
    RhiAccelerationStructureHandle compactBlas;
    RhiAccelerationStructureHandle tlas;
    RhiQueryPoolHandle compactedSizeQueries;
    RhiBindGroupLayoutHandle accelerationStructureDescriptorLayout;
    RhiBindGroupHandle accelerationStructureDescriptorGroup;
    GlobalBindlessSet accelerationStructureBindlessSet;

    const auto cleanup = [&]() {
        accelerationStructureBindlessSet.shutdown();
        if (accelerationStructureDescriptorGroup.isValid())
            device.destroyBindGroup(accelerationStructureDescriptorGroup);
        if (accelerationStructureDescriptorLayout.isValid())
            device.destroyBindGroupLayout(accelerationStructureDescriptorLayout);
        if (compactedSizeQueries.isValid())
            device.destroyQueryPool(compactedSizeQueries);
        if (tlas.isValid())
            device.destroyAccelerationStructure(tlas);
        if (compactBlas.isValid())
            device.destroyAccelerationStructure(compactBlas);
        if (cloneBlas.isValid())
            device.destroyAccelerationStructure(cloneBlas);
        if (blas.isValid())
            device.destroyAccelerationStructure(blas);
        if (tlasScratch.isValid())
            device.destroyBuffer(tlasScratch);
        if (tlasStorage.isValid())
            device.destroyBuffer(tlasStorage);
        if (instanceBuffer.isValid())
            device.destroyBuffer(instanceBuffer);
        if (compactStorage.isValid())
            device.destroyBuffer(compactStorage);
        if (cloneStorage.isValid())
            device.destroyBuffer(cloneStorage);
        if (blasScratch.isValid())
            device.destroyBuffer(blasScratch);
        if (blasStorage.isValid())
            device.destroyBuffer(blasStorage);
        if (indexBuffer.isValid())
            device.destroyBuffer(indexBuffer);
        if (vertexBuffer.isValid())
            device.destroyBuffer(vertexBuffer);
        device.waitIdle();
    };

    const auto submitComputeAndWait = [&](const char* debugName, auto&& recorder) {
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Compute);
        if (commands == nullptr || !commands->begin({debugName, RhiCommandListType::Compute}) || !recorder(*commands) ||
            !commands->end()) {
            return false;
        }
        RhiCommandList* submissions[] = {commands};
        RhiSubmissionToken token;
        return device.submit({debugName, submissions, 1u, RhiQueueType::Compute}, &token) &&
               device.waitForSubmission(token);
    };

    const auto rejectsBuildBatch = [&](const char* debugName, const RhiAccelerationStructureBuildDesc* builds,
                                       const uint32_t buildCount) {
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Compute);
        if (commands == nullptr || !commands->begin({debugName, RhiCommandListType::Compute})) {
            return false;
        }
        const bool rejected = !commands->buildAccelerationStructures(builds, buildCount);
        return commands->end() && rejected;
    };

    constexpr std::array<float, 18u> kTriangleVertices{-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                                       -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f};
    constexpr std::array<uint32_t, 3u> kTriangleIndices{0u, 1u, 2u};
    constexpr RhiBufferUsageFlags kBuildInputUsages = rhiFlag(RhiBufferUsage::TransferDst) |
                                                      rhiFlag(RhiBufferUsage::DeviceAddress) |
                                                      rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
    RhiBufferDesc vertexDesc;
    vertexDesc.debugName = "VulkanSmoke.AS.Vertices";
    vertexDesc.size = sizeof(kTriangleVertices);
    vertexDesc.usage = kBuildInputUsages;
    vertexDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    vertexDesc.initialState = RhiResourceState::AccelerationStructureBuildInput;
    vertexDesc.memoryCategory = RhiMemoryCategory::Geometry;
    vertexBuffer = device.createBuffer(vertexDesc, kTriangleVertices.data(), sizeof(kTriangleVertices));
    RhiBufferDesc indexDesc = vertexDesc;
    indexDesc.debugName = "VulkanSmoke.AS.Indices";
    indexDesc.size = sizeof(kTriangleIndices);
    indexBuffer = device.createBuffer(indexDesc, kTriangleIndices.data(), sizeof(kTriangleIndices));

    RhiAccelerationStructureGeometryDesc triangleGeometry;
    triangleGeometry.type = RhiAccelerationStructureGeometryType::Triangles;
    triangleGeometry.flags = rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque);
    triangleGeometry.triangles.vertexBuffer = vertexBuffer;
    triangleGeometry.triangles.vertexStride = sizeof(float) * 3u;
    triangleGeometry.triangles.vertexCount = 6u;
    triangleGeometry.triangles.vertexFormat = RhiVertexFormat::Float3;
    triangleGeometry.triangles.indexBuffer = indexBuffer;
    triangleGeometry.triangles.indexFormat = RhiAccelerationStructureIndexFormat::Uint32;
    RhiAccelerationStructureBuildRangeDesc triangleRange;
    triangleRange.primitiveCount = 1u;
    RhiAccelerationStructureBuildInput blasInput;
    blasInput.type = RhiAccelerationStructureType::BottomLevel;
    blasInput.flags = rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate) |
                      rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction) |
                      rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);
    blasInput.geometries = &triangleGeometry;
    blasInput.ranges = &triangleRange;
    blasInput.geometryCount = 1u;
    RhiAccelerationStructureBuildSizes blasSizes;
    bool valid = vertexBuffer.isValid() && indexBuffer.isValid() && device.bufferDeviceAddress(vertexBuffer) != 0u &&
                 device.bufferDeviceAddress(indexBuffer) != 0u &&
                 device.queryAccelerationStructureBuildSizes(blasInput, blasSizes) &&
                 blasSizes.accelerationStructureSize != 0u && blasSizes.buildScratchSize != 0u &&
                 blasSizes.updateScratchSize != 0u;

    if (valid) {
        RhiAccelerationStructureGeometryDesc nonIndexedGeometry = triangleGeometry;
        nonIndexedGeometry.triangles.indexBuffer = {};
        nonIndexedGeometry.triangles.indexOffset = 0u;
        nonIndexedGeometry.triangles.indexFormat = RhiAccelerationStructureIndexFormat::None;
        RhiAccelerationStructureBuildRangeDesc nonIndexedRange = triangleRange;
        nonIndexedRange.firstVertex = 3u;
        RhiAccelerationStructureBuildInput nonIndexedInput = blasInput;
        nonIndexedInput.geometries = &nonIndexedGeometry;
        nonIndexedInput.ranges = &nonIndexedRange;
        RhiAccelerationStructureBuildSizes validationSizes;
        valid = device.queryAccelerationStructureBuildSizes(nonIndexedInput, validationSizes);
        nonIndexedRange.primitiveOffset = 2u;
        valid = valid && !device.queryAccelerationStructureBuildSizes(nonIndexedInput, validationSizes);
        nonIndexedRange.primitiveOffset = 0u;
        nonIndexedRange.firstVertex = 4u;
        valid = valid && !device.queryAccelerationStructureBuildSizes(nonIndexedInput, validationSizes);
        nonIndexedRange.firstVertex = 3u;
        nonIndexedGeometry.triangles.vertexStride = sizeof(float) * 3u + 2u;
        valid = valid && !device.queryAccelerationStructureBuildSizes(nonIndexedInput, validationSizes);
        nonIndexedGeometry.triangles.vertexStride = sizeof(float) * 3u;
        nonIndexedGeometry.triangles.transformBuffer = vertexBuffer;
        nonIndexedRange.transformOffset = 4u;
        valid = valid && !device.queryAccelerationStructureBuildSizes(nonIndexedInput, validationSizes);
    }

    constexpr RhiBufferUsageFlags kAccelerationStructureStorageUsages =
        rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureStorage);
    if (valid) {
        RhiBufferDesc storageDesc;
        storageDesc.debugName = "VulkanSmoke.AS.BLAS.Storage";
        storageDesc.size = blasSizes.accelerationStructureSize;
        storageDesc.usage = kAccelerationStructureStorageUsages;
        storageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        storageDesc.initialState = RhiResourceState::AccelerationStructureBuildWrite;
        storageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        blasStorage = device.createBuffer(storageDesc, nullptr, 0u);
        RhiBufferDesc scratchDesc;
        scratchDesc.debugName = "VulkanSmoke.AS.BLAS.Scratch";
        scratchDesc.size = std::max(blasSizes.buildScratchSize, blasSizes.updateScratchSize);
        scratchDesc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
        scratchDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        scratchDesc.initialState = RhiResourceState::AccelerationStructureBuildScratch;
        scratchDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        blasScratch = device.createBuffer(scratchDesc, nullptr, 0u);
        blas = device.createAccelerationStructure({"VulkanSmoke.AS.BLAS", RhiAccelerationStructureType::BottomLevel,
                                                   blasStorage, 0u, blasSizes.accelerationStructureSize});
        compactedSizeQueries = device.createQueryPool(
            {"VulkanSmoke.AS.CompactedSize", RhiQueryType::AccelerationStructureCompactedSize, 1u});
        RhiAccelerationStructureDesc storedDesc;
        valid = blasStorage.isValid() && blasScratch.isValid() && blas.isValid() && compactedSizeQueries.isValid() &&
                device.getAccelerationStructureDesc(blas, storedDesc) &&
                storedDesc.type == RhiAccelerationStructureType::BottomLevel &&
                storedDesc.buffer.index == blasStorage.index &&
                storedDesc.buffer.generation == blasStorage.generation &&
                std::strcmp(storedDesc.debugName, "VulkanSmoke.AS.BLAS") == 0;
    }

    uint64_t compactedSize = 0u;
    if (valid) {
        const RhiAccelerationStructureBuildDesc build{
            blasInput, RhiAccelerationStructureBuildMode::Build, {}, blas, blasScratch, 0u};
        valid = submitComputeAndWait("VulkanSmoke.AS.Build", [&](RhiCommandList& commands) {
            commands.resetQueryPool(compactedSizeQueries, 0u, 1u);
            return commands.buildAccelerationStructures(&build, 1u) &&
                   commands.accelerationStructureBarrier({blas, RhiResourceState::AccelerationStructureBuildWrite,
                                                          RhiResourceState::AccelerationStructureRead}) &&
                   commands.writeAccelerationStructureProperties({&blas, 1u, compactedSizeQueries, 0u});
        });
    }
    if (valid) {
        valid = device.areQueryResultsAvailable(compactedSizeQueries, 0u, 1u) &&
                device.getQueryResults(compactedSizeQueries, 0u, 1u, &compactedSize) && compactedSize != 0u &&
                compactedSize <= blasSizes.accelerationStructureSize &&
                device.accelerationStructureDeviceAddress(blas) != 0u;
    }

    if (valid) {
        const RhiAccelerationStructureBuildDesc update{
            blasInput, RhiAccelerationStructureBuildMode::Update, blas, blas, blasScratch, 0u};
        valid = submitComputeAndWait("VulkanSmoke.AS.Update", [&](RhiCommandList& commands) {
            commands.resetQueryPool(compactedSizeQueries, 0u, 1u);
            return commands.accelerationStructureBarrier({blas, RhiResourceState::AccelerationStructureRead,
                                                          RhiResourceState::AccelerationStructureBuildWrite}) &&
                   commands.buildAccelerationStructures(&update, 1u) &&
                   commands.accelerationStructureBarrier({blas, RhiResourceState::AccelerationStructureBuildWrite,
                                                          RhiResourceState::AccelerationStructureRead}) &&
                   commands.writeAccelerationStructureProperties({&blas, 1u, compactedSizeQueries, 0u});
        });
    }
    if (valid) {
        valid = device.getQueryResults(compactedSizeQueries, 0u, 1u, &compactedSize) && compactedSize != 0u &&
                compactedSize <= blasSizes.accelerationStructureSize;
    }

    if (valid) {
        RhiBufferDesc cloneStorageDesc;
        cloneStorageDesc.debugName = "VulkanSmoke.AS.Clone.Storage";
        cloneStorageDesc.size = blasSizes.accelerationStructureSize;
        cloneStorageDesc.usage = kAccelerationStructureStorageUsages;
        cloneStorageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        cloneStorageDesc.initialState = RhiResourceState::AccelerationStructureBuildWrite;
        cloneStorageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        cloneStorage = device.createBuffer(cloneStorageDesc, nullptr, 0u);
        RhiBufferDesc compactStorageDesc = cloneStorageDesc;
        compactStorageDesc.debugName = "VulkanSmoke.AS.Compact.Storage";
        compactStorageDesc.size = compactedSize;
        compactStorage = device.createBuffer(compactStorageDesc, nullptr, 0u);
        cloneBlas =
            device.createAccelerationStructure({"VulkanSmoke.AS.Clone", RhiAccelerationStructureType::BottomLevel,
                                                cloneStorage, 0u, blasSizes.accelerationStructureSize});
        compactBlas = device.createAccelerationStructure(
            {"VulkanSmoke.AS.Compact", RhiAccelerationStructureType::BottomLevel, compactStorage, 0u, compactedSize});
        valid = cloneStorage.isValid() && compactStorage.isValid() && cloneBlas.isValid() && compactBlas.isValid();
    }
    if (valid) {
        const std::array<RhiAccelerationStructureBuildDesc, 2u> overlappingScratchBuilds{
            RhiAccelerationStructureBuildDesc{
                blasInput, RhiAccelerationStructureBuildMode::Build, {}, blas, blasScratch, 0u},
            RhiAccelerationStructureBuildDesc{
                blasInput, RhiAccelerationStructureBuildMode::Build, {}, cloneBlas, blasScratch, 0u}};
        const std::array<RhiAccelerationStructureBuildDesc, 2u> crossingSourceDestinationBuilds{
            RhiAccelerationStructureBuildDesc{
                blasInput, RhiAccelerationStructureBuildMode::Build, {}, cloneBlas, blasScratch, 0u},
            RhiAccelerationStructureBuildDesc{blasInput, RhiAccelerationStructureBuildMode::Update, cloneBlas, blas,
                                              blasScratch, 0u}};
        valid =
            rejectsBuildBatch("VulkanSmoke.AS.RejectScratchOverlap", overlappingScratchBuilds.data(),
                              static_cast<uint32_t>(overlappingScratchBuilds.size())) &&
            rejectsBuildBatch("VulkanSmoke.AS.RejectSourceDestinationCrossing", crossingSourceDestinationBuilds.data(),
                              static_cast<uint32_t>(crossingSourceDestinationBuilds.size()));
    }
    if (valid) {
        valid = submitComputeAndWait("VulkanSmoke.AS.Copy", [&](RhiCommandList& commands) {
            return commands.copyAccelerationStructure({blas, cloneBlas, RhiAccelerationStructureCopyMode::Clone}) &&
                   commands.accelerationStructureBarrier({cloneBlas, RhiResourceState::AccelerationStructureBuildWrite,
                                                          RhiResourceState::AccelerationStructureRead}) &&
                   commands.copyAccelerationStructure({blas, compactBlas, RhiAccelerationStructureCopyMode::Compact}) &&
                   commands.accelerationStructureBarrier({compactBlas,
                                                          RhiResourceState::AccelerationStructureBuildWrite,
                                                          RhiResourceState::AccelerationStructureRead});
        });
    }

    uint64_t compactBlasAddress = 0u;
    if (valid) {
        compactBlasAddress = device.accelerationStructureDeviceAddress(compactBlas);
        valid = device.accelerationStructureDeviceAddress(cloneBlas) != 0u && compactBlasAddress != 0u;
    }

    RhiAccelerationStructureBuildSizes tlasSizes;
    RhiAccelerationStructureGeometryDesc instanceGeometry;
    RhiAccelerationStructureBuildRangeDesc instanceRange;
    RhiAccelerationStructureBuildInput tlasInput;
    if (valid) {
        RhiAccelerationStructureInstance instance;
        const auto customIndex = rhiPackAccelerationStructureInstanceCustomIndexAndMask(23u, 0xffu);
        const auto instanceFlags = rhiPackAccelerationStructureInstanceShaderBindingTableOffsetAndFlags(
            0u, rhiFlag(RhiAccelerationStructureInstanceFlag::TriangleFacingCullDisable));
        valid = customIndex.has_value() && instanceFlags.has_value();
        if (valid) {
            instance.customIndexAndMask = *customIndex;
            instance.shaderBindingTableOffsetAndFlags = *instanceFlags;
            instance.accelerationStructureReference = compactBlasAddress;
            RhiBufferDesc instanceDesc;
            instanceDesc.debugName = "VulkanSmoke.AS.TLAS.Instances";
            instanceDesc.size = sizeof(instance);
            instanceDesc.usage = kBuildInputUsages;
            instanceDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
            instanceDesc.initialState = RhiResourceState::AccelerationStructureBuildInput;
            instanceDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
            instanceBuffer = device.createBuffer(instanceDesc, &instance, sizeof(instance));
        }
        instanceGeometry.type = RhiAccelerationStructureGeometryType::Instances;
        instanceGeometry.instances.buffer = instanceBuffer;
        instanceRange.primitiveCount = 1u;
        tlasInput.type = RhiAccelerationStructureType::TopLevel;
        tlasInput.flags = rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);
        tlasInput.geometries = &instanceGeometry;
        tlasInput.ranges = &instanceRange;
        tlasInput.geometryCount = 1u;
        RhiAccelerationStructureGeometryDesc pointerGeometry = instanceGeometry;
        pointerGeometry.instances.arrayOfPointers = true;
        RhiAccelerationStructureBuildRangeDesc misalignedPointerRange = instanceRange;
        misalignedPointerRange.primitiveOffset = 8u;
        RhiAccelerationStructureBuildInput misalignedPointerInput = tlasInput;
        misalignedPointerInput.geometries = &pointerGeometry;
        misalignedPointerInput.ranges = &misalignedPointerRange;
        RhiAccelerationStructureBuildSizes validationSizes;
        valid = valid && instanceBuffer.isValid() &&
                !device.queryAccelerationStructureBuildSizes(misalignedPointerInput, validationSizes) &&
                device.queryAccelerationStructureBuildSizes(tlasInput, tlasSizes);
    }

    if (valid) {
        RhiBufferDesc tlasStorageDesc;
        tlasStorageDesc.debugName = "VulkanSmoke.AS.TLAS.Storage";
        tlasStorageDesc.size = tlasSizes.accelerationStructureSize;
        tlasStorageDesc.usage = kAccelerationStructureStorageUsages;
        tlasStorageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        tlasStorageDesc.initialState = RhiResourceState::AccelerationStructureBuildWrite;
        tlasStorageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        tlasStorage = device.createBuffer(tlasStorageDesc, nullptr, 0u);
        RhiBufferDesc tlasScratchDesc;
        tlasScratchDesc.debugName = "VulkanSmoke.AS.TLAS.Scratch";
        tlasScratchDesc.size = tlasSizes.buildScratchSize;
        tlasScratchDesc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
        tlasScratchDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        tlasScratchDesc.initialState = RhiResourceState::AccelerationStructureBuildScratch;
        tlasScratchDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        tlasScratch = device.createBuffer(tlasScratchDesc, nullptr, 0u);
        tlas = device.createAccelerationStructure({"VulkanSmoke.AS.TLAS", RhiAccelerationStructureType::TopLevel,
                                                   tlasStorage, 0u, tlasSizes.accelerationStructureSize});
        valid = tlasStorage.isValid() && tlasScratch.isValid() && tlas.isValid();
    }
    if (valid) {
        const RhiAccelerationStructureBuildDesc build{
            tlasInput, RhiAccelerationStructureBuildMode::Build, {}, tlas, tlasScratch, 0u};
        valid = submitComputeAndWait("VulkanSmoke.AS.TLAS.Build", [&](RhiCommandList& commands) {
            return commands.buildAccelerationStructures(&build, 1u) &&
                   commands.accelerationStructureBarrier({tlas, RhiResourceState::AccelerationStructureBuildWrite,
                                                          RhiResourceState::AccelerationStructureRead});
        });
    }
    if (valid) {
        valid = device.accelerationStructureDeviceAddress(tlas) != 0u;
    }

    if (valid) {
        RhiBindGroupLayoutDesc descriptorLayoutDesc;
        descriptorLayoutDesc.debugName = "VulkanSmoke.AS.DescriptorLayout";
        descriptorLayoutDesc.entries = {{0u, RhiBindingType::AccelerationStructure, rhiFlag(RhiShaderStage::Compute),
                                         1u, rhiFlag(RhiBindingFlag::PartiallyBound)}};
        accelerationStructureDescriptorLayout = device.createBindGroupLayout(descriptorLayoutDesc);
        RhiBindingResource blasResource;
        blasResource.accelerationStructure = compactBlas;
        RhiBindGroupDesc invalidBlasDescriptor;
        invalidBlasDescriptor.layout = accelerationStructureDescriptorLayout;
        invalidBlasDescriptor.entries = {{0u, 0u, blasResource}};
        const RhiBindGroupHandle rejectedBlasDescriptor = device.createBindGroup(invalidBlasDescriptor);
        RhiBindGroupDesc descriptorGroupDesc;
        descriptorGroupDesc.layout = accelerationStructureDescriptorLayout;
        accelerationStructureDescriptorGroup = device.createBindGroup(descriptorGroupDesc);
        RhiBindGroupUpdate descriptorUpdate;
        descriptorUpdate.bindGroup = accelerationStructureDescriptorGroup;
        descriptorUpdate.binding = 0u;
        descriptorUpdate.resources = &blasResource;
        descriptorUpdate.resourceCount = 1u;
        const bool rejectedBlasUpdate = !device.updateBindGroups(&descriptorUpdate, 1u);
        RhiBindingResource tlasResource;
        tlasResource.accelerationStructure = tlas;
        descriptorUpdate.resources = &tlasResource;
        valid = accelerationStructureDescriptorLayout.isValid() && !rejectedBlasDescriptor.isValid() &&
                accelerationStructureDescriptorGroup.isValid() && rejectedBlasUpdate &&
                device.updateBindGroups(&descriptorUpdate, 1u);
    }

    if (valid) {
        GlobalBindlessSetConfig bindlessConfig;
        bindlessConfig.sampledTexture2DCapacity = 1u;
        bindlessConfig.sampledTextureCubeCapacity = 1u;
        bindlessConfig.samplerCapacity = 1u;
        bindlessConfig.storageBufferCapacity = 1u;
        valid = accelerationStructureBindlessSet.initialize(device, bindlessConfig) == GlobalBindlessSetError::None &&
                accelerationStructureBindlessSet.setAccelerationStructure(compactBlas) ==
                    GlobalBindlessSetError::InvalidResource &&
                accelerationStructureBindlessSet.setAccelerationStructure(tlas) == GlobalBindlessSetError::None &&
                accelerationStructureBindlessSet.setAccelerationStructure(tlas) == GlobalBindlessSetError::None;
        const GlobalBindlessSetStats bindlessStats = accelerationStructureBindlessSet.stats();
        valid = valid && bindlessStats.accelerationStructureUpdateCount == 1u;
    }

    if (valid) {
        const RhiMemoryStats stats = device.memoryStats();
        const RhiMemoryCategoryStats& accelerationStructureStats =
            stats.categories[static_cast<size_t>(RhiMemoryCategory::AccelerationStructure)];
        valid = stats.valid && accelerationStructureStats.resourceCount >= 11u &&
                accelerationStructureStats.allocationCount >= 7u;
    }

    if (valid) {
        accelerationStructureBindlessSet.shutdown();
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Compute);
        RhiSubmissionToken token;
        if (commands == nullptr || !commands->begin({"VulkanSmoke.AS.DeferredDestroy", RhiCommandListType::Compute}) ||
            !commands->accelerationStructureBarrier(
                {tlas, RhiResourceState::AccelerationStructureRead, RhiResourceState::AccelerationStructureRead}) ||
            !commands->end()) {
            valid = false;
        } else {
            RhiCommandList* submissions[] = {commands};
            valid = device.submit({"VulkanSmoke.AS.DeferredDestroy", submissions, 1u, RhiQueueType::Compute}, &token);
            if (valid) {
                device.destroyAccelerationStructure(tlas);
                tlas = {};
                device.destroyBuffer(tlasStorage);
                tlasStorage = {};
                valid = device.waitForSubmission(token);
            }
        }
    }

    cleanup();
    const RhiMemoryStats finalStats = device.memoryStats();
    const RhiMemoryCategoryStats& finalAccelerationStructureStats =
        finalStats.categories[static_cast<size_t>(RhiMemoryCategory::AccelerationStructure)];
    valid = valid && finalStats.valid && finalAccelerationStructureStats.resourceCount == 0u &&
            finalAccelerationStructureStats.allocationCount == 0u &&
            device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << "Acceleration-structure build, copy, descriptor, or lifetime validation failed\n";
    }
    return valid;
}

[[nodiscard]] bool validateTerrainBlasCache(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    const uint64_t validationErrorsBefore = device.validationErrorCount();
    TerrainBlasCache cache;
    renderer::rt::SceneTlasCache sceneTlas;
    if (!cache.init(&device) || !cache.supported()) {
        std::cerr << "Terrain BLAS cache initialization failed\n";
        cache.shutdown();
        return false;
    }
    cache.setBudgets({1u, 1024u * 1024u, 4096u, 1u});

    const auto makeVertex = [](const float x, const float y, const float z, const float u, const float v,
                               const int8_t face, const uint16_t layer, const uint16_t frameCount,
                               const uint8_t framesPerSecond, const bool animated, const uint8_t tintKind,
                               const uint8_t derivativeMaterialId) {
        return makeBlockVertex(x, y, z, u, v, static_cast<float>(face), 1.0f, 0.0f, 3.0f, static_cast<float>(layer),
                               static_cast<float>(frameCount), static_cast<float>(framesPerSecond),
                               animated ? 1.0f : 0.0f, tintKind, 64u, 128u, derivativeMaterialId);
    };
    const std::vector<BlockVertex> opaque{
        makeVertex(-2.0f, -2.0f, 2.0f, -0.5f, 0.0f, 2, 0u, 1u, 0u, false, BlockTintKinds::NONE, 3u),
        makeVertex(2.0f, -2.0f, 2.0f, 1.5f, 0.0f, 2, 0u, 1u, 0u, false, BlockTintKinds::NONE, 3u),
        makeVertex(0.0f, 2.0f, 2.0f, 0.5f, 1.0f, 2, 0u, 1u, 0u, false, BlockTintKinds::NONE, 3u)};
    const std::vector<BlockVertex> cutout{
        makeVertex(-2.0f, -2.0f, 1.0f, -0.5f, 0.0f, -1, 0u, 2u, 1u, true, BlockTintKinds::GRASS, 9u),
        makeVertex(2.0f, -2.0f, 1.0f, 1.5f, 0.0f, -1, 0u, 2u, 1u, true, BlockTintKinds::GRASS, 9u),
        makeVertex(0.0f, 2.0f, 1.0f, 0.5f, 1.0f, -1, 0u, 2u, 1u, true, BlockTintKinds::GRASS, 9u)};
    const auto makeGeometry = [&]() {
        TerrainBlasGeometry geometry;
        const TerrainBlasRequestResult prepared = TerrainBlasCache::prepareGeometry(opaque, cutout, {}, geometry);
        if (prepared != TerrainBlasRequestResult::Queued) {
            geometry = {};
        }
        return geometry;
    };
    const auto submitCacheFrame = [&](const char* debugName) {
        cache.beginFrame();
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Compute);
        if (commands == nullptr || !commands->begin({debugName, RhiCommandListType::Compute}) ||
            !cache.recordFrame(*commands) || !commands->end()) {
            cache.finishGraphExecution(false, {});
            std::cerr << cache.lastError() << '\n';
            return false;
        }
        RhiCommandList* submissions[] = {commands};
        RhiSubmissionToken token;
        if (!device.submit({debugName, submissions, 1u, RhiQueueType::Compute}, &token)) {
            cache.finishGraphExecution(false, token);
            return false;
        }
        cache.finishGraphExecution(true, token);
        if (!device.waitForSubmission(token)) {
            return false;
        }
        cache.beginFrame();
        return cache.healthy();
    };
    const auto validateMetadataReadback =
        [&](const TerrainBlasView& view,
            const std::vector<renderer::contracts::TerrainPrimitiveMetadata>& expectedMetadata) {
            const uint64_t byteCount =
                static_cast<uint64_t>(expectedMetadata.size()) * sizeof(renderer::contracts::TerrainPrimitiveMetadata);
            RhiBufferDesc readbackDesc;
            readbackDesc.debugName = "VulkanSmoke.TerrainBLAS.MetadataReadback";
            readbackDesc.size = byteCount;
            readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
            readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
            readbackDesc.initialState = RhiResourceState::TransferDst;
            readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
            const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
            if (!readback.isValid()) {
                return false;
            }

            RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Compute);
            bool readbackValid = commands != nullptr &&
                                 commands->begin({"VulkanSmoke.TerrainBLAS.MetadataCopy", RhiCommandListType::Compute});
            if (readbackValid) {
                commands->bufferBarrier(
                    {view.primitiveMetadataBuffer, RhiResourceState::ShaderRead, RhiResourceState::TransferSrc});
                commands->copyBuffer({view.primitiveMetadataBuffer, readback, 0u, 0u, byteCount});
                commands->bufferBarrier(
                    {view.primitiveMetadataBuffer, RhiResourceState::TransferSrc, RhiResourceState::ShaderRead});
                commands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
                readbackValid = commands->end();
            }
            RhiSubmissionToken token;
            if (readbackValid) {
                RhiCommandList* submissions[] = {commands};
                readbackValid =
                    device.submit({"VulkanSmoke.TerrainBLAS.MetadataCopy", submissions, 1u, RhiQueueType::Compute},
                                  &token) &&
                    device.waitForSubmission(token);
            }
            if (readbackValid) {
                const void* mapped = device.mapBuffer(readback, 0u, byteCount);
                readbackValid = mapped != nullptr && std::memcmp(mapped, expectedMetadata.data(), byteCount) == 0;
                if (mapped != nullptr) {
                    device.unmapBuffer(readback);
                }
            }
            device.destroyBuffer(readback);
            return readbackValid;
        };
    const auto submitTlasFrame = [&](const char* debugName) {
        sceneTlas.beginFrame();
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
        if (commands == nullptr || !commands->begin({debugName, RhiCommandListType::Graphics}) ||
            !sceneTlas.recordFrame(*commands) || !commands->end()) {
            sceneTlas.finishGraphExecution(false, {});
            return false;
        }
        RhiCommandList* submissions[] = {commands};
        RhiSubmissionToken token;
        if (!device.submit({debugName, submissions, 1u, RhiQueueType::Graphics}, &token)) {
            sceneTlas.finishGraphExecution(false, token);
            return false;
        }
        sceneTlas.finishGraphExecution(true, token);
        if (!device.waitForSubmission(token)) {
            return false;
        }
        sceneTlas.beginFrame();
        return sceneTlas.healthy();
    };
    const auto setTerrainInstance = [&](const TerrainBlasView& view,
                                        const std::optional<renderer::rt::SceneTlasTerrainHitData>& terrainHitData) {
        const uint8_t mask = renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiOpaque) |
                             renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiCutout) |
                             renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::ShadowCaster);
        std::vector<renderer::rt::SceneTlasInstanceInput> instances;
        instances.push_back({{renderer::rt::SceneTlasInstanceKind::Terrain, view.key.chunkKey, view.key.scy},
                             view.resource,
                             glm::translate(glm::mat4(1.0f), view.worldOrigin),
                             mask,
                             false,
                             terrainHitData,
                             {}});
        return sceneTlas.setInstances(std::move(instances));
    };

    const SubChunkGpuKey key{91, 4};
    const TerrainBlasGeometry expectedGeometry = makeGeometry();
    bool valid = cache.requestBuild(key, 1u, glm::vec3(32.0f, 64.0f, -16.0f), makeGeometry()) ==
                     TerrainBlasRequestResult::Queued &&
                 submitCacheFrame("VulkanSmoke.TerrainBLAS.Build1") && !cache.activeView(key).has_value() &&
                 submitCacheFrame("VulkanSmoke.TerrainBLAS.Compact1");
    std::optional<TerrainBlasView> firstView = cache.activeView(key);
    valid =
        valid && firstView.has_value() && firstView->revision == 1u && firstView->deviceAddress != 0u &&
        firstView->geometryBuffer.isValid() && firstView->primitiveMetadataBuffer.isValid() &&
        firstView->vertexAddress == device.bufferDeviceAddress(firstView->geometryBuffer) &&
        firstView->primitiveMetadataAddress == device.bufferDeviceAddress(firstView->primitiveMetadataBuffer) &&
        firstView->opaqueVertexCount == 3u && firstView->cutoutVertexCount == 3u && firstView->primitiveCount == 2u &&
        firstView->primitiveMetadataBytes ==
            sizeof(renderer::contracts::TerrainPrimitiveMetadata) * expectedGeometry.primitiveMetadata.size() &&
        firstView->resource->retainedBuffers().size() == 2u &&
        renderer::contracts::validTerrainRayTracingHitData(firstView->hitData) &&
        firstView->hitData.geometryCount == 2u &&
        firstView->hitData.geometries[0].geometryClass == renderer::contracts::TerrainRayTracingGeometryClass::Opaque &&
        firstView->hitData.geometries[0].primitiveBase == 0u &&
        firstView->hitData.geometries[1].geometryClass == renderer::contracts::TerrainRayTracingGeometryClass::Cutout &&
        firstView->hitData.geometries[1].primitiveBase == 1u &&
        validateMetadataReadback(*firstView, expectedGeometry.primitiveMetadata);
    if (valid) {
        valid = sceneTlas.init(&device) && sceneTlas.supported();
    }
    if (valid) {
        renderer::rt::SceneTlasTerrainHitData validHitData{firstView->hitData, firstView->geometryBuffer,
                                                           firstView->primitiveMetadataBuffer};
        renderer::rt::SceneTlasTerrainHitData foreignHitData = validHitData;
        foreignHitData.rayTracing.vertexAddress += 256u;
        renderer::rt::SceneTlasTerrainHitData swappedRoles = validHitData;
        std::swap(swappedRoles.vertexBuffer, swappedRoles.primitiveMetadataBuffer);
        valid = setTerrainInstance(*firstView, std::nullopt) == renderer::rt::SceneTlasSetResult::InvalidInstance &&
                setTerrainInstance(*firstView, foreignHitData) == renderer::rt::SceneTlasSetResult::InvalidInstance &&
                setTerrainInstance(*firstView, swappedRoles) == renderer::rt::SceneTlasSetResult::InvalidInstance &&
                setTerrainInstance(*firstView, validHitData) == renderer::rt::SceneTlasSetResult::Accepted &&
                submitTlasFrame("VulkanSmoke.TerrainTLAS.Build1");
    }
    const std::optional<renderer::rt::SceneTlasView> firstTlas = valid ? sceneTlas.activeView() : std::nullopt;
    renderer::contracts::TerrainRayTracingHitData firstHitData;
    renderer::rt::SceneTlasTerrainHitData firstSceneHitData;
    RhiBufferHandle firstGeometryBuffer;
    RhiBufferHandle firstPrimitiveMetadataBuffer;
    if (firstView.has_value()) {
        firstHitData = firstView->hitData;
        firstGeometryBuffer = firstView->geometryBuffer;
        firstPrimitiveMetadataBuffer = firstView->primitiveMetadataBuffer;
        firstSceneHitData = {firstView->hitData, firstView->geometryBuffer, firstView->primitiveMetadataBuffer};
    }
    const std::optional<renderer::contracts::TerrainRayTracingGpuInstance> firstGpuHitData =
        renderer::contracts::encodeTerrainRayTracingGpuInstance(firstHitData);
    const renderer::rt::SceneTlasStats firstTlasStats = sceneTlas.stats();
    valid = valid && firstTlas.has_value() && firstTlas->instanceCount == 1u && firstTlas->mappings.size() == 1u &&
            firstTlas->mappings[0].terrainHitData == std::optional(firstSceneHitData) &&
            firstTlas->terrainHitDataBuffer.isValid() &&
            firstTlas->terrainHitDataBytes == sizeof(renderer::contracts::TerrainRayTracingGpuInstance) &&
            firstTlasStats.activeTerrainHitDataBytes == firstTlas->terrainHitDataBytes && firstGpuHitData.has_value() &&
            validateGpuBufferContents(device, commandPool, firstTlas->terrainHitDataBuffer,
                                      RhiResourceState::StorageBuffer, &*firstGpuHitData, sizeof(*firstGpuHitData),
                                      "VulkanSmoke.TerrainTLAS.HitDataReadback1");
    if (valid) {
        const glm::vec2 rotation = renderer::contracts::rtgiCranleyPattersonRotation(0u);
        const auto wrapUnit = [](const float value) {
            return value - std::floor(value);
        };
        constexpr glm::vec2 kDesiredSample{0.125f, 0.0001f};
        const std::array<float, 4u> noiseTexel{wrapUnit(kDesiredSample.x - rotation.x),
                                               wrapUnit(kDesiredSample.y - rotation.y), 0.0f, 1.0f};
        RtgiTraceSmokeCase smokeCase;
        smokeCase.label = "Terrain Cutout Candidate Confirm";
        smokeCase.width = 2u;
        smokeCase.height = 1u;
        smokeCase.depth = {0.5f, 0.5f};
        smokeCase.normalAo = {0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f, 1.0f, 0.0f};
        smokeCase.materialAux = std::vector<float>(8u, 0.0f);
        smokeCase.voxelLight = {255u, 0u, 0u, 255u, 255u, 0u, 0u, 255u};
        smokeCase.blueNoise = {noiseTexel[0], noiseTexel[1], noiseTexel[2], noiseTexel[3],
                               noiseTexel[0], noiseTexel[1], noiseTexel[2], noiseTexel[3]};
        smokeCase.terrainAlbedoWidth = 2u;
        smokeCase.terrainAlbedoHeight = 1u;
        smokeCase.terrainAlbedoLayers = 2u;
        smokeCase.terrainAlbedo = {
            255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 0u, 255u, 255u, 255u, 255u,
        };
        smokeCase.grassColormap = {64u, 200u, 32u, 255u};
        smokeCase.skyAmbientRadiance = {0.5f, 0.5f, 0.5f};
        smokeCase.inverseViewProjection = glm::translate(glm::mat4(1.0f), glm::vec3(32.0f, 64.0f, -16.0f));
        smokeCase.cameraPosition = {32.0f, 64.0f, -17.0f};
        smokeCase.animationTime = 1.0f;
        smokeCase.instanceMask = renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiOpaque) |
                                 renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiCutout);
        RtgiTraceSmokeExpectedPixel rejectedCutout;
        rejectedCutout.classification = renderer::contracts::RtgiTraceClassification::Hit;
        rejectedCutout.candidateCount = 1u;
        rejectedCutout.minimumHitDistance = 1.95f;
        rejectedCutout.maximumHitDistance = 2.05f;
        rejectedCutout.minimumRadiance = {0.49f, 0.49f, 0.49f};
        rejectedCutout.maximumRadiance = {0.51f, 0.51f, 0.51f};
        rejectedCutout.hitIdentityHash = renderer::contracts::rtgiTerrainHitIdentityHash(
            firstView->hitData.revision, firstView->hitData.vertexAddress);
        RtgiTraceSmokeExpectedPixel confirmedCutout;
        confirmedCutout.classification = renderer::contracts::RtgiTraceClassification::Hit;
        confirmedCutout.candidateCount = 1u;
        confirmedCutout.confirmedCount = 1u;
        confirmedCutout.minimumHitDistance = 0.95f;
        confirmedCutout.maximumHitDistance = 1.05f;
        confirmedCutout.minimumRadiance = {0.02f, 0.28f, 0.004f};
        confirmedCutout.maximumRadiance = {0.03f, 0.31f, 0.007f};
        confirmedCutout.hitIdentityHash = renderer::contracts::rtgiTerrainHitIdentityHash(
            firstView->hitData.revision, firstView->hitData.vertexAddress);
        smokeCase.expectedPixels = {rejectedCutout, confirmedCutout};
        renderer::core::GlobalBindlessSet rtgiBindlessSet;
        renderer::core::GlobalBindlessSetConfig bindlessConfig;
        bindlessConfig.sampledTexture2DCapacity = 1u;
        bindlessConfig.sampledTextureCubeCapacity = 1u;
        bindlessConfig.samplerCapacity = 1u;
        bindlessConfig.storageBufferCapacity = 1u;
        valid = rtgiBindlessSet.initialize(device, bindlessConfig) == renderer::core::GlobalBindlessSetError::None &&
                validateRtgiTraceCase(device, commandPool, sceneTlas, *firstTlas, rtgiBindlessSet, smokeCase);
        rtgiBindlessSet.shutdown();
    }
    firstView.reset();

    valid = valid &&
            cache.requestBuild(key, 2u, glm::vec3(32.0f, 64.0f, -16.0f), makeGeometry()) ==
                TerrainBlasRequestResult::Queued &&
            submitCacheFrame("VulkanSmoke.TerrainBLAS.Build2");
    const std::optional<TerrainBlasView> duringRevision = cache.activeView(key);
    valid = valid && duringRevision.has_value() && duringRevision->revision == 1u &&
            submitCacheFrame("VulkanSmoke.TerrainBLAS.Compact2");
    const std::optional<TerrainBlasView> secondView = cache.activeView(key);
    valid = valid && secondView.has_value() && secondView->revision == 2u && secondView->deviceAddress != 0u &&
            secondView->hitData.revision == 2u && secondView->hitData.vertexAddress != firstHitData.vertexAddress &&
            secondView->hitData.primitiveMetadataAddress != firstHitData.primitiveMetadataAddress &&
            device.bufferDeviceAddress(firstGeometryBuffer) == firstHitData.vertexAddress &&
            device.bufferDeviceAddress(firstPrimitiveMetadataBuffer) == firstHitData.primitiveMetadataAddress;
    const std::optional<renderer::rt::SceneTlasView> retainedFirstTlas = valid ? sceneTlas.activeView() : std::nullopt;
    valid = valid && retainedFirstTlas.has_value() && retainedFirstTlas->revision == firstTlas->revision &&
            retainedFirstTlas->mappings[0].terrainHitData == std::optional(firstSceneHitData) &&
            setTerrainInstance(*secondView,
                               renderer::rt::SceneTlasTerrainHitData{secondView->hitData, secondView->geometryBuffer,
                                                                     secondView->primitiveMetadataBuffer}) ==
                renderer::rt::SceneTlasSetResult::Accepted;
    const std::optional<renderer::rt::SceneTlasView> activeAfterDesiredReplacement =
        valid ? sceneTlas.activeView() : std::nullopt;
    valid = valid && activeAfterDesiredReplacement.has_value() &&
            activeAfterDesiredReplacement->revision == firstTlas->revision &&
            activeAfterDesiredReplacement->mappings[0].terrainHitData == std::optional(firstSceneHitData) &&
            device.bufferDeviceAddress(firstGeometryBuffer) == firstHitData.vertexAddress &&
            device.bufferDeviceAddress(firstPrimitiveMetadataBuffer) == firstHitData.primitiveMetadataAddress &&
            submitTlasFrame("VulkanSmoke.TerrainTLAS.Build2");
    const std::optional<renderer::rt::SceneTlasView> secondTlas = valid ? sceneTlas.activeView() : std::nullopt;
    const std::optional<renderer::contracts::TerrainRayTracingGpuInstance> secondGpuHitData =
        secondView.has_value() ? renderer::contracts::encodeTerrainRayTracingGpuInstance(secondView->hitData)
                               : std::optional<renderer::contracts::TerrainRayTracingGpuInstance>{};
    const renderer::rt::SceneTlasStats secondTlasStats = sceneTlas.stats();
    valid = valid && secondTlas.has_value() && secondTlas->revision > firstTlas->revision &&
            secondTlas->instanceCount == 1u && secondTlas->mappings.size() == 1u &&
            secondTlas->mappings[0].terrainHitData.has_value() &&
            secondTlas->mappings[0].terrainHitData->rayTracing == secondView->hitData &&
            secondTlas->mappings[0].terrainHitData->vertexBuffer.index == secondView->geometryBuffer.index &&
            secondTlas->mappings[0].terrainHitData->vertexBuffer.generation == secondView->geometryBuffer.generation &&
            secondTlas->mappings[0].terrainHitData->primitiveMetadataBuffer.index ==
                secondView->primitiveMetadataBuffer.index &&
            secondTlas->mappings[0].terrainHitData->primitiveMetadataBuffer.generation ==
                secondView->primitiveMetadataBuffer.generation &&
            secondTlas->terrainHitDataBuffer.isValid() &&
            (secondTlas->terrainHitDataBuffer.index != firstTlas->terrainHitDataBuffer.index ||
             secondTlas->terrainHitDataBuffer.generation != firstTlas->terrainHitDataBuffer.generation) &&
            secondTlas->terrainHitDataBytes == sizeof(renderer::contracts::TerrainRayTracingGpuInstance) &&
            secondTlasStats.activeTerrainHitDataBytes == secondTlas->terrainHitDataBytes &&
            secondGpuHitData.has_value() &&
            validateGpuBufferContents(device, commandPool, secondTlas->terrainHitDataBuffer,
                                      RhiResourceState::StorageBuffer, &*secondGpuHitData, sizeof(*secondGpuHitData),
                                      "VulkanSmoke.TerrainTLAS.HitDataReadback2");

    TerrainBlasGeometry invalidGeometry = makeGeometry();
    if (!invalidGeometry.vertices.empty()) {
        invalidGeometry.vertices.front().x = std::numeric_limits<float>::quiet_NaN();
    }
    valid = valid &&
            cache.requestBuild(key, 3u, glm::vec3(32.0f, 64.0f, -16.0f), std::move(invalidGeometry)) ==
                TerrainBlasRequestResult::InvalidGeometry &&
            cache.requestBuild(key, 1u, glm::vec3(32.0f, 64.0f, -16.0f), makeGeometry()) ==
                TerrainBlasRequestResult::StaleRevision &&
            cache.requestBuild(key, 2u, glm::vec3(32.0f, 64.0f, -16.0f), makeGeometry()) ==
                TerrainBlasRequestResult::Unchanged;

    const TerrainBlasStats residentStats = cache.stats();
    valid = valid && residentStats.activeBlasCount == 1u && residentStats.activePrimitiveCount == 2u &&
            residentStats.activeGeometryBytes == sizeof(BlockVertex) * 6u &&
            residentStats.activePrimitiveMetadataBytes == sizeof(renderer::contracts::TerrainPrimitiveMetadata) * 2u &&
            residentStats.activeBlasBytes != 0u && cache.isSettled() &&
            sceneTlas.setInstances({}) == renderer::rt::SceneTlasSetResult::Accepted &&
            !sceneTlas.activeView().has_value() && sceneTlas.isSettled();
    cache.remove(key);
    const TerrainBlasStats removedStats = cache.stats();
    valid = valid && removedStats.activeBlasCount == 0u && removedStats.pendingBuildCount == 0u &&
            removedStats.pendingCompactionCount == 0u;

    sceneTlas.shutdown();
    cache.shutdown();
    device.waitIdle();
    valid = valid && device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << "Terrain BLAS build, compaction, revision, or lifetime validation failed\n";
    }
    return valid;
}

struct RayQuerySmokeResult {
    uint32_t committed = 0u;
    uint32_t candidateCount = 0u;
    uint32_t committedGeometry = 0u;
    uint32_t instanceCustomIndex = 0u;
    uint32_t candidateGeometry = 0u;
    uint32_t candidatePrimitive = 0u;
    uint32_t barycentricXBits = 0u;
    uint32_t barycentricYBits = 0u;
};

static_assert(sizeof(RayQuerySmokeResult) == 32u);

[[nodiscard]] bool validateCutoutRayQuery(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                          const RhiAccelerationStructureHandle sceneTlas) {
    using namespace renderer::core;

    constexpr uint32_t kRayCount = 4u;
    constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiBufferDesc outputDesc;
    outputDesc.debugName = "VulkanSmoke.CutoutRayQuery.Output";
    outputDesc.size = sizeof(RayQuerySmokeResult) * kRayCount;
    outputDesc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::MapRead);
    outputDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    outputDesc.initialState = RhiResourceState::StorageBuffer;
    outputDesc.memoryCategory = RhiMemoryCategory::Readback;
    RhiBufferHandle output = device.createBuffer(outputDesc, nullptr, 0u);
    GlobalBindlessSet bindlessSet;
    RhiShaderHandle shader;
    RhiPipelineLayoutHandle pipelineLayout;
    RhiPipelineHandle pipeline;
    const auto cleanup = [&]() {
        if (pipeline.isValid()) {
            device.destroyPipeline(pipeline);
        }
        if (pipelineLayout.isValid()) {
            device.destroyPipelineLayout(pipelineLayout);
        }
        if (shader.isValid()) {
            device.destroyShader(shader);
        }
        bindlessSet.shutdown();
        if (output.isValid()) {
            device.destroyBuffer(output);
        }
        device.waitIdle();
    };

    bool valid = sceneTlas.isValid() && output.isValid();
    if (valid) {
        GlobalBindlessSetConfig bindlessConfig;
        bindlessConfig.sampledTexture2DCapacity = 1u;
        bindlessConfig.sampledTextureCubeCapacity = 1u;
        bindlessConfig.samplerCapacity = 1u;
        bindlessConfig.storageBufferCapacity = 1u;
        valid = bindlessSet.initialize(device, bindlessConfig) == GlobalBindlessSetError::None;
    }
    const auto outputPublication =
        valid ? bindlessSet.publishStorageBuffer(output)
              : GlobalBindlessPublicationResult<renderer::contracts::BindlessStorageBufferTag>{};
    valid = valid && outputPublication.succeeded() &&
            bindlessSet.setAccelerationStructure(sceneTlas) == GlobalBindlessSetError::None;
    const GlobalBindlessSetStats bindlessStats = bindlessSet.stats();
    valid =
        valid && bindlessStats.storageBuffers.liveCount == 1u && bindlessStats.accelerationStructureUpdateCount == 1u;

    const std::optional<std::string> computeSource =
        valid ? renderer::rhi::loadShaderSource("tests/shaders/cutout_ray_query_test.comp") : std::nullopt;
    if (valid) {
        RhiShaderDesc shaderDesc;
        shaderDesc.debugName = "VulkanSmoke.CutoutRayQuery.Compute";
        shaderDesc.stage = RhiShaderStage::Compute;
        shaderDesc.source = computeSource.has_value() ? computeSource->c_str() : nullptr;
        shaderDesc.sourceSize = computeSource.has_value() ? computeSource->size() : 0u;
        shader = computeSource.has_value() ? device.createShader(shaderDesc) : RhiShaderHandle{};
        valid = shader.isValid();
    }
    if (valid) {
        RhiPipelineLayoutDesc pipelineLayoutDesc;
        pipelineLayoutDesc.debugName = "VulkanSmoke.CutoutRayQuery.PipelineLayout";
        pipelineLayoutDesc.bindGroupLayouts.push_back(bindlessSet.layout());
        pipelineLayoutDesc.pushConstantBytes = sizeof(uint32_t);
        pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
        pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
        RhiComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "VulkanSmoke.CutoutRayQuery.Pipeline";
        pipelineDesc.computeShader = shader;
        pipelineDesc.layout = pipelineLayout;
        pipeline = pipelineLayout.isValid() ? device.createComputePipeline(pipelineDesc) : RhiPipelineHandle{};
        valid = pipeline.isValid();
    }

    RhiCommandList* commands = valid ? commandPool.acquire(RhiCommandListType::Graphics) : nullptr;
    if (valid) {
        valid = commands != nullptr &&
                commands->begin({"VulkanSmoke.CutoutRayQuery.Commands", RhiCommandListType::Graphics});
    }
    if (valid) {
        commands->setComputePipeline(pipeline);
        commands->setBindGroup(0u, bindlessSet.bindGroup());
        commands->pushConstants(&outputPublication.handle.index, sizeof(outputPublication.handle.index),
                                rhiFlag(RhiShaderStage::Compute));
        commands->dispatch(kRayCount, 1u, 1u);
        commands->bufferBarrier({output, RhiResourceState::StorageBuffer, RhiResourceState::HostRead});
        valid = commands->end();
    }

    RhiSubmissionToken token;
    if (valid) {
        RhiCommandList* submissions[] = {commands};
        valid = device.submit({"VulkanSmoke.CutoutRayQuery.Submit", submissions, 1u, RhiQueueType::Graphics}, &token) &&
                token.isValid() && device.waitForSubmission(token);
    }

    if (valid) {
        const auto* results = static_cast<const RayQuerySmokeResult*>(device.mapBuffer(output, 0u, outputDesc.size));
        valid = results != nullptr;
        if (results != nullptr) {
            const auto decodeFloat = [](const uint32_t bits) {
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            };
            const auto validBarycentrics = [&](const RayQuerySmokeResult& result) {
                const float x = decodeFloat(result.barycentricXBits);
                const float y = decodeFloat(result.barycentricYBits);
                return std::isfinite(x) && std::isfinite(y) && std::abs(x - 0.25f) <= 0.0001f &&
                       std::abs(y - 0.25f) <= 0.0001f;
            };
            valid = results[0].committed == 1u && results[0].candidateCount == 0u &&
                    results[0].committedGeometry == 0u && results[0].instanceCustomIndex == 0u &&
                    results[0].candidateGeometry == kInvalidIndex && results[1].committed == 0u &&
                    results[1].candidateCount == 1u && results[1].committedGeometry == kInvalidIndex &&
                    results[1].instanceCustomIndex == 0u && results[1].candidateGeometry == 1u &&
                    results[1].candidatePrimitive == 0u && validBarycentrics(results[1]) &&
                    results[2].committed == 1u && results[2].candidateCount == 1u &&
                    results[2].committedGeometry == 1u && results[2].instanceCustomIndex == 0u &&
                    results[2].candidateGeometry == 1u && results[2].candidatePrimitive == 0u &&
                    validBarycentrics(results[2]) && results[3].committed == 1u && results[3].candidateCount == 1u &&
                    results[3].committedGeometry == 1u && results[3].instanceCustomIndex == 1u &&
                    results[3].candidateGeometry == 1u && results[3].candidatePrimitive == 0u &&
                    validBarycentrics(results[3]);
            device.unmapBuffer(output);
        }
    }

    cleanup();
    valid = valid && device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << "Cutout Ray Query candidate or confirmation validation failed\n";
    }
    return valid;
}

namespace {

[[nodiscard]] bool validateRtgiTraceCase(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                         renderer::rt::SceneTlasCache& sceneTlas,
                                         const renderer::rt::SceneTlasView& activeTlas,
                                         renderer::core::GlobalBindlessSet& globalBindlessSet,
                                         const RtgiTraceSmokeCase& smokeCase) {
    using namespace renderer::contracts;
    using namespace renderer::core;
    using namespace renderer::rt;

    struct SmokeTexture final {
        RhiTextureDesc desc;
        RhiTextureHandle texture;
        RhiTextureViewHandle view;
    };

    const size_t maximumSize = std::numeric_limits<size_t>::max();
    if (smokeCase.label == nullptr || smokeCase.width == 0u || smokeCase.height == 0u ||
        smokeCase.terrainAlbedoWidth == 0u || smokeCase.terrainAlbedoHeight == 0u ||
        smokeCase.terrainAlbedoLayers == 0u ||
        static_cast<size_t>(smokeCase.width) > maximumSize / static_cast<size_t>(smokeCase.height) ||
        static_cast<size_t>(smokeCase.terrainAlbedoWidth) >
            maximumSize / static_cast<size_t>(smokeCase.terrainAlbedoHeight)) {
        return false;
    }
    const size_t pixelCount = static_cast<size_t>(smokeCase.width) * static_cast<size_t>(smokeCase.height);
    const size_t terrainLayerTexels =
        static_cast<size_t>(smokeCase.terrainAlbedoWidth) * static_cast<size_t>(smokeCase.terrainAlbedoHeight);
    if (pixelCount > maximumSize / (4u * sizeof(float)) ||
        terrainLayerTexels > maximumSize / static_cast<size_t>(smokeCase.terrainAlbedoLayers)) {
        return false;
    }
    const size_t terrainTexelCount = terrainLayerTexels * static_cast<size_t>(smokeCase.terrainAlbedoLayers);
    if (terrainTexelCount > maximumSize / 4u || smokeCase.depth.size() != pixelCount ||
        smokeCase.normalAo.size() != pixelCount * 4u || smokeCase.materialAux.size() != pixelCount * 4u ||
        smokeCase.voxelLight.size() != pixelCount * 4u || smokeCase.blueNoise.size() != pixelCount * 4u ||
        smokeCase.terrainAlbedo.size() != terrainTexelCount * 4u || smokeCase.expectedPixels.size() != pixelCount) {
        return false;
    }

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    const auto createTexture = [&](const char* debugName, const RhiTextureDimension dimension,
                                   const RhiTextureViewType viewType, const RhiTextureFormat format,
                                   const uint32_t width, const uint32_t height, const uint32_t layers,
                                   const RhiTextureUsageFlags usage, const void* pixels, const size_t sizeBytes,
                                   const RhiResourceState finalState, SmokeTexture& output) {
        output.desc.debugName = debugName;
        output.desc.dimension = dimension;
        output.desc.format = format;
        output.desc.width = width;
        output.desc.height = height;
        output.desc.depthOrLayers = layers;
        output.desc.usage = usage;
        output.desc.memoryCategory = RhiMemoryCategory::Transient;
        if (pixels != nullptr) {
            RhiTextureInitialData initialData;
            initialData.pixels = pixels;
            initialData.sizeBytes = sizeBytes;
            initialData.layerCount = layers;
            initialData.finalState = finalState;
            output.texture = device.createTexture(output.desc, &initialData);
        } else {
            output.texture = device.createTexture(output.desc, nullptr);
        }
        if (!output.texture.isValid()) {
            return false;
        }
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = output.texture;
        viewDesc.viewType = viewType;
        viewDesc.format = format;
        viewDesc.layerCount = layers;
        output.view = device.createTextureView(viewDesc);
        return output.view.isValid();
    };

    constexpr RhiTextureUsageFlags kSampledUsage =
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    constexpr RhiTextureUsageFlags kStorageUsage =
        rhiFlag(RhiTextureUsage::Storage) | rhiFlag(RhiTextureUsage::TransferSrc);
    constexpr RhiTextureUsageFlags kRawSignalUsage = kStorageUsage | rhiFlag(RhiTextureUsage::Sampled);
    SmokeTexture depth;
    SmokeTexture normalAo;
    SmokeTexture materialAux;
    SmokeTexture voxelLight;
    SmokeTexture noise;
    SmokeTexture terrainAlbedo;
    SmokeTexture terrainNormal;
    SmokeTexture terrainSpecular;
    SmokeTexture grassColormap;
    SmokeTexture foliageColormap;
    SmokeTexture skyCapture;
    SmokeTexture localShadowSpotAtlas;
    SmokeTexture localShadowPointCubeArray;
    SmokeTexture radianceHitDistance;
    SmokeTexture relaxRadianceHitDistance;
    SmokeTexture reblurRadianceHitDistance;
    SmokeTexture validation;
    RtgiTracePass tracePass;
    RtgiSignalPackPass signalPackPass;
    RtgiSignalPackPass reblurSignalPackPass;
    ClusteredLightingPass clusteredLightingPass;
    RhiBufferHandle localShadowMetadata;
    RhiSamplerHandle localShadowSampler;
    RhiBufferHandle validationReadback;
    RhiBufferHandle radianceReadback;
    RhiBufferHandle relaxReadback;
    RhiBufferHandle reblurReadback;
    bool clusteredPrepared = false;
    bool clusteredFinished = false;
    const auto cleanup = [&]() {
        if (clusteredPrepared && !clusteredFinished) {
            clusteredLightingPass.finishGraphExecution(false, {});
        }
        device.waitIdle();
        reblurSignalPackPass.shutdown();
        signalPackPass.shutdown();
        tracePass.shutdown();
        clusteredLightingPass.shutdown();
        if (validationReadback.isValid()) {
            device.destroyBuffer(validationReadback);
        }
        if (radianceReadback.isValid()) {
            device.destroyBuffer(radianceReadback);
        }
        if (relaxReadback.isValid()) {
            device.destroyBuffer(relaxReadback);
        }
        if (reblurReadback.isValid()) {
            device.destroyBuffer(reblurReadback);
        }
        if (localShadowMetadata.isValid()) {
            device.destroyBuffer(localShadowMetadata);
        }
        if (localShadowSampler.isValid()) {
            device.destroySampler(localShadowSampler);
        }
        SmokeTexture* textures[] = {
            &validation,
            &reblurRadianceHitDistance,
            &relaxRadianceHitDistance,
            &radianceHitDistance,
            &localShadowPointCubeArray,
            &localShadowSpotAtlas,
            &skyCapture,
            &foliageColormap,
            &grassColormap,
            &terrainSpecular,
            &terrainNormal,
            &terrainAlbedo,
            &noise,
            &voxelLight,
            &materialAux,
            &normalAo,
            &depth,
        };
        for (SmokeTexture* texture : textures) {
            if (texture->view.isValid()) {
                device.destroyTextureView(texture->view);
            }
            if (texture->texture.isValid()) {
                device.destroyTexture(texture->texture);
            }
        }
    };

    std::vector<uint8_t> terrainNormalPixels(terrainTexelCount * 4u);
    std::vector<uint8_t> terrainSpecularPixels(terrainTexelCount * 4u);
    for (size_t texel = 0u; texel < terrainTexelCount; ++texel) {
        terrainNormalPixels[texel * 4u + 0u] = 128u;
        terrainNormalPixels[texel * 4u + 1u] = 128u;
        terrainNormalPixels[texel * 4u + 2u] = 255u;
        terrainNormalPixels[texel * 4u + 3u] = 255u;
        terrainSpecularPixels[texel * 4u + 0u] = 0u;
        terrainSpecularPixels[texel * 4u + 1u] = 254u;
        terrainSpecularPixels[texel * 4u + 2u] = 0u;
        terrainSpecularPixels[texel * 4u + 3u] = 255u;
    }
    const std::array<float, 4u> skyCapturePixel{smokeCase.skyCaptureRadiance.r, smokeCase.skyCaptureRadiance.g,
                                                smokeCase.skyCaptureRadiance.b, 1.0f};
    constexpr float kInitializedShadowDepth = 1.0f;
    constexpr std::array<float, 6u> kInitializedPointShadowDepth{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    constexpr RhiTextureUsageFlags kShadowTextureUsage =
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    bool valid =
        activeTlas.accelerationStructure.isValid() && activeTlas.terrainHitDataBuffer.isValid() &&
        activeTlas.gpuSceneMaterialBuffer.isValid() && activeTlas.gpuSceneGeometryBuffer.isValid() &&
        activeTlas.gpuSceneInstanceBuffer.isValid() &&
        (activeTlas.bindlessIdentity == 0u || activeTlas.bindlessIdentity == globalBindlessSet.identity()) &&
        createTexture("VulkanSmoke.RTGI.Depth", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Depth32Float, smokeCase.width, smokeCase.height, 1u,
                      kSampledUsage | rhiFlag(RhiTextureUsage::DepthStencilAttachment), smokeCase.depth.data(),
                      smokeCase.depth.size() * sizeof(float), RhiResourceState::DepthRead, depth) &&
        createTexture("VulkanSmoke.RTGI.NormalAo", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rgba32Float, smokeCase.width, smokeCase.height, 1u, kSampledUsage,
                      smokeCase.normalAo.data(), smokeCase.normalAo.size() * sizeof(float),
                      RhiResourceState::ShaderRead, normalAo) &&
        createTexture("VulkanSmoke.RTGI.MaterialAux", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rgba32Float, smokeCase.width, smokeCase.height, 1u, kSampledUsage,
                      smokeCase.materialAux.data(), smokeCase.materialAux.size() * sizeof(float),
                      RhiResourceState::ShaderRead, materialAux) &&
        createTexture("VulkanSmoke.RTGI.VoxelLight", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rgba8Unorm, smokeCase.width, smokeCase.height, 1u, kSampledUsage,
                      smokeCase.voxelLight.data(), smokeCase.voxelLight.size(), RhiResourceState::ShaderRead,
                      voxelLight) &&
        createTexture("VulkanSmoke.RTGI.BlueNoise", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rgba32Float, smokeCase.width, smokeCase.height, 1u, kSampledUsage,
                      smokeCase.blueNoise.data(), smokeCase.blueNoise.size() * sizeof(float),
                      RhiResourceState::ShaderRead, noise) &&
        createTexture("VulkanSmoke.RTGI.TerrainAlbedo", RhiTextureDimension::Texture2DArray,
                      RhiTextureViewType::Texture2DArray, RhiTextureFormat::Rgba8Unorm, smokeCase.terrainAlbedoWidth,
                      smokeCase.terrainAlbedoHeight, smokeCase.terrainAlbedoLayers, kSampledUsage,
                      smokeCase.terrainAlbedo.data(), smokeCase.terrainAlbedo.size(), RhiResourceState::ShaderRead,
                      terrainAlbedo) &&
        createTexture("VulkanSmoke.RTGI.TerrainNormal", RhiTextureDimension::Texture2DArray,
                      RhiTextureViewType::Texture2DArray, RhiTextureFormat::Rgba8Unorm, smokeCase.terrainAlbedoWidth,
                      smokeCase.terrainAlbedoHeight, smokeCase.terrainAlbedoLayers, kSampledUsage,
                      terrainNormalPixels.data(), terrainNormalPixels.size(), RhiResourceState::ShaderRead,
                      terrainNormal) &&
        createTexture("VulkanSmoke.RTGI.TerrainSpecular", RhiTextureDimension::Texture2DArray,
                      RhiTextureViewType::Texture2DArray, RhiTextureFormat::Rgba8Unorm, smokeCase.terrainAlbedoWidth,
                      smokeCase.terrainAlbedoHeight, smokeCase.terrainAlbedoLayers, kSampledUsage,
                      terrainSpecularPixels.data(), terrainSpecularPixels.size(), RhiResourceState::ShaderRead,
                      terrainSpecular) &&
        createTexture("VulkanSmoke.RTGI.GrassColormap", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rgba8Unorm, 1u, 1u, 1u, kSampledUsage, smokeCase.grassColormap.data(),
                      smokeCase.grassColormap.size(), RhiResourceState::ShaderRead, grassColormap) &&
        createTexture("VulkanSmoke.RTGI.FoliageColormap", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rgba8Unorm, 1u, 1u, 1u, kSampledUsage, smokeCase.foliageColormap.data(),
                      smokeCase.foliageColormap.size(), RhiResourceState::ShaderRead, foliageColormap) &&
        createTexture("VulkanSmoke.RTGI.SkyCapture", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rgba32Float, 1u, 1u, 1u, kSampledUsage, skyCapturePixel.data(),
                      sizeof(skyCapturePixel), RhiResourceState::ShaderRead, skyCapture) &&
        createTexture("VulkanSmoke.RTGI.LocalShadowSpotAtlas", RhiTextureDimension::Texture2D,
                      RhiTextureViewType::Texture2D, RhiTextureFormat::Depth32Float, 1u, 1u, 1u, kShadowTextureUsage,
                      &kInitializedShadowDepth, sizeof(kInitializedShadowDepth), RhiResourceState::DepthRead,
                      localShadowSpotAtlas) &&
        createTexture("VulkanSmoke.RTGI.LocalShadowPointCubeArray", RhiTextureDimension::CubeArray,
                      RhiTextureViewType::CubeArray, RhiTextureFormat::Depth32Float, 1u, 1u, 6u, kShadowTextureUsage,
                      kInitializedPointShadowDepth.data(), sizeof(kInitializedPointShadowDepth),
                      RhiResourceState::DepthRead, localShadowPointCubeArray) &&
        createTexture("VulkanSmoke.RTGI.RadianceHitDistance", RhiTextureDimension::Texture2D,
                      RhiTextureViewType::Texture2D, RhiTextureFormat::Rgba16Float, smokeCase.width, smokeCase.height,
                      1u, kRawSignalUsage, nullptr, 0u, RhiResourceState::Undefined, radianceHitDistance) &&
        createTexture("VulkanSmoke.RTGI.RelaxRadianceHitDistance", RhiTextureDimension::Texture2D,
                      RhiTextureViewType::Texture2D, RhiTextureFormat::Rgba16Float, smokeCase.width, smokeCase.height,
                      1u, kStorageUsage, nullptr, 0u, RhiResourceState::Undefined, relaxRadianceHitDistance) &&
        createTexture("VulkanSmoke.RTGI.ReblurRadianceHitDistance", RhiTextureDimension::Texture2D,
                      RhiTextureViewType::Texture2D, RhiTextureFormat::Rgba16Float, smokeCase.width, smokeCase.height,
                      1u, kStorageUsage, nullptr, 0u, RhiResourceState::Undefined, reblurRadianceHitDistance) &&
        createTexture("VulkanSmoke.RTGI.Validation", RhiTextureDimension::Texture2D, RhiTextureViewType::Texture2D,
                      RhiTextureFormat::Rg32Uint, smokeCase.width, smokeCase.height, 1u, kStorageUsage, nullptr, 0u,
                      RhiResourceState::Undefined, validation);

    RhiBufferDesc localShadowMetadataDesc;
    localShadowMetadataDesc.debugName = "VulkanSmoke.RTGI.LocalShadowMetadata";
    localShadowMetadataDesc.size = static_cast<uint64_t>(kLocalShadowMetadataCount) * sizeof(LocalShadowMetadata);
    localShadowMetadataDesc.usage = rhiFlag(RhiBufferUsage::Storage);
    localShadowMetadataDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    localShadowMetadataDesc.initialState = RhiResourceState::StorageBuffer;
    localShadowMetadataDesc.memoryCategory = RhiMemoryCategory::SceneData;
    if (valid) {
        localShadowMetadata = device.createBuffer(localShadowMetadataDesc, nullptr, 0u);
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = RhiFilter::Nearest;
        samplerDesc.magFilter = RhiFilter::Nearest;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = RhiAddressMode::ClampToEdge;
        samplerDesc.addressV = RhiAddressMode::ClampToEdge;
        samplerDesc.addressW = RhiAddressMode::ClampToEdge;
        localShadowSampler = device.createSampler(samplerDesc);
        valid = localShadowMetadata.isValid() && localShadowSampler.isValid();
    }

    if (valid) {
        valid = globalBindlessSet.setAccelerationStructure(activeTlas.accelerationStructure) ==
                GlobalBindlessSetError::None;
    }

    RhiBufferDesc validationReadbackDesc;
    validationReadbackDesc.debugName = "VulkanSmoke.RTGI.ValidationReadback";
    validationReadbackDesc.size = static_cast<uint64_t>(sizeof(uint32_t) * 2u) * pixelCount;
    validationReadbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    validationReadbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    validationReadbackDesc.initialState = RhiResourceState::TransferDst;
    validationReadbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    RhiBufferDesc radianceReadbackDesc = validationReadbackDesc;
    radianceReadbackDesc.debugName = "VulkanSmoke.RTGI.RadianceReadback";
    radianceReadbackDesc.size = static_cast<uint64_t>(sizeof(uint16_t) * 4u) * pixelCount;
    if (valid) {
        validationReadback = device.createBuffer(validationReadbackDesc, nullptr, 0u);
        radianceReadback = device.createBuffer(radianceReadbackDesc, nullptr, 0u);
        relaxReadback = device.createBuffer(radianceReadbackDesc, nullptr, 0u);
        reblurReadback = device.createBuffer(radianceReadbackDesc, nullptr, 0u);
        valid = validationReadback.isValid() && radianceReadback.isValid() && relaxReadback.isValid() &&
                reblurReadback.isValid();
    }

    SharedRenderResources shared;
    shared.rhiDevice = &device;
    shared.commandListPool = &commandPool;
    shared.sceneTlasCache = &sceneTlas;
    shared.globalBindlessSet = &globalBindlessSet;
    FrameContext frame;
    frame.shared = &shared;
    frame.frameIndex = 0u;
    const TemporalExtent renderExtent{smokeCase.width, smokeCase.height};
    frame.temporalExtents = makeTemporalFrameExtents(renderExtent, renderExtent, renderExtent, renderExtent);
    frame.camera.position = smokeCase.cameraPosition;
    frame.camera.view = glm::translate(glm::mat4(1.0f), -frame.camera.position);
    const glm::vec3 cameraRelativePosition = frame.camera.position - activeTlas.sceneOrigin;
    const glm::mat4 inverseProjection =
        glm::translate(glm::mat4(1.0f), -cameraRelativePosition) * smokeCase.inverseViewProjection;
    frame.camera.projection = glm::inverse(inverseProjection);
    frame.camera.invViewProj = glm::inverse(frame.camera.projection * frame.camera.view);
    frame.camera.jitteredInvViewProj = frame.camera.invViewProj;
    frame.camera.nearPlane = 0.1f;
    frame.camera.farPlane = 32.0f;
    frame.preExposure = smokeCase.preExposure;
    frame.previousPreExposure = smokeCase.previousPreExposure;
    frame.skyColors.sunDirection = smokeCase.sunDirection;
    frame.skyColors.moonDirection = smokeCase.moonDirection;
    frame.skyColors.sunVisibility = smokeCase.sunVisibility;
    frame.skyColors.moonVisibility = smokeCase.moonVisibility;
    frame.skyIlluminance.sunIlluminance = smokeCase.sunRadiance;
    frame.skyIlluminance.moonIlluminance = smokeCase.moonRadiance;
    frame.skyIlluminance.skyIlluminance = smokeCase.skyAmbientRadiance;
    frame.animationTime = smokeCase.animationTime;

    if (valid) {
        valid = clusteredLightingPass.setLights(smokeCase.lights) &&
                clusteredLightingPass.setLocalShadowResources({localShadowMetadata, localShadowMetadataDesc.size,
                                                               localShadowSpotAtlas.view,
                                                               localShadowPointCubeArray.view, localShadowSampler}) &&
                clusteredLightingPass.prepareGraphFrame(device, frame, smokeCase.width, smokeCase.height);
        clusteredPrepared = valid;
    }

    RenderGraph graph;
    const auto importTexture = [&](const SmokeTexture& texture, const RhiResourceState initialState,
                                   const RhiResourceState finalState) {
        return graph.importTexture({texture.desc.debugName, texture.texture, texture.desc, initialState, finalState,
                                    texture.view, RhiQueueType::Graphics, RhiQueueType::Graphics});
    };
    RtgiTracePass::GraphResources resources;
    RtgiSignalPackPass::GraphResources packResources;
    ClusteredLightingPass::GraphResources clusteredResources;
    RtgiTracePass::LightingResources lightingResources;
    RgBufferHandle validationReadbackResource;
    RgBufferHandle radianceReadbackResource;
    RgBufferHandle relaxReadbackResource;
    RgBufferHandle reblurReadbackResource;
    RgBufferHandle localShadowMetadataResource;
    RgTextureHandle localShadowSpotAtlasResource;
    RgTextureHandle localShadowPointCubeArrayResource;
    RgPassHandle traceHandle;
    RgPassHandle packHandle;
    RgPassHandle reblurPackHandle;
    if (valid) {
        resources.depth = importTexture(depth, RhiResourceState::DepthRead, RhiResourceState::DepthRead);
        resources.normalAo = importTexture(normalAo, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.materialAux = importTexture(materialAux, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.voxelLight = importTexture(voxelLight, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.blueNoise = importTexture(noise, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.terrainAlbedo =
            importTexture(terrainAlbedo, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.terrainNormal =
            importTexture(terrainNormal, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.terrainSpecular =
            importTexture(terrainSpecular, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.grassColormap =
            importTexture(grassColormap, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.foliageColormap =
            importTexture(foliageColormap, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.skyCapture = importTexture(skyCapture, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.diffuseRadianceHitDistance =
            importTexture(radianceHitDistance, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        resources.validation = importTexture(validation, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        packResources.rawDiffuseRadianceHitDistance = resources.diffuseRadianceHitDistance;
        packResources.depth = resources.depth;
        packResources.relaxDiffuseRadianceHitDistance =
            importTexture(relaxRadianceHitDistance, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        packResources.reblurDiffuseRadianceHitDistance =
            importTexture(reblurRadianceHitDistance, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        packResources.validation = resources.validation;
        localShadowSpotAtlasResource =
            importTexture(localShadowSpotAtlas, RhiResourceState::DepthRead, RhiResourceState::DepthRead);
        localShadowPointCubeArrayResource =
            importTexture(localShadowPointCubeArray, RhiResourceState::DepthRead, RhiResourceState::DepthRead);
        localShadowMetadataResource =
            graph.importBuffer({localShadowMetadataDesc.debugName, localShadowMetadata, localShadowMetadataDesc,
                                RhiResourceState::StorageBuffer, RhiResourceState::StorageBuffer,
                                RhiQueueType::Graphics, RhiQueueType::Graphics});
        valid = clusteredLightingPass.importGraphResources(graph, clusteredResources);
        validationReadbackResource = graph.importBuffer(
            {"RTGI.ValidationReadback", validationReadback, validationReadbackDesc, RhiResourceState::TransferDst,
             RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});
        radianceReadbackResource = graph.importBuffer({"RTGI.RadianceReadback", radianceReadback, radianceReadbackDesc,
                                                       RhiResourceState::TransferDst, RhiResourceState::HostRead,
                                                       RhiQueueType::Graphics, RhiQueueType::Graphics});
        relaxReadbackResource = graph.importBuffer({"RTGI.RelaxReadback", relaxReadback, radianceReadbackDesc,
                                                    RhiResourceState::TransferDst, RhiResourceState::HostRead,
                                                    RhiQueueType::Graphics, RhiQueueType::Graphics});
        reblurReadbackResource = graph.importBuffer({"RTGI.ReblurReadback", reblurReadback, radianceReadbackDesc,
                                                     RhiResourceState::TransferDst, RhiResourceState::HostRead,
                                                     RhiQueueType::Graphics, RhiQueueType::Graphics});
        valid =
            valid && resources.depth.isValid() && resources.normalAo.isValid() && resources.materialAux.isValid() &&
            resources.voxelLight.isValid() && resources.blueNoise.isValid() &&
            resources.diffuseRadianceHitDistance.isValid() && resources.terrainAlbedo.isValid() &&
            resources.terrainNormal.isValid() && resources.terrainSpecular.isValid() &&
            resources.grassColormap.isValid() && resources.foliageColormap.isValid() &&
            resources.skyCapture.isValid() && resources.validation.isValid() && localShadowMetadataResource.isValid() &&
            localShadowSpotAtlasResource.isValid() && localShadowPointCubeArrayResource.isValid() &&
            packResources.relaxDiffuseRadianceHitDistance.isValid() &&
            packResources.reblurDiffuseRadianceHitDistance.isValid() && validationReadbackResource.isValid() &&
            radianceReadbackResource.isValid() && relaxReadbackResource.isValid() && reblurReadbackResource.isValid();
    }
    if (valid) {
        const RgPassHandle inputReady = graph.addPass({"RTGI.InputReady", RgPassType::Copy, RhiQueueType::Graphics})
                                            .setExecute([](RgPassContext&) { return true; })
                                            .handle();
        const RgPassHandle clusteredReady = clusteredLightingPass.addGraphPasses(graph, clusteredResources, inputReady);
        lightingResources.bindGroupLayout = clusteredLightingPass.consumerBindGroupLayout();
        lightingResources.bindGroup = clusteredLightingPass.consumerBindGroup();
        lightingResources.lights = clusteredResources.lights;
        lightingResources.worldCells = clusteredResources.worldCells;
        lightingResources.worldIndices = clusteredResources.worldIndices;
        lightingResources.worldHeader = clusteredResources.worldHeader;
        lightingResources.localShadowMetadata = localShadowMetadataResource;
        lightingResources.localShadowSpotAtlas = localShadowSpotAtlasResource;
        lightingResources.localShadowPointCubeArray = localShadowPointCubeArrayResource;
        RtgiTracePass::Settings settings;
        settings.maxRayDistance = smokeCase.maxRayDistance;
        settings.maxShadowRayDistance = smokeCase.maxShadowRayDistance;
        settings.minimumRayOriginBias = smokeCase.minimumRayOriginBias;
        settings.instanceMask = smokeCase.instanceMask;
        settings.shadowInstanceMask = smokeCase.shadowInstanceMask;
        settings.terrainNormalMapsEnabled = smokeCase.terrainNormalMapsEnabled;
        settings.terrainSpecularMapsEnabled = smokeCase.terrainSpecularMapsEnabled;
        traceHandle = tracePass.addGraphPass(graph, frame, settings, resources, lightingResources, clusteredReady);
        valid = traceHandle.isValid();
        if (valid) {
            RtgiSignalPackPass::Settings packSettings;
            packSettings.useJitteredProjection = settings.useJitteredProjection;
            packSettings.method = RtgiSignalPackPass::Method::Relax;
            packHandle = signalPackPass.addGraphPass(graph, frame, packSettings, packResources, traceHandle);
            valid = packHandle.isValid();
            if (valid) {
                packSettings.method = RtgiSignalPackPass::Method::Reblur;
                reblurPackHandle =
                    reblurSignalPackPass.addGraphPass(graph, frame, packSettings, packResources, packHandle);
                valid = reblurPackHandle.isValid();
            }
        }
    }
    if (valid) {
        graph.addPass({"RTGI.ValidationCopy", RgPassType::Copy, RhiQueueType::Graphics})
            .dependsOn(reblurPackHandle)
            .readTexture(resources.validation, RhiResourceState::TransferSrc)
            .readTexture(resources.diffuseRadianceHitDistance, RhiResourceState::TransferSrc)
            .readTexture(packResources.relaxDiffuseRadianceHitDistance, RhiResourceState::TransferSrc)
            .readTexture(packResources.reblurDiffuseRadianceHitDistance, RhiResourceState::TransferSrc)
            .writeBuffer(validationReadbackResource, RhiResourceState::TransferDst)
            .writeBuffer(radianceReadbackResource, RhiResourceState::TransferDst)
            .writeBuffer(relaxReadbackResource, RhiResourceState::TransferDst)
            .writeBuffer(reblurReadbackResource, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                RhiTextureBufferCopy validationCopy;
                validationCopy.srcTexture = context.texture(resources.validation);
                validationCopy.dstBuffer = context.buffer(validationReadbackResource);
                validationCopy.bytesPerRow = sizeof(uint32_t) * 2u * smokeCase.width;
                validationCopy.rowsPerImage = smokeCase.height;
                validationCopy.width = smokeCase.width;
                validationCopy.height = smokeCase.height;
                context.commandList().copyTextureToBuffer(validationCopy);
                RhiTextureBufferCopy radianceCopy;
                radianceCopy.srcTexture = context.texture(resources.diffuseRadianceHitDistance);
                radianceCopy.dstBuffer = context.buffer(radianceReadbackResource);
                radianceCopy.bytesPerRow = sizeof(uint16_t) * 4u * smokeCase.width;
                radianceCopy.rowsPerImage = smokeCase.height;
                radianceCopy.width = smokeCase.width;
                radianceCopy.height = smokeCase.height;
                context.commandList().copyTextureToBuffer(radianceCopy);
                RhiTextureBufferCopy relaxCopy = radianceCopy;
                relaxCopy.srcTexture = context.texture(packResources.relaxDiffuseRadianceHitDistance);
                relaxCopy.dstBuffer = context.buffer(relaxReadbackResource);
                context.commandList().copyTextureToBuffer(relaxCopy);
                RhiTextureBufferCopy reblurCopy = radianceCopy;
                reblurCopy.srcTexture = context.texture(packResources.reblurDiffuseRadianceHitDistance);
                reblurCopy.dstBuffer = context.buffer(reblurReadbackResource);
                context.commandList().copyTextureToBuffer(reblurCopy);
                return true;
            });
        const RgCompileResult compiled = graph.compile();
        valid = compiled.succeeded();
        if (!valid) {
            std::cerr << "RTGI Trace Render Graph compile failed: " << compiled.message << '\n';
        }
    }
    if (valid) {
        const RgExecuteResult executed = graph.execute(device, commandPool);
        clusteredLightingPass.finishGraphExecution(executed.succeeded(), executed.completionToken());
        clusteredFinished = true;
        sceneTlas.finishGraphExecution(executed.succeeded(), executed.completionToken());
        valid = executed.succeeded() && executed.completionToken().isValid() &&
                device.waitForSubmission(executed.completionToken());
        if (!executed.succeeded()) {
            std::cerr << "RTGI Trace Render Graph execution failed: " << executed.message << '\n';
        }
    }
    if (valid) {
        const auto* validationResult =
            static_cast<const uint32_t*>(device.mapBuffer(validationReadback, 0u, validationReadbackDesc.size));
        const auto* radianceResult =
            static_cast<const uint16_t*>(device.mapBuffer(radianceReadback, 0u, radianceReadbackDesc.size));
        const auto* relaxResult =
            static_cast<const uint16_t*>(device.mapBuffer(relaxReadback, 0u, radianceReadbackDesc.size));
        const auto* reblurResult =
            static_cast<const uint16_t*>(device.mapBuffer(reblurReadback, 0u, radianceReadbackDesc.size));
        valid = validationResult != nullptr && radianceResult != nullptr && relaxResult != nullptr &&
                reblurResult != nullptr;
        if (validationResult != nullptr && radianceResult != nullptr && relaxResult != nullptr &&
            reblurResult != nullptr) {
            const auto unpackSignal = [](const uint16_t* signal, const size_t pixelIndex) {
                return glm::vec4(glm::unpackHalf1x16(signal[pixelIndex * 4u + 0u]),
                                 glm::unpackHalf1x16(signal[pixelIndex * 4u + 1u]),
                                 glm::unpackHalf1x16(signal[pixelIndex * 4u + 2u]),
                                 glm::unpackHalf1x16(signal[pixelIndex * 4u + 3u]));
            };
            const auto nearHalf = [](const float actual, const float expected) {
                return std::abs(actual - expected) <= std::max(0.002f, std::abs(expected) * 0.002f);
            };
            for (size_t pixelIndex = 0u; pixelIndex < pixelCount; ++pixelIndex) {
                const uint32_t validationWord = validationResult[pixelIndex * 2u];
                const uint32_t hitIdentityHash = validationResult[pixelIndex * 2u + 1u];
                const glm::vec3 radiance{glm::unpackHalf1x16(radianceResult[pixelIndex * 4u + 0u]),
                                         glm::unpackHalf1x16(radianceResult[pixelIndex * 4u + 1u]),
                                         glm::unpackHalf1x16(radianceResult[pixelIndex * 4u + 2u])};
                const float hitDistance = glm::unpackHalf1x16(radianceResult[pixelIndex * 4u + 3u]);
                const std::optional<glm::vec3> sceneRadiance = rtgiRemovePreExposure(radiance, smokeCase.preExposure);
                const glm::vec4 relaxSignal = unpackSignal(relaxResult, pixelIndex);
                const glm::vec4 reblurSignal = unpackSignal(reblurResult, pixelIndex);
                const RtgiTraceSmokeExpectedPixel& expected = smokeCase.expectedPixels[pixelIndex];
                const bool radianceValid =
                    std::isfinite(radiance.r) && std::isfinite(radiance.g) && std::isfinite(radiance.b) &&
                    radiance.r >= expected.minimumRadiance.r && radiance.g >= expected.minimumRadiance.g &&
                    radiance.b >= expected.minimumRadiance.b && radiance.r <= expected.maximumRadiance.r &&
                    radiance.g <= expected.maximumRadiance.g && radiance.b <= expected.maximumRadiance.b;
                bool packedSignalsValid = false;
                if (expected.classification == RtgiTraceClassification::Hit ||
                    expected.classification == RtgiTraceClassification::Miss) {
                    const size_t pixelX = pixelIndex % smokeCase.width;
                    const size_t pixelY = pixelIndex / smokeCase.width;
                    const glm::vec2 screenUv{(static_cast<float>(pixelX) + 0.5f) / static_cast<float>(smokeCase.width),
                                             (static_cast<float>(pixelY) + 0.5f) /
                                                 static_cast<float>(smokeCase.height)};
                    const glm::vec2 clipUv{screenUv.x, 1.0f - screenUv.y};
                    const glm::mat4 inverseProjection = frame.camera.view * frame.camera.invViewProj;
                    const glm::vec4 viewPositionH =
                        inverseProjection *
                        glm::vec4(clipUv * 2.0f - 1.0f, smokeCase.depth[pixelIndex] * 2.0f - 1.0f, 1.0f);
                    std::optional<glm::vec4> expectedReblur;
                    if (std::isfinite(viewPositionH.w) && std::abs(viewPositionH.w) > 1.0e-7f) {
                        const float viewZ = viewPositionH.z / viewPositionH.w;
                        const std::optional<float> normalizedHitDistance =
                            rtgiReblurNormalizedHitDistance(hitDistance, viewZ, {}, 1.0f);
                        if (normalizedHitDistance.has_value() && sceneRadiance.has_value()) {
                            expectedReblur =
                                rtgiPackReblurRadianceAndNormalizedHitDistance(*sceneRadiance, *normalizedHitDistance);
                        }
                    }
                    const std::optional<glm::vec4> expectedRelax =
                        sceneRadiance.has_value() ? rtgiPackRelaxRadianceAndHitDistance(*sceneRadiance, hitDistance)
                                                  : std::nullopt;
                    packedSignalsValid = expectedRelax.has_value() && expectedReblur.has_value();
                    for (uint32_t component = 0u; component < 4u && packedSignalsValid; ++component) {
                        packedSignalsValid = nearHalf(relaxSignal[component], (*expectedRelax)[component]) &&
                                             nearHalf(reblurSignal[component], (*expectedReblur)[component]);
                    }
                } else {
                    packedSignalsValid = relaxSignal == glm::vec4(0.0f) && reblurSignal == glm::vec4(0.0f);
                }
                const bool pixelValid = rtgiTraceValidationClassification(validationWord) == expected.classification &&
                                        rtgiTraceValidationCandidateCount(validationWord) == expected.candidateCount &&
                                        rtgiTraceValidationConfirmedCount(validationWord) == expected.confirmedCount &&
                                        hitIdentityHash == expected.hitIdentityHash && std::isfinite(hitDistance) &&
                                        hitDistance >= expected.minimumHitDistance &&
                                        hitDistance <= expected.maximumHitDistance && radianceValid &&
                                        packedSignalsValid;
                if (!pixelValid) {
                    std::cerr << smokeCase.label << " pixel " << pixelIndex << " validation failed: class="
                              << static_cast<uint32_t>(rtgiTraceValidationClassification(validationWord))
                              << " candidates=" << rtgiTraceValidationCandidateCount(validationWord)
                              << " confirmed=" << rtgiTraceValidationConfirmedCount(validationWord)
                              << " identityHash=" << hitIdentityHash << " radiance=(" << radiance.r << ", "
                              << radiance.g << ", " << radiance.b << ") distance=" << hitDistance << " relax=("
                              << relaxSignal.x << ", " << relaxSignal.y << ", " << relaxSignal.z << ", "
                              << relaxSignal.w << ") reblur=(" << reblurSignal.x << ", " << reblurSignal.y << ", "
                              << reblurSignal.z << ", " << reblurSignal.w << ")\n";
                    valid = false;
                }
            }
            const RtgiTracePass::Stats& stats = tracePass.stats();
            const RtgiSignalPackPass::Stats& packStats = signalPackPass.stats();
            const RtgiSignalPackPass::Stats& reblurPackStats = reblurSignalPackPass.stats();
            valid = valid && stats.dispatched && stats.frameIndex == frame.frameIndex &&
                    stats.sceneTlasRevision == activeTlas.revision && stats.width == smokeCase.width &&
                    stats.height == smokeCase.height && stats.instanceMask == smokeCase.instanceMask &&
                    stats.terrainHitDataBytes == activeTlas.terrainHitDataBytes &&
                    stats.gpuSceneMaterialBytes == activeTlas.gpuSceneMaterialBytes &&
                    stats.gpuSceneGeometryBytes == activeTlas.gpuSceneGeometryBytes &&
                    stats.gpuSceneInstanceBytes == activeTlas.gpuSceneInstanceBytes &&
                    stats.gpuSceneMaterialCount == activeTlas.gpuSceneMaterialCount &&
                    stats.gpuSceneGeometryCount == activeTlas.gpuSceneGeometryCount && packStats.dispatched &&
                    packStats.width == smokeCase.width && packStats.height == smokeCase.height &&
                    packStats.reblurHitDistance.constantScale == 3.0f &&
                    packStats.reblurHitDistance.viewZScale == 0.1f &&
                    packStats.reblurHitDistance.roughnessScale == 20.0f && packStats.diffuseRoughness == 1.0f &&
                    packStats.preExposure == smokeCase.preExposure &&
                    packStats.previousPreExposure == smokeCase.previousPreExposure &&
                    packStats.method == RtgiSignalPackPass::Method::Relax && reblurPackStats.dispatched &&
                    reblurPackStats.method == RtgiSignalPackPass::Method::Reblur;
        }
        if (validationResult != nullptr) {
            device.unmapBuffer(validationReadback);
        }
        if (radianceResult != nullptr) {
            device.unmapBuffer(radianceReadback);
        }
        if (relaxResult != nullptr) {
            device.unmapBuffer(relaxReadback);
        }
        if (reblurResult != nullptr) {
            device.unmapBuffer(reblurReadback);
        }
    }

    cleanup();
    valid = valid && device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << smokeCase.label << " RTGI trace pass validation failed\n";
    }
    return valid;
}

[[nodiscard]] bool validateRtgiSignalPackInvalidInputs(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    using namespace renderer::contracts;

    struct SmokeTexture final {
        RhiTextureDesc desc;
        RhiTextureHandle texture;
        RhiTextureViewHandle view;
    };

    constexpr uint32_t kWidth = 7u;
    constexpr uint32_t kHeight = 1u;
    constexpr size_t kPixelCount = static_cast<size_t>(kWidth) * kHeight;
    const uint64_t validationErrorsBefore = device.validationErrorCount();
    const auto createTexture = [&](const char* debugName, const RhiTextureFormat format,
                                   const RhiTextureUsageFlags usage, const void* pixels, const size_t sizeBytes,
                                   const RhiResourceState finalState, SmokeTexture& output) {
        output.desc.debugName = debugName;
        output.desc.format = format;
        output.desc.width = kWidth;
        output.desc.height = kHeight;
        output.desc.usage = usage;
        output.desc.memoryCategory = RhiMemoryCategory::Transient;
        if (pixels != nullptr) {
            RhiTextureInitialData initialData;
            initialData.pixels = pixels;
            initialData.sizeBytes = sizeBytes;
            initialData.finalState = finalState;
            output.texture = device.createTexture(output.desc, &initialData);
        } else {
            output.texture = device.createTexture(output.desc, nullptr);
        }
        if (!output.texture.isValid()) {
            return false;
        }
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = output.texture;
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.format = format;
        output.view = device.createTextureView(viewDesc);
        return output.view.isValid();
    };

    std::vector<uint16_t> rawSignal(kPixelCount * 4u, 0u);
    const auto setRawSignal = [&](const size_t pixel, const glm::vec4& value) {
        for (uint32_t component = 0u; component < 4u; ++component) {
            rawSignal[pixel * 4u + component] = glm::packHalf1x16(value[component]);
        }
    };
    setRawSignal(0u, glm::vec4(4.0f, 8.0f, 12.0f, 2.0f));
    setRawSignal(1u, glm::vec4(1.0f, 2.0f, 3.0f, kRtgiNrdFp16Max));
    setRawSignal(2u, glm::vec4(0.0f));
    setRawSignal(3u, glm::vec4(0.0f));
    setRawSignal(4u, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    rawSignal[4u * 4u] = 0x7e00u;
    setRawSignal(5u, glm::vec4(1.0f));
    rawSignal[5u * 4u + 3u] = 0x7c00u;
    setRawSignal(6u, glm::vec4(4.0f, 8.0f, 12.0f, 2.0f));

    std::vector<float> depth(kPixelCount, 0.25f);
    depth[2u] = 1.0f;
    depth[6u] = std::numeric_limits<float>::quiet_NaN();
    std::vector<uint32_t> validation(kPixelCount * 2u, 0u);
    const auto setValidation = [&](const size_t pixel, const RtgiTraceClassification classification,
                                   const uint32_t candidateCount, const uint32_t confirmedCount,
                                   const uint32_t identity) {
        const std::optional<uint32_t> word = encodeRtgiTraceValidation(classification, candidateCount, confirmedCount);
        if (!word.has_value()) {
            return false;
        }
        validation[pixel * 2u] = *word;
        validation[pixel * 2u + 1u] = identity;
        return true;
    };
    bool valid = setValidation(0u, RtgiTraceClassification::Hit, 1u, 1u, 111u) &&
                 setValidation(1u, RtgiTraceClassification::Miss, 2u, 0u, 0u) &&
                 setValidation(2u, RtgiTraceClassification::Sky, 0u, 0u, 0u) &&
                 setValidation(3u, RtgiTraceClassification::Translucent, 0u, 0u, 0u) &&
                 setValidation(4u, RtgiTraceClassification::Hit, 7u, 2u, 444u) &&
                 setValidation(5u, RtgiTraceClassification::Miss, 3u, 1u, 555u) &&
                 setValidation(6u, RtgiTraceClassification::Hit, 5u, 4u, 666u);

    constexpr RhiTextureUsageFlags kRawUsage =
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    constexpr RhiTextureUsageFlags kDepthUsage = kRawUsage | rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    constexpr RhiTextureUsageFlags kOutputUsage =
        rhiFlag(RhiTextureUsage::Storage) | rhiFlag(RhiTextureUsage::TransferSrc);
    constexpr RhiTextureUsageFlags kValidationUsage = kOutputUsage | rhiFlag(RhiTextureUsage::TransferDst);
    SmokeTexture raw;
    SmokeTexture depthTexture;
    SmokeTexture relax;
    SmokeTexture reblur;
    SmokeTexture validationTexture;
    RtgiSignalPackPass relaxPass;
    RtgiSignalPackPass reblurPass;
    RhiBufferHandle validationReadback;
    RhiBufferHandle relaxReadback;
    RhiBufferHandle reblurReadback;
    const auto cleanup = [&]() {
        device.waitIdle();
        reblurPass.shutdown();
        relaxPass.shutdown();
        RhiBufferHandle* buffers[] = {&reblurReadback, &relaxReadback, &validationReadback};
        for (RhiBufferHandle* buffer : buffers) {
            if (buffer->isValid()) {
                device.destroyBuffer(*buffer);
            }
        }
        SmokeTexture* textures[] = {&validationTexture, &reblur, &relax, &depthTexture, &raw};
        for (SmokeTexture* texture : textures) {
            if (texture->view.isValid()) {
                device.destroyTextureView(texture->view);
            }
            if (texture->texture.isValid()) {
                device.destroyTexture(texture->texture);
            }
        }
    };

    valid = valid &&
            createTexture("VulkanSmoke.RTGI.SignalPack.Raw", RhiTextureFormat::Rgba16Float, kRawUsage, rawSignal.data(),
                          rawSignal.size() * sizeof(uint16_t), RhiResourceState::ShaderRead, raw) &&
            createTexture("VulkanSmoke.RTGI.SignalPack.Depth", RhiTextureFormat::Depth32Float, kDepthUsage,
                          depth.data(), depth.size() * sizeof(float), RhiResourceState::DepthRead, depthTexture) &&
            createTexture("VulkanSmoke.RTGI.SignalPack.Relax", RhiTextureFormat::Rgba16Float, kOutputUsage, nullptr, 0u,
                          RhiResourceState::Undefined, relax) &&
            createTexture("VulkanSmoke.RTGI.SignalPack.Reblur", RhiTextureFormat::Rgba16Float, kOutputUsage, nullptr,
                          0u, RhiResourceState::Undefined, reblur) &&
            createTexture("VulkanSmoke.RTGI.SignalPack.Validation", RhiTextureFormat::Rg32Uint, kValidationUsage,
                          validation.data(), validation.size() * sizeof(uint32_t), RhiResourceState::ShaderWrite,
                          validationTexture);

    RhiBufferDesc validationReadbackDesc;
    validationReadbackDesc.debugName = "VulkanSmoke.RTGI.SignalPack.ValidationReadback";
    validationReadbackDesc.size = validation.size() * sizeof(uint32_t);
    validationReadbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    validationReadbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    validationReadbackDesc.initialState = RhiResourceState::TransferDst;
    validationReadbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    RhiBufferDesc signalReadbackDesc = validationReadbackDesc;
    signalReadbackDesc.debugName = "VulkanSmoke.RTGI.SignalPack.SignalReadback";
    signalReadbackDesc.size = rawSignal.size() * sizeof(uint16_t);
    if (valid) {
        validationReadback = device.createBuffer(validationReadbackDesc, nullptr, 0u);
        relaxReadback = device.createBuffer(signalReadbackDesc, nullptr, 0u);
        reblurReadback = device.createBuffer(signalReadbackDesc, nullptr, 0u);
        valid = validationReadback.isValid() && relaxReadback.isValid() && reblurReadback.isValid();
    }

    SharedRenderResources shared;
    shared.rhiDevice = &device;
    shared.commandListPool = &commandPool;
    FrameContext frame;
    frame.shared = &shared;
    frame.temporalExtents =
        makeTemporalFrameExtents({kWidth, kHeight}, {kWidth, kHeight}, {kWidth, kHeight}, {kWidth, kHeight});
    frame.camera.view = glm::mat4(1.0f);
    glm::mat4 inverseProjection(1.0f);
    inverseProjection[3][2] = -9.5f;
    frame.camera.projection = glm::inverse(inverseProjection);
    frame.camera.invViewProj = inverseProjection;
    frame.camera.jitteredInvViewProj = frame.camera.invViewProj;
    frame.preExposure = 4.0f;
    frame.previousPreExposure = 2.0f;

    RenderGraph graph;
    RtgiSignalPackPass::GraphResources resources;
    RgBufferHandle validationReadbackResource;
    RgBufferHandle relaxReadbackResource;
    RgBufferHandle reblurReadbackResource;
    RgPassHandle packHandle;
    RgPassHandle reblurPackHandle;
    if (valid) {
        const auto importTexture = [&](const SmokeTexture& texture, const RhiResourceState initialState,
                                       const RhiResourceState finalState) {
            return graph.importTexture({texture.desc.debugName, texture.texture, texture.desc, initialState, finalState,
                                        texture.view, RhiQueueType::Graphics, RhiQueueType::Graphics});
        };
        resources.rawDiffuseRadianceHitDistance =
            importTexture(raw, RhiResourceState::ShaderRead, RhiResourceState::ShaderRead);
        resources.depth = importTexture(depthTexture, RhiResourceState::DepthRead, RhiResourceState::DepthRead);
        resources.relaxDiffuseRadianceHitDistance =
            importTexture(relax, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        resources.reblurDiffuseRadianceHitDistance =
            importTexture(reblur, RhiResourceState::Undefined, RhiResourceState::ShaderWrite);
        resources.validation =
            importTexture(validationTexture, RhiResourceState::ShaderWrite, RhiResourceState::ShaderWrite);
        validationReadbackResource =
            graph.importBuffer({validationReadbackDesc.debugName, validationReadback, validationReadbackDesc,
                                RhiResourceState::TransferDst, RhiResourceState::HostRead, RhiQueueType::Graphics,
                                RhiQueueType::Graphics});
        relaxReadbackResource = graph.importBuffer({signalReadbackDesc.debugName, relaxReadback, signalReadbackDesc,
                                                    RhiResourceState::TransferDst, RhiResourceState::HostRead,
                                                    RhiQueueType::Graphics, RhiQueueType::Graphics});
        reblurReadbackResource = graph.importBuffer({signalReadbackDesc.debugName, reblurReadback, signalReadbackDesc,
                                                     RhiResourceState::TransferDst, RhiResourceState::HostRead,
                                                     RhiQueueType::Graphics, RhiQueueType::Graphics});
        const RgPassHandle inputReady =
            graph.addPass({"RTGI.SignalPackInputReady", RgPassType::Copy, RhiQueueType::Graphics})
                .setExecute([](RgPassContext&) { return true; })
                .handle();
        RtgiSignalPackPass::GraphResources aliasedResources = resources;
        aliasedResources.reblurDiffuseRadianceHitDistance = aliasedResources.relaxDiffuseRadianceHitDistance;
        valid = !relaxPass.addGraphPass(graph, frame, {}, aliasedResources, inputReady).isValid();
        FrameContext invalidExposureFrame = frame;
        invalidExposureFrame.preExposure = 0.0f;
        valid = valid && !relaxPass.addGraphPass(graph, invalidExposureFrame, {}, resources, inputReady).isValid();
        RtgiSignalPackPass::Settings relaxSettings;
        relaxSettings.method = RtgiSignalPackPass::Method::Relax;
        packHandle = relaxPass.addGraphPass(graph, frame, relaxSettings, resources, inputReady);
        RtgiSignalPackPass::Settings reblurSettings;
        reblurSettings.method = RtgiSignalPackPass::Method::Reblur;
        reblurPackHandle = reblurPass.addGraphPass(graph, frame, reblurSettings, resources, packHandle);
        valid = valid && resources.rawDiffuseRadianceHitDistance.isValid() && resources.depth.isValid() &&
                resources.relaxDiffuseRadianceHitDistance.isValid() &&
                resources.reblurDiffuseRadianceHitDistance.isValid() && resources.validation.isValid() &&
                validationReadbackResource.isValid() && relaxReadbackResource.isValid() &&
                reblurReadbackResource.isValid() && packHandle.isValid() && reblurPackHandle.isValid();
    }
    if (valid) {
        graph.addPass({"RTGI.SignalPackCopy", RgPassType::Copy, RhiQueueType::Graphics})
            .dependsOn(reblurPackHandle)
            .readTexture(resources.validation, RhiResourceState::TransferSrc)
            .readTexture(resources.relaxDiffuseRadianceHitDistance, RhiResourceState::TransferSrc)
            .readTexture(resources.reblurDiffuseRadianceHitDistance, RhiResourceState::TransferSrc)
            .writeBuffer(validationReadbackResource, RhiResourceState::TransferDst)
            .writeBuffer(relaxReadbackResource, RhiResourceState::TransferDst)
            .writeBuffer(reblurReadbackResource, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                RhiTextureBufferCopy validationCopy;
                validationCopy.srcTexture = context.texture(resources.validation);
                validationCopy.dstBuffer = context.buffer(validationReadbackResource);
                validationCopy.bytesPerRow = sizeof(uint32_t) * 2u * kWidth;
                validationCopy.rowsPerImage = kHeight;
                validationCopy.width = kWidth;
                validationCopy.height = kHeight;
                context.commandList().copyTextureToBuffer(validationCopy);
                RhiTextureBufferCopy signalCopy;
                signalCopy.srcTexture = context.texture(resources.relaxDiffuseRadianceHitDistance);
                signalCopy.dstBuffer = context.buffer(relaxReadbackResource);
                signalCopy.bytesPerRow = sizeof(uint16_t) * 4u * kWidth;
                signalCopy.rowsPerImage = kHeight;
                signalCopy.width = kWidth;
                signalCopy.height = kHeight;
                context.commandList().copyTextureToBuffer(signalCopy);
                signalCopy.srcTexture = context.texture(resources.reblurDiffuseRadianceHitDistance);
                signalCopy.dstBuffer = context.buffer(reblurReadbackResource);
                context.commandList().copyTextureToBuffer(signalCopy);
                return true;
            });
        const RgCompileResult compiled = graph.compile();
        valid = compiled.succeeded();
        if (!valid) {
            std::cerr << "RTGI Signal Pack Render Graph compile failed: " << compiled.message << '\n';
        }
    }
    if (valid) {
        const RgExecuteResult executed = graph.execute(device, commandPool);
        valid = executed.succeeded() && executed.completionToken().isValid() &&
                device.waitForSubmission(executed.completionToken());
        if (!executed.succeeded()) {
            std::cerr << "RTGI Signal Pack Render Graph execution failed: " << executed.message << '\n';
        }
    }
    if (valid) {
        const auto* validationResult =
            static_cast<const uint32_t*>(device.mapBuffer(validationReadback, 0u, validationReadbackDesc.size));
        const auto* relaxResult =
            static_cast<const uint16_t*>(device.mapBuffer(relaxReadback, 0u, signalReadbackDesc.size));
        const auto* reblurResult =
            static_cast<const uint16_t*>(device.mapBuffer(reblurReadback, 0u, signalReadbackDesc.size));
        valid = validationResult != nullptr && relaxResult != nullptr && reblurResult != nullptr;
        if (validationResult != nullptr && relaxResult != nullptr && reblurResult != nullptr) {
            const std::array<RtgiTraceClassification, kPixelCount> expectedClassifications{
                RtgiTraceClassification::Hit,       RtgiTraceClassification::Miss,
                RtgiTraceClassification::Sky,       RtgiTraceClassification::Translucent,
                RtgiTraceClassification::NonFinite, RtgiTraceClassification::NonFinite,
                RtgiTraceClassification::NonFinite};
            constexpr std::array<uint32_t, kPixelCount> kExpectedCandidates{1u, 2u, 0u, 0u, 7u, 3u, 5u};
            constexpr std::array<uint32_t, kPixelCount> kExpectedConfirmed{1u, 0u, 0u, 0u, 2u, 1u, 4u};
            constexpr std::array<uint32_t, kPixelCount> kExpectedIdentities{111u, 0u, 0u, 0u, 444u, 555u, 666u};
            const std::array<glm::vec4, kPixelCount> expectedRelax{glm::vec4(1.0f, 2.0f, 3.0f, 2.0f),
                                                                   glm::vec4(0.25f, 0.5f, 0.75f, kRtgiNrdFp16Max),
                                                                   glm::vec4(0.0f),
                                                                   glm::vec4(0.0f),
                                                                   glm::vec4(0.0f),
                                                                   glm::vec4(0.0f),
                                                                   glm::vec4(0.0f)};
            const std::array<glm::vec4, kPixelCount> expectedReblur{glm::vec4(2.0f, -1.0f, 0.0f, 0.5f),
                                                                    glm::vec4(0.5f, -0.25f, 0.0f, 1.0f),
                                                                    glm::vec4(0.0f),
                                                                    glm::vec4(0.0f),
                                                                    glm::vec4(0.0f),
                                                                    glm::vec4(0.0f),
                                                                    glm::vec4(0.0f)};
            const auto unpackSignal = [](const uint16_t* signal, const size_t pixel) {
                return glm::vec4(glm::unpackHalf1x16(signal[pixel * 4u]), glm::unpackHalf1x16(signal[pixel * 4u + 1u]),
                                 glm::unpackHalf1x16(signal[pixel * 4u + 2u]),
                                 glm::unpackHalf1x16(signal[pixel * 4u + 3u]));
            };
            const auto nearSignal = [](const glm::vec4& actual, const glm::vec4& expected) {
                for (uint32_t component = 0u; component < 4u; ++component) {
                    const float tolerance = std::max(0.002f, std::abs(expected[component]) * 0.002f);
                    if (std::abs(actual[component] - expected[component]) > tolerance) {
                        return false;
                    }
                }
                return true;
            };
            for (size_t pixel = 0u; pixel < kPixelCount; ++pixel) {
                const uint32_t validationWord = validationResult[pixel * 2u];
                const uint32_t identity = validationResult[pixel * 2u + 1u];
                const glm::vec4 relaxSignal = unpackSignal(relaxResult, pixel);
                const glm::vec4 reblurSignal = unpackSignal(reblurResult, pixel);
                const bool pixelValid =
                    rtgiTraceValidationClassification(validationWord) == expectedClassifications[pixel] &&
                    rtgiTraceValidationCandidateCount(validationWord) == kExpectedCandidates[pixel] &&
                    rtgiTraceValidationConfirmedCount(validationWord) == kExpectedConfirmed[pixel] &&
                    identity == kExpectedIdentities[pixel] && nearSignal(relaxSignal, expectedRelax[pixel]) &&
                    nearSignal(reblurSignal, expectedReblur[pixel]);
                if (!pixelValid) {
                    std::cerr << "RTGI Signal Pack pixel " << pixel << " failed: class="
                              << static_cast<uint32_t>(rtgiTraceValidationClassification(validationWord))
                              << " candidates=" << rtgiTraceValidationCandidateCount(validationWord)
                              << " confirmed=" << rtgiTraceValidationConfirmedCount(validationWord)
                              << " identity=" << identity << " relax=(" << relaxSignal.x << ", " << relaxSignal.y
                              << ", " << relaxSignal.z << ", " << relaxSignal.w << ") reblur=(" << reblurSignal.x
                              << ", " << reblurSignal.y << ", " << reblurSignal.z << ", " << reblurSignal.w << ")\n";
                    valid = false;
                }
            }
            const RtgiSignalPackPass::Stats& stats = relaxPass.stats();
            const RtgiSignalPackPass::Stats& reblurStats = reblurPass.stats();
            valid = valid && stats.dispatched && stats.width == kWidth && stats.height == kHeight &&
                    stats.reblurHitDistance.constantScale == 3.0f && stats.reblurHitDistance.viewZScale == 0.1f &&
                    stats.reblurHitDistance.roughnessScale == 20.0f && stats.diffuseRoughness == 1.0f &&
                    stats.preExposure == 4.0f && stats.previousPreExposure == 2.0f &&
                    stats.method == RtgiSignalPackPass::Method::Relax && reblurStats.dispatched &&
                    reblurStats.method == RtgiSignalPackPass::Method::Reblur;
        }
        if (validationResult != nullptr) {
            device.unmapBuffer(validationReadback);
        }
        if (relaxResult != nullptr) {
            device.unmapBuffer(relaxReadback);
        }
        if (reblurResult != nullptr) {
            device.unmapBuffer(reblurReadback);
        }
    }

    cleanup();
    valid = valid && device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << "RTGI signal-pack invalid-input validation failed\n";
    }
    return valid;
}

} // namespace

[[nodiscard]] bool validateRtgiTracePass(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                         renderer::rt::SceneTlasCache& sceneTlas,
                                         const renderer::rt::SceneTlasView& activeTlas,
                                         renderer::core::GlobalBindlessSet& globalBindlessSet) {
    using namespace renderer::contracts;

    const glm::vec2 rotation = renderer::contracts::rtgiCranleyPattersonRotation(0u);
    const auto wrapUnit = [](const float value) {
        return value - std::floor(value);
    };
    constexpr glm::vec2 kDesiredSample{0.125f, 0.0001f};
    RtgiTraceSmokeCase smokeCase;
    smokeCase.label = "Static Mesh Alpha Mask";
    smokeCase.width = 2u;
    smokeCase.height = 1u;
    smokeCase.depth = {0.25f, 0.25f};
    smokeCase.normalAo = {1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    smokeCase.materialAux = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    smokeCase.voxelLight = {255u, 0u, 0u, 255u, 255u, 0u, 0u, 255u};
    const std::array<float, 4u> noisePixel{wrapUnit(kDesiredSample.x - rotation.x),
                                           wrapUnit(kDesiredSample.y - rotation.y), 0.0f, 1.0f};
    smokeCase.blueNoise = {noisePixel[0], noisePixel[1], noisePixel[2], noisePixel[3],
                           noisePixel[0], noisePixel[1], noisePixel[2], noisePixel[3]};
    smokeCase.terrainAlbedoWidth = 1u;
    smokeCase.terrainAlbedoHeight = 1u;
    smokeCase.terrainAlbedoLayers = 1u;
    smokeCase.terrainAlbedo = {255u, 255u, 255u, 255u};
    smokeCase.inverseViewProjection = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.1f, 2.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 1.0f, 1.0f));
    smokeCase.cameraPosition = {0.5f, 0.1f, 2.0f};
    smokeCase.maxRayDistance = 4.0f;
    smokeCase.sunDirection = {0.0f, 0.0f, 1.0f};
    smokeCase.sunRadiance = {0.0f, 1.5f, 0.0f};
    smokeCase.sunVisibility = 1.0f;
    GpuLightNormalizationInput pointInput;
    pointInput.lightId = StableLightId{701u};
    pointInput.type = GpuLightType::Point;
    pointInput.positionMeters = {0.0f, 0.0f, 0.0f};
    pointInput.rangeMeters = 4.0f;
    pointInput.colorLinear = {0.0f, 0.0f, 1.0f};
    pointInput.intensity = 2145.7078f;
    pointInput.intensityUnit = GpuLightIntensityUnit::Candela;
    pointInput.contributionFlags = gpuLightContributionFlagBit(GpuLightContributionFlag::Diffuse);
    const GpuLightNormalizationResult point = normalizeGpuLight(pointInput);
    if (!point.succeeded()) {
        return false;
    }
    smokeCase.lights = {point.light};
    smokeCase.instanceMask = renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiOpaque) |
                             renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiCutout);
    RtgiTraceSmokeExpectedPixel rejectedMask;
    rejectedMask.classification = renderer::contracts::RtgiTraceClassification::Hit;
    rejectedMask.candidateCount = 1u;
    rejectedMask.confirmedCount = 0u;
    rejectedMask.hitIdentityHash = renderer::contracts::rtgiStableHitIdentityHash(601u, 501u);
    rejectedMask.minimumHitDistance = 1.45f;
    rejectedMask.maximumHitDistance = 1.55f;
    rejectedMask.minimumRadiance = {0.999f, 0.456f, 0.212f};
    rejectedMask.maximumRadiance = {1.001f, 0.46f, 0.216f};
    RtgiTraceSmokeExpectedPixel confirmedMask;
    confirmedMask.classification = renderer::contracts::RtgiTraceClassification::Hit;
    confirmedMask.candidateCount = 1u;
    confirmedMask.confirmedCount = 1u;
    confirmedMask.hitIdentityHash = renderer::contracts::rtgiStableHitIdentityHash(602u, 502u);
    confirmedMask.minimumHitDistance = 0.45f;
    confirmedMask.maximumHitDistance = 0.55f;
    confirmedMask.minimumRadiance = {0.0f, 0.456f, 0.0f};
    confirmedMask.maximumRadiance = {0.001f, 0.46f, 0.001f};
    smokeCase.expectedPixels = {rejectedMask, confirmedMask};
    if (!validateRtgiTraceCase(device, commandPool, sceneTlas, activeTlas, globalBindlessSet, smokeCase)) {
        return false;
    }

    RtgiTraceSmokeCase missCase;
    missCase.label = "Sky Capture Miss";
    missCase.width = 1u;
    missCase.height = 1u;
    missCase.depth = {0.25f};
    missCase.normalAo = {1.0f, 1.0f, 1.0f, 0.0f};
    missCase.materialAux = {0.0f, 0.0f, 0.0f, 0.0f};
    missCase.voxelLight = {255u, 0u, 0u, 255u};
    missCase.blueNoise = {noisePixel[0], noisePixel[1], noisePixel[2], noisePixel[3]};
    missCase.terrainAlbedoWidth = 1u;
    missCase.terrainAlbedoHeight = 1u;
    missCase.terrainAlbedoLayers = 1u;
    missCase.terrainAlbedo = {255u, 255u, 255u, 255u};
    missCase.skyCaptureRadiance = {0.25f, 0.5f, 0.75f};
    missCase.inverseViewProjection = glm::translate(glm::mat4(1.0f), glm::vec3(10.5f, 0.1f, 2.0f)) *
                                     glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 1.0f, 1.0f));
    missCase.cameraPosition = {10.5f, 0.1f, 2.0f};
    missCase.preExposure = 4.0f;
    missCase.previousPreExposure = 2.0f;
    missCase.maxRayDistance = 4.0f;
    missCase.instanceMask = smokeCase.instanceMask;
    RtgiTraceSmokeExpectedPixel miss;
    miss.classification = RtgiTraceClassification::Miss;
    miss.minimumHitDistance = kRtgiNrdFp16Max;
    miss.maximumHitDistance = kRtgiNrdFp16Max;
    miss.minimumRadiance = {0.999f, 1.999f, 2.999f};
    miss.maximumRadiance = {1.001f, 2.001f, 3.001f};
    missCase.expectedPixels = {miss};
    if (!validateRtgiTraceCase(device, commandPool, sceneTlas, activeTlas, globalBindlessSet, missCase)) {
        return false;
    }

    RtgiTraceSmokeCase enclosedMissCase = missCase;
    enclosedMissCase.label = "Enclosed Sky Miss Rejection";
    enclosedMissCase.voxelLight = {0u, 0u, 0u, 255u};
    enclosedMissCase.expectedPixels[0].minimumRadiance = {0.0f, 0.0f, 0.0f};
    enclosedMissCase.expectedPixels[0].maximumRadiance = {0.0f, 0.0f, 0.0f};
    return validateRtgiTraceCase(device, commandPool, sceneTlas, activeTlas, globalBindlessSet, enclosedMissCase) &&
           validateRtgiSignalPackInvalidInputs(device, commandPool);
}

[[nodiscard]] bool validateStaticMeshBlasAndSceneTlas(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    using namespace renderer::contracts;
    using namespace renderer::core;
    using namespace renderer::rt;

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    struct StaticMeshVertex final {
        float position[3];
        float normal[3];
        float tangent[4];
        float uv[2];
    };
    static_assert(sizeof(StaticMeshVertex) == kStaticMeshRayTracingVertexStride);
    const auto makeVertex = [](const float x, const float y, const float z, const float u, const float v) {
        return StaticMeshVertex{{x, y, z}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {u, v}};
    };
    const std::array<StaticMeshVertex, 3u> opaqueVertices{makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
                                                          makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f),
                                                          makeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f)};
    const std::array<StaticMeshVertex, 3u> cutoutVertices{makeVertex(0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
                                                          makeVertex(1.0f, 0.0f, 1.0f, 1.0f, 0.0f),
                                                          makeVertex(0.0f, 1.0f, 1.0f, 0.0f, 1.0f)};
    const std::array<uint32_t, 3u> indices{0u, 1u, 2u};
    const std::array<StaticMeshPrimitiveMetadata, 1u> opaqueMetadata{
        StaticMeshPrimitiveMetadata{0u, 601u, 501u, kStaticMeshRayTracingContractVersion}};
    const std::array<StaticMeshPrimitiveMetadata, 1u> cutoutMetadata{
        StaticMeshPrimitiveMetadata{1u, 602u, 502u, kStaticMeshRayTracingContractVersion}};
    constexpr RhiBufferUsageFlags kVertexUsages =
        rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst) |
        rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
    constexpr RhiBufferUsageFlags kIndexUsages =
        rhiFlag(RhiBufferUsage::Index) | rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst) |
        rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
    constexpr RhiBufferUsageFlags kMetadataUsages = rhiFlag(RhiBufferUsage::Storage) |
                                                    rhiFlag(RhiBufferUsage::TransferDst) |
                                                    rhiFlag(RhiBufferUsage::DeviceAddress);

    const auto createGeometryBuffer = [&](const char* name, const uint64_t size, const RhiBufferUsageFlags usage,
                                          const RhiResourceState state, const void* data) {
        RhiBufferDesc desc;
        desc.debugName = name;
        desc.size = size;
        desc.usage = usage;
        desc.memoryUsage = RhiMemoryUsage::GpuOnly;
        desc.initialState = state;
        desc.memoryCategory = RhiMemoryCategory::Geometry;
        return device.createBuffer(desc, data, static_cast<size_t>(size));
    };

    std::array<RhiBufferHandle, 6u> geometryBuffers{
        createGeometryBuffer("VulkanSmoke.StaticBLAS.OpaqueVertices", sizeof(opaqueVertices), kVertexUsages,
                             RhiResourceState::VertexBuffer, opaqueVertices.data()),
        createGeometryBuffer("VulkanSmoke.StaticBLAS.OpaqueIndices", sizeof(indices), kIndexUsages,
                             RhiResourceState::IndexBuffer, indices.data()),
        createGeometryBuffer("VulkanSmoke.StaticBLAS.OpaqueMetadata", sizeof(opaqueMetadata), kMetadataUsages,
                             RhiResourceState::ShaderRead, opaqueMetadata.data()),
        createGeometryBuffer("VulkanSmoke.StaticBLAS.CutoutVertices", sizeof(cutoutVertices), kVertexUsages,
                             RhiResourceState::VertexBuffer, cutoutVertices.data()),
        createGeometryBuffer("VulkanSmoke.StaticBLAS.CutoutIndices", sizeof(indices), kIndexUsages,
                             RhiResourceState::IndexBuffer, indices.data()),
        createGeometryBuffer("VulkanSmoke.StaticBLAS.CutoutMetadata", sizeof(cutoutMetadata), kMetadataUsages,
                             RhiResourceState::ShaderRead, cutoutMetadata.data())};

    class StaticMeshBindlessLifetime final : public GlobalBindlessLifetime {
    public:
        StaticMeshBindlessLifetime(VkRhiDevice& owner, std::vector<RhiTextureHandle> textures,
                                   std::vector<RhiTextureViewHandle> views, std::vector<RhiSamplerHandle> samplers)
            : m_owner(owner), m_textures(std::move(textures)), m_views(std::move(views)),
              m_samplers(std::move(samplers)) {}

        ~StaticMeshBindlessLifetime() override {
            for (const RhiTextureViewHandle view : m_views) {
                if (view.isValid()) {
                    m_owner.destroyTextureView(view);
                }
            }
            for (const RhiTextureHandle texture : m_textures) {
                if (texture.isValid()) {
                    m_owner.destroyTexture(texture);
                }
            }
            for (const RhiSamplerHandle sampler : m_samplers) {
                if (sampler.isValid()) {
                    m_owner.destroySampler(sampler);
                }
            }
        }

    private:
        VkRhiDevice& m_owner;
        std::vector<RhiTextureHandle> m_textures;
        std::vector<RhiTextureViewHandle> m_views;
        std::vector<RhiSamplerHandle> m_samplers;
    };

    const auto createTexture2D = [&](const char* debugName, const uint32_t width, const uint32_t height,
                                     const uint8_t* pixels, RhiTextureHandle& texture, RhiTextureViewHandle& view) {
        RhiTextureDesc textureDesc;
        textureDesc.debugName = debugName;
        textureDesc.format = RhiTextureFormat::Rgba8Unorm;
        textureDesc.width = width;
        textureDesc.height = height;
        textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
        textureDesc.memoryCategory = RhiMemoryCategory::Texture;
        RhiTextureInitialData initialData;
        initialData.pixels = pixels;
        initialData.sizeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        initialData.finalState = RhiResourceState::ShaderRead;
        texture = device.createTexture(textureDesc, &initialData);
        if (!texture.isValid()) {
            return false;
        }
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = texture;
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.format = textureDesc.format;
        view = device.createTextureView(viewDesc);
        return view.isValid();
    };

    GlobalBindlessSet globalBindlessSet;
    RhiTextureHandle whiteTexture;
    RhiTextureViewHandle whiteView;
    RhiTextureHandle maskTexture;
    RhiTextureViewHandle maskView;
    RhiSamplerHandle materialSampler;
    std::shared_ptr<StaticMeshBindlessLifetime> bindlessLifetime;
    const std::array<uint8_t, 4u> whitePixel{255u, 255u, 255u, 255u};
    const std::array<uint8_t, 8u> maskPixels{255u, 255u, 255u, 0u, 255u, 255u, 255u, 255u};
    GlobalBindlessSetConfig bindlessConfig;
    bindlessConfig.sampledTexture2DCapacity = 2u;
    bindlessConfig.sampledTextureCubeCapacity = 1u;
    bindlessConfig.samplerCapacity = 1u;
    bindlessConfig.storageBufferCapacity = 1u;
    bool valid = globalBindlessSet.initialize(device, bindlessConfig) == GlobalBindlessSetError::None &&
                 createTexture2D("VulkanSmoke.StaticMesh.White", 1u, 1u, whitePixel.data(), whiteTexture, whiteView) &&
                 createTexture2D("VulkanSmoke.StaticMesh.Mask", 2u, 1u, maskPixels.data(), maskTexture, maskView);
    if (valid) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = RhiFilter::Nearest;
        samplerDesc.magFilter = RhiFilter::Nearest;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = RhiAddressMode::ClampToEdge;
        samplerDesc.addressV = RhiAddressMode::ClampToEdge;
        samplerDesc.addressW = RhiAddressMode::ClampToEdge;
        materialSampler = device.createSampler(samplerDesc);
        valid = materialSampler.isValid();
    }
    bindlessLifetime = std::make_shared<StaticMeshBindlessLifetime>(
        device, std::vector<RhiTextureHandle>{whiteTexture, maskTexture},
        std::vector<RhiTextureViewHandle>{whiteView, maskView}, std::vector<RhiSamplerHandle>{materialSampler});
    if (valid) {
        valid = globalBindlessSet.retainLifetime(bindlessLifetime) == GlobalBindlessSetError::None;
    }
    const auto whitePublication =
        valid ? globalBindlessSet.publishTexture2D(whiteView) : GlobalBindlessPublicationResult<BindlessTexture2DTag>{};
    const auto maskPublication =
        valid ? globalBindlessSet.publishTexture2D(maskView) : GlobalBindlessPublicationResult<BindlessTexture2DTag>{};
    const auto samplerPublication = valid ? globalBindlessSet.publishSampler(materialSampler)
                                          : GlobalBindlessPublicationResult<BindlessSamplerTag>{};
    valid = valid && whitePublication.succeeded() && maskPublication.succeeded() && samplerPublication.succeeded();

    std::vector<GpuMaterial> gpuMaterials(2u);
    for (GpuMaterial& material : gpuMaterials) {
        material.materialFactors.x = 0.0f;
        material.materialFactors.z = 0.0f;
    }
    gpuMaterials[0].emissiveFactorAndStrength = {1.0f, 0.0f, 0.0f, 1.0f};
    gpuMaterials[1].baseColorFactor = {0.0f, 1.0f, 0.0f, 1.0f};
    gpuMaterials[1].modesAndFlags.x = static_cast<uint32_t>(GpuMaterialAlphaMode::Mask);
    gpuMaterials[1].transmissionVolumeFactors.z = 0.5f;
    if (valid) {
        for (GpuMaterial& material : gpuMaterials) {
            for (size_t semantic = 0u; semantic < kGpuMaterialTextureSemanticCount; ++semantic) {
                material.textureIndices[semantic / 4u][semantic % 4u] = whitePublication.handle.index;
                material.samplerIndices[semantic / 4u][semantic % 4u] = samplerPublication.handle.index;
            }
        }
        gpuMaterials[1].textureIndices[static_cast<size_t>(GpuMaterialTextureSemantic::BaseColor) / 4u]
                                      [static_cast<size_t>(GpuMaterialTextureSemantic::BaseColor) % 4u] =
            maskPublication.handle.index;
    }

    StaticMeshBlasCache staticBlas;
    SceneTlasCache sceneTlas;
    bool staticBlasOwnsGeometry = false;
    SceneBlasResourcePtr sharedBlas;
    StaticMeshRayTracingResourcePtr rayTracingResource;
    const auto cleanup = [&]() {
        device.waitIdle();
        sceneTlas.shutdown();
        rayTracingResource.reset();
        sharedBlas.reset();
        staticBlas.shutdown();
        globalBindlessSet.shutdown();
        bindlessLifetime.reset();
        if (!staticBlasOwnsGeometry) {
            for (const RhiBufferHandle buffer : geometryBuffers) {
                if (buffer.isValid()) {
                    device.destroyBuffer(buffer);
                }
            }
        }
    };

    valid = valid &&
            std::all_of(geometryBuffers.begin(), geometryBuffers.end(),
                        [](const RhiBufferHandle buffer) { return buffer.isValid(); }) &&
            staticBlas.init(&device) && staticBlas.supported();
    if (valid) {
        StaticMeshBlasGeometry opaqueGeometry;
        opaqueGeometry.geometryId = StableGeometryId{501u};
        opaqueGeometry.materialId = StableMaterialId{601u};
        opaqueGeometry.vertexBuffer = geometryBuffers[0];
        opaqueGeometry.indexBuffer = geometryBuffers[1];
        opaqueGeometry.primitiveMetadataBuffer = geometryBuffers[2];
        opaqueGeometry.vertexStride = sizeof(StaticMeshVertex);
        opaqueGeometry.vertexCount = 3u;
        opaqueGeometry.indexCount = 3u;
        opaqueGeometry.materialIndex = 0u;
        opaqueGeometry.localBoundsMin = {0.0f, 0.0f, 0.0f};
        opaqueGeometry.localBoundsMax = {1.0f, 1.0f, 0.0f};
        opaqueGeometry.opaque = true;
        StaticMeshBlasGeometry cutoutGeometry;
        cutoutGeometry.geometryId = StableGeometryId{502u};
        cutoutGeometry.materialId = StableMaterialId{602u};
        cutoutGeometry.vertexBuffer = geometryBuffers[3];
        cutoutGeometry.indexBuffer = geometryBuffers[4];
        cutoutGeometry.primitiveMetadataBuffer = geometryBuffers[5];
        cutoutGeometry.vertexStride = sizeof(StaticMeshVertex);
        cutoutGeometry.vertexCount = 3u;
        cutoutGeometry.indexCount = 3u;
        cutoutGeometry.materialIndex = 1u;
        cutoutGeometry.localBoundsMin = {0.0f, 0.0f, 1.0f};
        cutoutGeometry.localBoundsMax = {1.0f, 1.0f, 1.0f};
        cutoutGeometry.opaque = false;
        cutoutGeometry.doubleSided = true;
        const std::vector<StaticMeshBlasGeometry> geometries{opaqueGeometry, cutoutGeometry};
        valid = staticBlas.build(commandPool, geometries) == StaticMeshBlasBuildResult::Built;
        staticBlasOwnsGeometry = valid;
    }
    if (valid) {
        sharedBlas = staticBlas.resource();
        const StaticMeshBlasStats& stats = staticBlas.stats();
        valid = sharedBlas != nullptr && stats.resident && stats.geometryCount == 2u && stats.primitiveCount == 2u &&
                stats.containsOpaque && stats.containsCutout && stats.containsDoubleSided &&
                stats.compactedBlasBytes != 0u && stats.compactedBlasBytes <= stats.uncompactedBlasBytes;
    }
    if (valid) {
        std::vector<GpuMaterial> invalidOpaqueMaterials = gpuMaterials;
        invalidOpaqueMaterials[0].modesAndFlags.x = static_cast<uint32_t>(GpuMaterialAlphaMode::Mask);
        std::vector<GpuMaterial> invalidCutoutMaterials = gpuMaterials;
        invalidCutoutMaterials[1].modesAndFlags.x = static_cast<uint32_t>(GpuMaterialAlphaMode::Opaque);
        const std::vector<StableMaterialId> materialIds{StableMaterialId{601u}, StableMaterialId{602u}};
        valid = StaticMeshRayTracingResource::create(globalBindlessSet.identity(), bindlessLifetime,
                                                     std::move(invalidOpaqueMaterials), materialIds,
                                                     staticBlas.rayTracingGeometries()) == nullptr &&
                StaticMeshRayTracingResource::create(globalBindlessSet.identity(), bindlessLifetime,
                                                     std::move(invalidCutoutMaterials), materialIds,
                                                     staticBlas.rayTracingGeometries()) == nullptr;
    }
    if (valid) {
        rayTracingResource = StaticMeshRayTracingResource::create(
            globalBindlessSet.identity(), bindlessLifetime, gpuMaterials,
            std::vector<StableMaterialId>{StableMaterialId{601u}, StableMaterialId{602u}},
            staticBlas.rayTracingGeometries());
        valid = rayTracingResource != nullptr && sceneTlas.init(&device) && sceneTlas.supported();
    }

    const auto makeInstances = [&](const float secondTranslation) {
        const uint8_t mask = sceneTlasMaskBit(SceneTlasInstanceMask::GiOpaque) |
                             sceneTlasMaskBit(SceneTlasInstanceMask::GiCutout) |
                             sceneTlasMaskBit(SceneTlasInstanceMask::ShadowCaster) |
                             sceneTlasMaskBit(SceneTlasInstanceMask::ReflectionVisible);
        std::vector<SceneTlasInstanceInput> instances;
        instances.push_back({{SceneTlasInstanceKind::StaticMesh, 42, 0},
                             sharedBlas,
                             glm::translate(glm::mat4(1.0f), glm::vec3(secondTranslation, 0.0f, 0.0f)),
                             mask,
                             true,
                             std::nullopt,
                             rayTracingResource});
        instances.push_back({{SceneTlasInstanceKind::StaticMesh, 7, 0},
                             sharedBlas,
                             glm::mat4(1.0f),
                             mask,
                             true,
                             std::nullopt,
                             rayTracingResource});
        return instances;
    };
    const auto expectedGpuSceneInstances = [&](const float secondTranslation) {
        std::array<GpuSceneInstance, 2u> instances;
        const std::array<glm::mat4, 2u> transforms{
            glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(secondTranslation, 0.0f, 0.0f))};
        const std::array<uint32_t, 2u> stableObjectIds{7u, 42u};
        const GpuSceneInstanceFlags flags = gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::Enabled) |
                                            gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::ShadowCaster) |
                                            gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::ReflectionVisible) |
                                            gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::RayTracingVisible);
        for (uint32_t index = 0u; index < instances.size(); ++index) {
            const glm::vec3 center = glm::vec3(transforms[index] * glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            GpuSceneInstanceNormalizationInput input;
            input.worldFromObject = transforms[index];
            input.previousWorldFromObject = transforms[index];
            input.worldBoundsCenterAndRadius = glm::vec4(center, glm::length(glm::vec3(0.5f)));
            input.geometryCount = 2u;
            input.flags = flags;
            input.stableObjectId = StableObjectId{stableObjectIds[index]};
            input.rayTracingInstanceId = index;
            const GpuSceneInstanceNormalizationResult normalized = normalizeGpuSceneInstance(input);
            if (!normalized.succeeded()) {
                return std::optional<std::array<GpuSceneInstance, 2u>>{};
            }
            instances[index] = normalized.instance;
        }
        return std::optional<std::array<GpuSceneInstance, 2u>>{instances};
    };
    const auto submitTlasFrame = [&](const char* debugName) {
        sceneTlas.beginFrame();
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
        if (commands == nullptr || !commands->begin({debugName, RhiCommandListType::Graphics}) ||
            !sceneTlas.recordFrame(*commands) || !commands->end()) {
            sceneTlas.finishGraphExecution(false, {});
            return false;
        }
        RhiCommandList* submissions[] = {commands};
        RhiSubmissionToken token;
        if (!device.submit({debugName, submissions, 1u, RhiQueueType::Graphics}, &token)) {
            sceneTlas.finishGraphExecution(false, token);
            return false;
        }
        sceneTlas.finishGraphExecution(true, token);
        if (!device.waitForSubmission(token)) {
            return false;
        }
        sceneTlas.beginFrame();
        return sceneTlas.healthy();
    };
    const auto submitPartiallyFailedTlasFrame = [&](const char* debugName) {
        sceneTlas.beginFrame();
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
        if (commands == nullptr || !commands->begin({debugName, RhiCommandListType::Graphics}) ||
            !sceneTlas.recordFrame(*commands) || !commands->end()) {
            sceneTlas.finishGraphExecution(false, {});
            return false;
        }
        RhiCommandList* submissions[] = {commands};
        RhiSubmissionToken token;
        if (!device.submit({debugName, submissions, 1u, RhiQueueType::Graphics}, &token)) {
            sceneTlas.finishGraphExecution(false, token);
            return false;
        }
        sceneTlas.finishGraphExecution(false, token);
        if (!device.waitForSubmission(token)) {
            return false;
        }
        sceneTlas.beginFrame();
        return sceneTlas.healthy();
    };

    if (valid) {
        valid = sceneTlas.setInstances(makeInstances(3.0f)) == SceneTlasSetResult::Accepted &&
                submitTlasFrame("VulkanSmoke.SceneTLAS.Build1");
    }
    const std::optional<SceneTlasView> firstTlas = valid ? sceneTlas.activeView() : std::nullopt;
    if (valid) {
        const SceneTlasStats stats = sceneTlas.stats();
        const std::array<renderer::contracts::TerrainRayTracingGpuInstance, 2u> emptyTerrainHitData{};
        const std::array<GpuSceneGeometry, 2u> expectedGeometries{rayTracingResource->geometries()[0].gpu,
                                                                  rayTracingResource->geometries()[1].gpu};
        const std::optional<std::array<GpuSceneInstance, 2u>> expectedInstances = expectedGpuSceneInstances(3.0f);
        valid =
            firstTlas.has_value() && firstTlas->deviceAddress != 0u && firstTlas->instanceCount == 2u &&
            firstTlas->blasCount == 1u && firstTlas->bindlessIdentity == globalBindlessSet.identity() &&
            firstTlas->gpuSceneMaterialCount == gpuMaterials.size() &&
            firstTlas->gpuSceneGeometryCount == expectedGeometries.size() &&
            firstTlas->instanceBytes == 2u * sizeof(RhiAccelerationStructureInstance) &&
            firstTlas->terrainHitDataBuffer.isValid() &&
            firstTlas->terrainHitDataBytes == sizeof(emptyTerrainHitData) &&
            firstTlas->gpuSceneMaterialBytes == sizeof(GpuMaterial) * gpuMaterials.size() &&
            firstTlas->gpuSceneGeometryBytes == sizeof(expectedGeometries) &&
            firstTlas->gpuSceneInstanceBytes == sizeof(GpuSceneInstance) * 2u &&
            firstTlas->blasBytes == sharedBlas->blasBytes() && firstTlas->mappings.size() == 2u &&
            firstTlas->mappings[0].customIndex == 0u && firstTlas->mappings[0].key.primary == 7 &&
            firstTlas->mappings[0].staticMeshHitData == rayTracingResource &&
            firstTlas->mappings[1].customIndex == 1u && firstTlas->mappings[1].key.primary == 42 &&
            firstTlas->mappings[1].staticMeshHitData == rayTracingResource && stats.activeBlasCount == 1u &&
            stats.activeInstanceBytes == firstTlas->instanceBytes &&
            stats.activeTerrainHitDataBytes == firstTlas->terrainHitDataBytes &&
            stats.activeGpuSceneMaterialBytes == firstTlas->gpuSceneMaterialBytes &&
            stats.activeGpuSceneGeometryBytes == firstTlas->gpuSceneGeometryBytes &&
            stats.activeGpuSceneInstanceBytes == firstTlas->gpuSceneInstanceBytes &&
            stats.activeBlasBytes == firstTlas->blasBytes && sceneTlas.isSettled() && expectedInstances.has_value() &&
            validateGpuBufferContents(device, commandPool, firstTlas->terrainHitDataBuffer,
                                      RhiResourceState::StorageBuffer, emptyTerrainHitData.data(),
                                      sizeof(emptyTerrainHitData), "VulkanSmoke.SceneTLAS.TerrainHitDataReadback") &&
            validateGpuBufferContents(device, commandPool, firstTlas->gpuSceneMaterialBuffer,
                                      RhiResourceState::StorageBuffer, gpuMaterials.data(),
                                      firstTlas->gpuSceneMaterialBytes,
                                      "VulkanSmoke.SceneTLAS.GpuSceneMaterialReadback") &&
            validateGpuBufferContents(device, commandPool, firstTlas->gpuSceneGeometryBuffer,
                                      RhiResourceState::StorageBuffer, expectedGeometries.data(),
                                      firstTlas->gpuSceneGeometryBytes,
                                      "VulkanSmoke.SceneTLAS.GpuSceneGeometryReadback") &&
            validateGpuBufferContents(device, commandPool, firstTlas->gpuSceneInstanceBuffer,
                                      RhiResourceState::StorageBuffer, expectedInstances->data(),
                                      firstTlas->gpuSceneInstanceBytes,
                                      "VulkanSmoke.SceneTLAS.GpuSceneInstanceReadback");
    }
    if (valid) {
        valid = validateCutoutRayQuery(device, commandPool, firstTlas->accelerationStructure);
    }
    if (valid) {
        valid = validateRtgiTracePass(device, commandPool, sceneTlas, *firstTlas, globalBindlessSet);
    }
    if (valid) {
        valid = sceneTlas.setInstances(makeInstances(6.0f)) == SceneTlasSetResult::Accepted &&
                submitPartiallyFailedTlasFrame("VulkanSmoke.SceneTLAS.PartialFailure");
    }
    if (valid) {
        const std::optional<SceneTlasView> retainedTlas = sceneTlas.activeView();
        valid = retainedTlas.has_value() && retainedTlas->revision == firstTlas->revision &&
                !sceneTlas.stats().pending && !sceneTlas.isSettled() &&
                submitTlasFrame("VulkanSmoke.SceneTLAS.Build2Retry");
    }
    const std::optional<SceneTlasView> secondTlas = valid ? sceneTlas.activeView() : std::nullopt;
    if (valid) {
        const std::optional<std::array<GpuSceneInstance, 2u>> expectedInstances = expectedGpuSceneInstances(6.0f);
        valid = secondTlas.has_value() && secondTlas->revision > firstTlas->revision &&
                secondTlas->deviceAddress != 0u && secondTlas->instanceCount == 2u &&
                secondTlas->gpuSceneMaterialCount == 2u && secondTlas->gpuSceneGeometryCount == 2u &&
                expectedInstances.has_value() &&
                validateGpuBufferContents(device, commandPool, secondTlas->gpuSceneInstanceBuffer,
                                          RhiResourceState::StorageBuffer, expectedInstances->data(),
                                          secondTlas->gpuSceneInstanceBytes,
                                          "VulkanSmoke.SceneTLAS.GpuSceneInstanceReplacementReadback") &&
                sceneTlas.isSettled() && sceneTlas.setInstances({}) == SceneTlasSetResult::Accepted &&
                !sceneTlas.activeView().has_value() && sceneTlas.isSettled();
    }

    cleanup();
    valid = valid && device.validationErrorCount() == validationErrorsBefore;
    if (!valid) {
        std::cerr << "Static mesh BLAS sharing or Scene TLAS generation validation failed\n";
    }
    return valid;
}

[[nodiscard]] bool validateCubeArrayViews(VkRhiDevice& device) {
    const uint64_t validationErrorsBefore = device.validationErrorCount();
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.CubeArray";
    textureDesc.dimension = RhiTextureDimension::CubeArray;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 4u;
    textureDesc.height = 4u;
    textureDesc.depthOrLayers = 12u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc cubeArrayDesc;
    cubeArrayDesc.texture = texture;
    cubeArrayDesc.viewType = RhiTextureViewType::CubeArray;
    cubeArrayDesc.layerCount = 12u;
    const RhiTextureViewHandle cubeArrayView = device.createTextureView(cubeArrayDesc);

    RhiTextureViewDesc cubeDesc;
    cubeDesc.texture = texture;
    cubeDesc.viewType = RhiTextureViewType::Cube;
    cubeDesc.baseLayer = 6u;
    cubeDesc.layerCount = 6u;
    const RhiTextureViewHandle cubeView = device.createTextureView(cubeDesc);

    RhiTextureViewDesc faceDesc;
    faceDesc.texture = texture;
    faceDesc.viewType = RhiTextureViewType::Texture2D;
    faceDesc.baseLayer = 7u;
    faceDesc.layerCount = 1u;
    const RhiTextureViewHandle faceView = device.createTextureView(faceDesc);

    RhiTextureDesc invalidTextureDesc = textureDesc;
    invalidTextureDesc.depthOrLayers = 10u;
    const RhiTextureHandle invalidTexture = device.createTexture(invalidTextureDesc, nullptr);
    RhiTextureViewDesc invalidViewDesc = cubeArrayDesc;
    invalidViewDesc.baseLayer = 1u;
    invalidViewDesc.layerCount = 6u;
    const RhiTextureViewHandle invalidView = device.createTextureView(invalidViewDesc);

    const bool valid = cubeArrayView.isValid() && cubeView.isValid() && faceView.isValid() &&
                       !invalidTexture.isValid() && !invalidView.isValid();
    if (faceView.isValid())
        device.destroyTextureView(faceView);
    if (cubeView.isValid())
        device.destroyTextureView(cubeView);
    if (cubeArrayView.isValid())
        device.destroyTextureView(cubeArrayView);
    device.destroyTexture(texture);
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateCubeArrayCaptureOrientation(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr uint32_t kFaceExtent = 32u;
    constexpr uint32_t kOutputWidth = 2u;
    constexpr uint32_t kOutputHeight = 1u;
    constexpr uint32_t kBytesPerPixel = 4u;
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiTextureDesc cubeDesc;
    cubeDesc.debugName = "VulkanSmoke.CubeArrayOrientation.Source";
    cubeDesc.dimension = RhiTextureDimension::CubeArray;
    cubeDesc.format = RhiTextureFormat::Rgba8Unorm;
    cubeDesc.width = kFaceExtent;
    cubeDesc.height = kFaceExtent;
    cubeDesc.depthOrLayers = renderer::contracts::kCubeMapFaceCount;
    cubeDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment);
    cubeDesc.memoryCategory = RhiMemoryCategory::SceneData;
    const RhiTextureHandle cubeTexture = device.createTexture(cubeDesc, nullptr);

    RhiTextureViewDesc cubeArrayViewDesc;
    cubeArrayViewDesc.texture = cubeTexture;
    cubeArrayViewDesc.viewType = RhiTextureViewType::CubeArray;
    cubeArrayViewDesc.format = cubeDesc.format;
    cubeArrayViewDesc.layerCount = renderer::contracts::kCubeMapFaceCount;
    const RhiTextureViewHandle cubeArrayView = device.createTextureView(cubeArrayViewDesc);

    RhiTextureViewDesc faceViewDesc;
    faceViewDesc.texture = cubeTexture;
    faceViewDesc.viewType = RhiTextureViewType::Texture2D;
    faceViewDesc.format = cubeDesc.format;
    faceViewDesc.baseLayer = 0u;
    faceViewDesc.layerCount = 1u;
    const RhiTextureViewHandle faceView = device.createTextureView(faceViewDesc);

    RhiTextureDesc outputDesc;
    outputDesc.debugName = "VulkanSmoke.CubeArrayOrientation.Output";
    outputDesc.format = RhiTextureFormat::Rgba8Unorm;
    outputDesc.width = kOutputWidth;
    outputDesc.height = kOutputHeight;
    outputDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    outputDesc.memoryCategory = RhiMemoryCategory::Readback;
    const RhiTextureHandle outputTexture = device.createTexture(outputDesc, nullptr);

    RhiTextureViewDesc outputViewDesc;
    outputViewDesc.texture = outputTexture;
    outputViewDesc.viewType = RhiTextureViewType::Texture2D;
    outputViewDesc.format = outputDesc.format;
    const RhiTextureViewHandle outputView = device.createTextureView(outputViewDesc);

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Nearest;
    samplerDesc.magFilter = RhiFilter::Nearest;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    const RhiSamplerHandle sampler = device.createSampler(samplerDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "VulkanSmoke.CubeArrayOrientation.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back(
        {0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u, 0u});
    const RhiBindGroupLayoutHandle bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = bindGroupLayout;
    RhiBindGroupEntry bindGroupEntry;
    bindGroupEntry.binding = 0u;
    bindGroupEntry.resource.combinedTextureSampler = {cubeArrayView, sampler};
    bindGroupDesc.entries.push_back(bindGroupEntry);
    const RhiBindGroupHandle bindGroup = device.createBindGroup(bindGroupDesc);

    constexpr char kFaceVertexSource[] = R"glsl(
#version 450 core
layout(push_constant) uniform FacePushConstants {
    mat4 viewProjection;
};
const vec3 positions[6] = vec3[](
    vec3(1.0, 0.35, -0.25), vec3(1.0, 0.75, -0.25), vec3(1.0, 0.75, 0.25),
    vec3(1.0, 0.35, -0.25), vec3(1.0, 0.75, 0.25), vec3(1.0, 0.35, 0.25)
);
void main() {
    gl_Position = viewProjection * vec4(positions[gl_VertexIndex], 1.0);
}
)glsl";
    constexpr char kFaceFragmentSource[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)glsl";
    constexpr char kSampleVertexSource[] = R"glsl(
#version 450 core
void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";
    constexpr char kSampleFragmentSource[] = R"glsl(
#version 450 core
layout(set = 0, binding = 0) uniform samplerCubeArray cubeArray;
layout(location = 0) out vec4 outColor;
void main() {
    const bool upperDirection = gl_FragCoord.x < 1.0;
    const vec3 direction = normalize(vec3(1.0, upperDirection ? 0.60 : -0.60, 0.0));
    outColor = textureLod(cubeArray, vec4(direction, 0.0), 0.0);
}
)glsl";

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.CubeArrayOrientation.FaceVertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kFaceVertexSource;
    shaderDesc.sourceSize = sizeof(kFaceVertexSource) - 1u;
    const RhiShaderHandle faceVertexShader = device.createShader(shaderDesc);
    shaderDesc.debugName = "VulkanSmoke.CubeArrayOrientation.FaceFragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = kFaceFragmentSource;
    shaderDesc.sourceSize = sizeof(kFaceFragmentSource) - 1u;
    const RhiShaderHandle faceFragmentShader = device.createShader(shaderDesc);
    shaderDesc.debugName = "VulkanSmoke.CubeArrayOrientation.SampleVertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kSampleVertexSource;
    shaderDesc.sourceSize = sizeof(kSampleVertexSource) - 1u;
    const RhiShaderHandle sampleVertexShader = device.createShader(shaderDesc);
    shaderDesc.debugName = "VulkanSmoke.CubeArrayOrientation.SampleFragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = kSampleFragmentSource;
    shaderDesc.sourceSize = sizeof(kSampleFragmentSource) - 1u;
    const RhiShaderHandle sampleFragmentShader = device.createShader(shaderDesc);

    RhiPipelineLayoutDesc faceLayoutDesc;
    faceLayoutDesc.debugName = "VulkanSmoke.CubeArrayOrientation.FaceLayout";
    faceLayoutDesc.pushConstantBytes = sizeof(glm::mat4);
    faceLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    const RhiPipelineLayoutHandle faceLayout = device.createPipelineLayout(faceLayoutDesc);

    RhiPipelineLayoutDesc sampleLayoutDesc;
    sampleLayoutDesc.debugName = "VulkanSmoke.CubeArrayOrientation.SampleLayout";
    sampleLayoutDesc.bindGroupLayouts.push_back(bindGroupLayout);
    const RhiPipelineLayoutHandle sampleLayout = device.createPipelineLayout(sampleLayoutDesc);

    RhiGraphicsPipelineDesc facePipelineDesc;
    facePipelineDesc.debugName = "VulkanSmoke.CubeArrayOrientation.FacePipeline";
    facePipelineDesc.vertexShader = faceVertexShader;
    facePipelineDesc.fragmentShader = faceFragmentShader;
    facePipelineDesc.layout = faceLayout;
    facePipelineDesc.raster.cullMode = RhiCullMode::Back;
    facePipelineDesc.depthStencil.depthTestEnabled = false;
    facePipelineDesc.depthStencil.depthWriteEnabled = false;
    facePipelineDesc.colorFormats = {cubeDesc.format};
    const RhiPipelineHandle facePipeline = device.createGraphicsPipeline(facePipelineDesc);

    RhiGraphicsPipelineDesc samplePipelineDesc;
    samplePipelineDesc.debugName = "VulkanSmoke.CubeArrayOrientation.SamplePipeline";
    samplePipelineDesc.vertexShader = sampleVertexShader;
    samplePipelineDesc.fragmentShader = sampleFragmentShader;
    samplePipelineDesc.layout = sampleLayout;
    samplePipelineDesc.raster.cullMode = RhiCullMode::None;
    samplePipelineDesc.depthStencil.depthTestEnabled = false;
    samplePipelineDesc.depthStencil.depthWriteEnabled = false;
    samplePipelineDesc.colorFormats = {outputDesc.format};
    const RhiPipelineHandle samplePipeline = device.createGraphicsPipeline(samplePipelineDesc);

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.CubeArrayOrientation.Readback";
    readbackDesc.size = static_cast<uint64_t>(kOutputWidth) * kOutputHeight * kBytesPerPixel;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);

    bool valid = cubeTexture.isValid() && cubeArrayView.isValid() && faceView.isValid() && outputTexture.isValid() &&
                 outputView.isValid() && sampler.isValid() && bindGroupLayout.isValid() && bindGroup.isValid() &&
                 faceVertexShader.isValid() && faceFragmentShader.isValid() && sampleVertexShader.isValid() &&
                 sampleFragmentShader.isValid() && faceLayout.isValid() && sampleLayout.isValid() &&
                 facePipeline.isValid() && samplePipeline.isValid() && readback.isValid();

    if (valid) {
        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
        valid = commands != nullptr && commands->begin({"VulkanSmoke.CubeArrayOrientation.Commands",
                                                         RhiCommandListType::Graphics});
        if (valid) {
            commands->textureBarrier({cubeTexture, RhiResourceState::Undefined, RhiResourceState::RenderTarget});
            RhiColorAttachment faceAttachment;
            faceAttachment.view = faceView;
            faceAttachment.loadOp = RhiLoadOp::Clear;
            faceAttachment.storeOp = RhiStoreOp::Store;
            faceAttachment.clearColor[0] = 0.0f;
            faceAttachment.clearColor[1] = 0.0f;
            faceAttachment.clearColor[2] = 1.0f;
            faceAttachment.clearColor[3] = 1.0f;
            RhiRenderingInfo faceRendering;
            faceRendering.debugName = "VulkanSmoke.CubeArrayOrientation.FaceRendering";
            faceRendering.renderArea = {0, 0, kFaceExtent, kFaceExtent};
            faceRendering.colorAttachments = &faceAttachment;
            faceRendering.colorAttachmentCount = 1u;
            commands->beginRendering(faceRendering);
            commands->setGraphicsPipeline(facePipeline);
            const glm::mat4 viewProjection = renderer::contracts::cubeMapFaceViewProjection(
                glm::vec3(0.0f), 0u, 0.05f, 4.0f);
            commands->pushConstants(&viewProjection, sizeof(viewProjection), rhiFlag(RhiShaderStage::Vertex));
            commands->draw(6u, 1u, 0u, 0u);
            commands->endRendering();
            commands->textureBarrier({cubeTexture, RhiResourceState::RenderTarget, RhiResourceState::ShaderRead});

            commands->textureBarrier({outputTexture, RhiResourceState::Undefined, RhiResourceState::RenderTarget});
            RhiColorAttachment outputAttachment;
            outputAttachment.view = outputView;
            outputAttachment.loadOp = RhiLoadOp::Clear;
            outputAttachment.storeOp = RhiStoreOp::Store;
            RhiRenderingInfo outputRendering;
            outputRendering.debugName = "VulkanSmoke.CubeArrayOrientation.SampleRendering";
            outputRendering.renderArea = {0, 0, kOutputWidth, kOutputHeight};
            outputRendering.colorAttachments = &outputAttachment;
            outputRendering.colorAttachmentCount = 1u;
            commands->beginRendering(outputRendering);
            commands->setGraphicsPipeline(samplePipeline);
            commands->setBindGroup(0u, bindGroup);
            commands->draw(3u, 1u, 0u, 0u);
            commands->endRendering();
            commands->textureBarrier({outputTexture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});

            RhiTextureBufferCopy copy;
            copy.srcTexture = outputTexture;
            copy.dstBuffer = readback;
            copy.bytesPerRow = static_cast<uint64_t>(kOutputWidth) * kBytesPerPixel;
            copy.rowsPerImage = kOutputHeight;
            copy.width = kOutputWidth;
            copy.height = kOutputHeight;
            commands->copyTextureToBuffer(copy);
            commands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
            valid = commands->end();
        }
        if (valid) {
            RhiCommandList* submissions[] = {commands};
            RhiSubmissionToken token;
            valid = device.submit({"VulkanSmoke.CubeArrayOrientation.Submit", submissions, 1u}, &token) &&
                    device.waitForSubmission(token);
        }
    }
    if (valid) {
        const auto* pixels = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, readbackDesc.size));
        const auto isRed = [](const uint8_t* pixel) {
            return pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
        };
        const auto isBlue = [](const uint8_t* pixel) {
            return pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
        };
        valid = pixels != nullptr && isRed(pixels) && isBlue(pixels + kBytesPerPixel);
        if (pixels != nullptr) {
            device.unmapBuffer(readback);
        }
    }

    if (readback.isValid())
        device.destroyBuffer(readback);
    if (samplePipeline.isValid())
        device.destroyPipeline(samplePipeline);
    if (facePipeline.isValid())
        device.destroyPipeline(facePipeline);
    if (sampleLayout.isValid())
        device.destroyPipelineLayout(sampleLayout);
    if (faceLayout.isValid())
        device.destroyPipelineLayout(faceLayout);
    if (sampleFragmentShader.isValid())
        device.destroyShader(sampleFragmentShader);
    if (sampleVertexShader.isValid())
        device.destroyShader(sampleVertexShader);
    if (faceFragmentShader.isValid())
        device.destroyShader(faceFragmentShader);
    if (faceVertexShader.isValid())
        device.destroyShader(faceVertexShader);
    if (bindGroup.isValid())
        device.destroyBindGroup(bindGroup);
    if (bindGroupLayout.isValid())
        device.destroyBindGroupLayout(bindGroupLayout);
    if (sampler.isValid())
        device.destroySampler(sampler);
    if (outputView.isValid())
        device.destroyTextureView(outputView);
    if (outputTexture.isValid())
        device.destroyTexture(outputTexture);
    if (faceView.isValid())
        device.destroyTextureView(faceView);
    if (cubeArrayView.isValid())
        device.destroyTextureView(cubeArrayView);
    if (cubeTexture.isValid())
        device.destroyTexture(cubeTexture);
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

int main() {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (!streamline.initialize()) {
        std::cerr << streamline.lastError() << '\n';
        return 1;
    }
#endif
    if (glfwInit() != GLFW_TRUE) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        streamline.shutdown();
#endif
        return 1;
    }

    VkRhiDevice device;
    if (!device.prepareWindowCreation()) {
        glfwTerminate();
#if defined(MECRAFT_ENABLE_STREAMLINE)
        streamline.shutdown();
#endif
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(320, 240, "vulkan_rhi_smoke_test", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "vulkan_rhi_smoke_test: window creation failed\n";
        glfwTerminate();
#if defined(MECRAFT_ENABLE_STREAMLINE)
        streamline.shutdown();
#endif
        return 1;
    }
    glfwShowWindow(window);
    glfwWaitEventsTimeout(0.1);

    RhiDeviceDesc desc;
    desc.debugName = "VulkanRhiSmokeTest";
    desc.nativeWindow = window;
    desc.width = 320;
    desc.height = 240;
    desc.enableDebugMarkers = true;
    desc.enableDebugOutput = true;
    if (!device.init(desc)) {
        std::cerr << "vulkan_rhi_smoke_test: device init failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.capabilities().vulkanApiVersion < VK_API_VERSION_1_3 ||
        !device.capabilities().shaderDemoteToHelperInvocation) {
        std::cerr << "vulkan_rhi_smoke_test: Vulkan 1.3 shader target features are unavailable\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.swapchainColorFormat() != RhiTextureFormat::Bgra8Unorm ||
        !validateResourceDescriptionNameOwnership(device)) {
        std::cerr << "vulkan_rhi_smoke_test: swapchain format or resource "
                     "name ownership check failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!validateTextureAliasing(device) || !validateCubeArrayViews(device)) {
        std::cerr << "vulkan_rhi_smoke_test: texture aliasing or cube-array check failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!device.vsyncEnabled() || device.capabilities().swapchainPresentMode != RhiPresentMode::Fifo) {
        std::cerr << "vulkan_rhi_smoke_test: default present mode is not "
                     "vsync Fifo\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.capabilities().vsyncControl) {
        if (!device.setVsyncEnabled(false) || device.vsyncEnabled()) {
            std::cerr << "vulkan_rhi_smoke_test: vsync toggle failed\n";
            device.shutdown();
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
    }
    if (!createDrawParametersShader(device)) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!validateDescriptorArrayContract(device)) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!validateIndependentBlendPipeline(device)) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    constexpr uint32_t textureWidth = 256u;
    constexpr uint32_t textureHeight = 128u;
    constexpr uint32_t textureDepth = 33u;
    std::vector<float> texturePixels(static_cast<size_t>(textureWidth) * textureHeight * textureDepth * 4u, 0.25f);
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.Texture3D";
    textureDesc.dimension = RhiTextureDimension::Texture3D;
    textureDesc.format = RhiTextureFormat::Rgba32Float;
    textureDesc.width = textureWidth;
    textureDesc.height = textureHeight;
    textureDesc.depthOrLayers = textureDepth;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureInitialData textureInitialData;
    textureInitialData.pixels = texturePixels.data();
    textureInitialData.sizeBytes = texturePixels.size() * sizeof(float);
    textureInitialData.layerCount = textureDepth;
    textureInitialData.finalState = RhiResourceState::ShaderRead;
    const RhiTextureHandle texture = device.createTexture(textureDesc, &textureInitialData);
    if (!texture.isValid()) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    RhiTextureViewDesc textureViewDesc;
    textureViewDesc.texture = texture;
    textureViewDesc.viewType = RhiTextureViewType::Texture3D;
    textureViewDesc.format = RhiTextureFormat::Rgba32Float;
    textureViewDesc.baseMip = 0u;
    textureViewDesc.mipCount = 1u;
    textureViewDesc.baseLayer = 0u;
    textureViewDesc.layerCount = textureDepth;
    const RhiTextureViewHandle textureView = device.createTextureView(textureViewDesc);
    if (!textureView.isValid()) {
        device.destroyTexture(texture);
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    std::unique_ptr<RhiCommandListPool> commandPool =
        device.createCommandListPool({"VulkanSmoke.CommandPool", 4u, 64u * 1024u});
    const bool immediateModeValidated =
        !device.capabilities().vsyncControl ||
        (commandPool != nullptr && renderStableFrame(device, *commandPool, window) &&
         device.capabilities().swapchainPresentMode == RhiPresentMode::Immediate && device.setVsyncEnabled(true) &&
         device.vsyncEnabled() && renderStableFrame(device, *commandPool, window) &&
         device.capabilities().swapchainPresentMode == RhiPresentMode::Fifo);
    if (commandPool == nullptr || !immediateModeValidated ||
        (commandPool != nullptr && !validateCubeArrayCaptureOrientation(device, *commandPool)) ||
        !validateRg32UintAttachmentClear(device, *commandPool) ||
#if defined(MECRAFT_ENABLE_FSR31)
        !validateFsr31VulkanDispatch(device, *commandPool) || !validateFsr31VulkanContext(device) ||
#endif
#if defined(MECRAFT_ENABLE_NRD)
        !validateNrdGuidePrepPass(device, *commandPool) || !validateNrdRenderGraphDispatch(device, *commandPool) ||
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
        !validateDlssVulkanDispatch(device, *commandPool, window) ||
        !validateDlssFrameGenerationSwapchainLifecycle(device, *commandPool, window) ||
#endif
        !validateTemporalOutputTarget(device, *commandPool) ||
        !validateBindGroupUpdateLifecycle(device, *commandPool) ||
        !validateGlobalBindlessGpuScene(device, *commandPool) ||
        !validateAccelerationStructures(device, *commandPool) || !validateTerrainBlasCache(device, *commandPool) ||
        !validateStaticMeshBlasAndSceneTlas(device, *commandPool) ||
        !validateVulkanInterop(device, *commandPool, texture, textureView, textureWidth, textureHeight, textureDepth) ||
        !validateOffscreenCoordinateContract(device, *commandPool, window) ||
        !validateRenderGraphMultiQueue(device, *commandPool) ||
        !validateRenderGraphMultithreadedRecord(device, *commandPool) ||
        !validateRenderGraphTextureAliasing(device, *commandPool) ||
        !validateIndependentUiPresentation(device, *commandPool, window) ||
        !rejectDestroyedResourceSubmission(device, *commandPool) || !cancelAcquiredFrame(device, window) ||
        !cancelSubmittedFrame(device, *commandPool, window) || !renderStableFrame(device, *commandPool, window) ||
        !renderStableFrame(device, *commandPool, window)) {
        commandPool.reset();
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwSetWindowSize(window, 480, 320);
    const bool resized = renderStableFrame(device, *commandPool, window);

    device.destroyTextureView(textureView);
    device.destroyTexture(texture);
    commandPool.reset();
    device.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return resized ? 0 : 1;
}
