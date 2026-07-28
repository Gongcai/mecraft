#include "renderer/upscaling/Fsr31VulkanContext.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"
#include "renderer/upscaling/Fsr31TemporalConfig.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {

constexpr size_t kFsrScratchAlignment = 64u;

[[nodiscard]] uint8_t* allocateFsrScratchMemory(const size_t size) {
#if defined(_MSC_VER)
    return static_cast<uint8_t*>(_aligned_malloc(size, kFsrScratchAlignment));
#else
    return static_cast<uint8_t*>(std::aligned_alloc(kFsrScratchAlignment, size));
#endif
}

struct FsrScratchMemoryDeleter {
    void operator()(uint8_t* const memory) const {
#if defined(_MSC_VER)
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    }
};

void receiveFsrMessage(const FfxMsgType type, const wchar_t* const message) {
    if (message == nullptr) {
        return;
    }
    const wchar_t* const level = type == FFX_MESSAGE_TYPE_ERROR
        ? L"error" : L"warning";
    std::wcerr << L"[FSR 3.1 " << level << L"] " << message << L'\n';
}

[[nodiscard]] FfxResource makeFsrResource(
    const VkRhiTextureInteropInfo& resource,
    const FfxResourceUsage usage,
    const FfxResourceStates state,
    const wchar_t* const name) {
    FfxResourceDescription description{};
    description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    description.format = ffxGetSurfaceFormatVK(resource.format);
    description.width = resource.extent.width;
    description.height = resource.extent.height;
    description.depth = 1u;
    description.mipCount = 1u;
    description.flags = FFX_RESOURCE_FLAGS_NONE;
    description.usage = usage;
    return ffxGetResourceVK(
        reinterpret_cast<void*>(resource.image), description, name, state);
}

struct Fsr31SharedTexture {
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
};

[[nodiscard]] bool createFsrSharedTexture(
    VkRhiDevice& device,
    const char* const debugName,
    const RhiTextureFormat format,
    const TemporalExtent extent,
    Fsr31SharedTexture& output) {
    RhiTextureDesc textureDesc;
    textureDesc.debugName = debugName;
    textureDesc.format = format;
    textureDesc.width = extent.width;
    textureDesc.height = extent.height;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::Storage) |
                        rhiFlag(RhiTextureUsage::TransferSrc) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    textureDesc.memoryCategory = RhiMemoryCategory::Sdk;
    output.texture = device.createTexture(textureDesc, nullptr);
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

void destroyFsrSharedTexture(
    VkRhiDevice& device,
    Fsr31SharedTexture& resource) {
    if (resource.view.isValid()) {
        device.destroyTextureView(resource.view);
    }
    if (resource.texture.isValid()) {
        device.destroyTexture(resource.texture);
    }
    resource = {};
}

[[nodiscard]] bool matchesSharedResourceDescription(
    const FfxCreateResourceDescription& description,
    const FfxSurfaceFormat format,
    const TemporalExtent extent) {
    const FfxResourceDescription& resource = description.resourceDescription;
    return resource.type == FFX_RESOURCE_TYPE_TEXTURE2D &&
           resource.format == format && resource.width == extent.width &&
           resource.height == extent.height && resource.depth == 1u &&
           resource.mipCount == 1u &&
           (resource.usage & FFX_RESOURCE_USAGE_UAV) != 0;
}

} // namespace

struct Fsr31VulkanContext::Impl {
    FfxFsr3UpscalerContext context{};
    std::unique_ptr<uint8_t, FsrScratchMemoryDeleter> scratchMemory;
    size_t scratchMemorySize = 0u;
    TemporalExtent maxRenderExtent;
    TemporalExtent maxOutputExtent;
    VkRhiDevice* device = nullptr;
    Fsr31SharedTexture dilatedDepth;
    Fsr31SharedTexture dilatedMotionVectors;
    Fsr31SharedTexture reconstructedPrevNearestDepth;
    bool sharedResourcesInitialized = false;
    bool initialized = false;
};

Fsr31VulkanContext::Fsr31VulkanContext()
    : m_impl(std::make_unique<Impl>()) {}

Fsr31VulkanContext::~Fsr31VulkanContext() {
    if (m_impl->initialized) {
        static_cast<void>(shutdown());
    }
}

Fsr31VulkanContextCreateResult Fsr31VulkanContext::initialize(
    VkRhiDevice& device,
    const Fsr31VulkanContextDesc& desc) {
    if (m_impl->initialized) {
        return {Fsr31VulkanContextCreateStatus::AlreadyInitialized, 0};
    }
    if (!desc.maxRenderExtent.isValid()) {
        return {Fsr31VulkanContextCreateStatus::InvalidRenderExtent, 0};
    }
    if (!desc.maxOutputExtent.isValid()) {
        return {Fsr31VulkanContextCreateStatus::InvalidOutputExtent, 0};
    }
    if (desc.maxRenderExtent.width > desc.maxOutputExtent.width ||
        desc.maxRenderExtent.height > desc.maxOutputExtent.height) {
        return {
            Fsr31VulkanContextCreateStatus::RenderExtentExceedsOutputExtent, 0};
    }

    const auto deviceInfo = VkRhiInterop::deviceInfo(device);
    if (!deviceInfo.has_value()) {
        return {Fsr31VulkanContextCreateStatus::MissingVulkanDevice, 0};
    }
    const size_t scratchMemorySize = ffxGetScratchMemorySizeVK(
        deviceInfo->physicalDevice, FFX_FSR3UPSCALER_CONTEXT_COUNT);
    if (scratchMemorySize == 0u) {
        return {Fsr31VulkanContextCreateStatus::InvalidScratchMemorySize, 0};
    }
    const size_t alignedScratchMemorySize =
        (scratchMemorySize + kFsrScratchAlignment - 1u) &
        ~(kFsrScratchAlignment - 1u);
    m_impl->scratchMemory.reset(
        allocateFsrScratchMemory(alignedScratchMemorySize));
    if (m_impl->scratchMemory == nullptr) {
        return {Fsr31VulkanContextCreateStatus::ScratchMemoryAllocationError, 0};
    }
    std::memset(
        m_impl->scratchMemory.get(), 0, alignedScratchMemorySize);
    m_impl->scratchMemorySize = scratchMemorySize;

    VkDeviceContext deviceContext{
        deviceInfo->device,
        deviceInfo->physicalDevice,
        vkGetDeviceProcAddr
    };
    const FfxDevice ffxDevice = ffxGetDeviceVK(&deviceContext);
    if (ffxDevice == nullptr) {
        m_impl->scratchMemory.reset();
        m_impl->scratchMemorySize = 0u;
        return {Fsr31VulkanContextCreateStatus::MissingVulkanDevice, 0};
    }

    FfxInterface backendInterface{};
    const FfxErrorCode interfaceError = ffxGetInterfaceVK(
        &backendInterface,
        ffxDevice,
        m_impl->scratchMemory.get(),
        m_impl->scratchMemorySize,
        FFX_FSR3UPSCALER_CONTEXT_COUNT);
    if (interfaceError != FFX_OK) {
        m_impl->scratchMemory.reset();
        m_impl->scratchMemorySize = 0u;
        return {
            Fsr31VulkanContextCreateStatus::BackendInterfaceError,
            interfaceError
        };
    }

    FfxFsr3UpscalerContextDescription contextDesc{};
    contextDesc.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
    if (desc.dynamicResolution) {
        contextDesc.flags |= FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION;
    }
    if (desc.debugChecking) {
        contextDesc.flags |= FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
        contextDesc.fpMessage = receiveFsrMessage;
    }
    contextDesc.maxRenderSize = {
        desc.maxRenderExtent.width, desc.maxRenderExtent.height};
    contextDesc.maxUpscaleSize = {
        desc.maxOutputExtent.width, desc.maxOutputExtent.height};
    contextDesc.backendInterface = backendInterface;

    const FfxErrorCode contextError = ffxFsr3UpscalerContextCreate(
        &m_impl->context, &contextDesc);
    if (contextError != FFX_OK) {
        m_impl->context = {};
        m_impl->scratchMemory.reset();
        m_impl->scratchMemorySize = 0u;
        return {
            Fsr31VulkanContextCreateStatus::ContextCreationError,
            contextError
        };
    }

    FfxFsr3UpscalerSharedResourceDescriptions sharedDescriptions{};
    const FfxErrorCode sharedDescriptionError =
        ffxFsr3UpscalerGetSharedResourceDescriptions(
            &m_impl->context, &sharedDescriptions);
    if (sharedDescriptionError != FFX_OK) {
        static_cast<void>(ffxFsr3UpscalerContextDestroy(&m_impl->context));
        m_impl->context = {};
        m_impl->scratchMemory.reset();
        m_impl->scratchMemorySize = 0u;
        return {
            Fsr31VulkanContextCreateStatus::SharedResourceDescriptionError,
            sharedDescriptionError
        };
    }
    const bool sharedDescriptionsValid = matchesSharedResourceDescription(
            sharedDescriptions.dilatedDepth, FFX_SURFACE_FORMAT_R32_FLOAT,
            desc.maxRenderExtent) &&
        matchesSharedResourceDescription(
            sharedDescriptions.dilatedMotionVectors,
            FFX_SURFACE_FORMAT_R16G16_FLOAT, desc.maxRenderExtent) &&
        matchesSharedResourceDescription(
            sharedDescriptions.reconstructedPrevNearestDepth,
            FFX_SURFACE_FORMAT_R32_UINT, desc.maxRenderExtent);
    if (!sharedDescriptionsValid) {
        static_cast<void>(ffxFsr3UpscalerContextDestroy(&m_impl->context));
        m_impl->context = {};
        m_impl->scratchMemory.reset();
        m_impl->scratchMemorySize = 0u;
        return {
            Fsr31VulkanContextCreateStatus::InvalidSharedResourceDescription,
            0
        };
    }

    const bool sharedResourcesCreated = createFsrSharedTexture(
            device, "FSR31.DilatedDepth", RhiTextureFormat::R32Float,
            desc.maxRenderExtent, m_impl->dilatedDepth) &&
        createFsrSharedTexture(
            device, "FSR31.DilatedMotionVectors", RhiTextureFormat::Rg16Float,
            desc.maxRenderExtent, m_impl->dilatedMotionVectors) &&
        createFsrSharedTexture(
            device, "FSR31.ReconstructedPrevNearestDepth",
            RhiTextureFormat::R32Uint, desc.maxRenderExtent,
            m_impl->reconstructedPrevNearestDepth);
    if (!sharedResourcesCreated) {
        destroyFsrSharedTexture(device, m_impl->reconstructedPrevNearestDepth);
        destroyFsrSharedTexture(device, m_impl->dilatedMotionVectors);
        destroyFsrSharedTexture(device, m_impl->dilatedDepth);
        static_cast<void>(ffxFsr3UpscalerContextDestroy(&m_impl->context));
        m_impl->context = {};
        m_impl->scratchMemory.reset();
        m_impl->scratchMemorySize = 0u;
        return {
            Fsr31VulkanContextCreateStatus::SharedResourceCreationError,
            0
        };
    }
    m_impl->maxRenderExtent = desc.maxRenderExtent;
    m_impl->maxOutputExtent = desc.maxOutputExtent;
    m_impl->device = &device;
    m_impl->initialized = true;
    return {Fsr31VulkanContextCreateStatus::Success, 0};
}

Fsr31VulkanContextDestroyResult Fsr31VulkanContext::shutdown() {
    if (!m_impl->initialized) {
        return {Fsr31VulkanContextDestroyStatus::NotInitialized, 0};
    }
    const FfxErrorCode error = ffxFsr3UpscalerContextDestroy(&m_impl->context);
    if (error != FFX_OK) {
        return {Fsr31VulkanContextDestroyStatus::ContextDestroyError, error};
    }
    destroyFsrSharedTexture(
        *m_impl->device, m_impl->reconstructedPrevNearestDepth);
    destroyFsrSharedTexture(*m_impl->device, m_impl->dilatedMotionVectors);
    destroyFsrSharedTexture(*m_impl->device, m_impl->dilatedDepth);
    m_impl->context = {};
    m_impl->scratchMemory.reset();
    m_impl->scratchMemorySize = 0u;
    m_impl->maxRenderExtent = {};
    m_impl->maxOutputExtent = {};
    m_impl->device = nullptr;
    m_impl->sharedResourcesInitialized = false;
    m_impl->initialized = false;
    return {Fsr31VulkanContextDestroyStatus::Success, 0};
}

Fsr31VulkanDispatchResult Fsr31VulkanContext::dispatch(
    const VkRhiDevice& device,
    RhiCommandList& commandList,
    const TemporalFrameInput& frame,
    const Fsr31VulkanDispatchDesc& desc) {
    if (!m_impl->initialized) {
        return Fsr31VulkanDispatchResult{
            Fsr31VulkanDispatchStatus::NotInitialized};
    }
    if (!std::isfinite(desc.sharpness) || desc.sharpness < 0.0f ||
        desc.sharpness > 1.0f) {
        return Fsr31VulkanDispatchResult{
            Fsr31VulkanDispatchStatus::InvalidSettings};
    }
    if (frame.extents.resourceExtent.width > m_impl->maxRenderExtent.width ||
        frame.extents.resourceExtent.height > m_impl->maxRenderExtent.height ||
        frame.extents.outputExtent.width > m_impl->maxOutputExtent.width ||
        frame.extents.outputExtent.height > m_impl->maxOutputExtent.height) {
        return Fsr31VulkanDispatchResult{
            Fsr31VulkanDispatchStatus::ContextExtentExceeded};
    }

    const Fsr31ResourceResolveResult resolved = resolveFsr31VulkanResourceSet(
        device, frame);
    if (!resolved.succeeded()) {
        Fsr31VulkanDispatchResult result;
        result.status = Fsr31VulkanDispatchStatus::InvalidResources;
        result.temporalError = resolved.temporalError;
        result.resourceStatus = resolved.status;
        result.resourceRole = resolved.missingRole;
        result.validationFailure = resolved.validationFailure;
        return result;
    }
    const auto nativeCommandBuffer = VkRhiInterop::commandBuffer(device, commandList);
    if (!nativeCommandBuffer.has_value()) {
        return Fsr31VulkanDispatchResult{
            Fsr31VulkanDispatchStatus::MissingCommandBuffer};
    }

    const auto dilatedDepth = VkRhiInterop::textureInfo(
        device, m_impl->dilatedDepth.texture, m_impl->dilatedDepth.view);
    const auto dilatedMotionVectors = VkRhiInterop::textureInfo(
        device, m_impl->dilatedMotionVectors.texture,
        m_impl->dilatedMotionVectors.view);
    const auto reconstructedPrevNearestDepth = VkRhiInterop::textureInfo(
        device, m_impl->reconstructedPrevNearestDepth.texture,
        m_impl->reconstructedPrevNearestDepth.view);
    if (!dilatedDepth.has_value() || !dilatedMotionVectors.has_value() ||
        !reconstructedPrevNearestDepth.has_value()) {
        return Fsr31VulkanDispatchResult{
            Fsr31VulkanDispatchStatus::InvalidResources};
    }

    const RhiTextureHandle sharedTextures[] = {
        m_impl->dilatedDepth.texture,
        m_impl->dilatedMotionVectors.texture,
        m_impl->reconstructedPrevNearestDepth.texture
    };
    for (const RhiTextureHandle texture : sharedTextures) {
        commandList.textureBarrier({
            texture,
            m_impl->sharedResourcesInitialized
                ? RhiResourceState::ShaderWrite
                : RhiResourceState::Undefined,
            RhiResourceState::ShaderWrite
        });
    }

    const Fsr31VulkanResourceSet& resources = resolved.resources;
    FfxFsr3UpscalerDispatchDescription dispatchDesc{};
    dispatchDesc.commandList = ffxGetCommandListVK(*nativeCommandBuffer);
    dispatchDesc.color = makeFsrResource(
        resources.hdrColor, FFX_RESOURCE_USAGE_READ_ONLY,
        FFX_RESOURCE_STATE_COMPUTE_READ, L"Mecraft FSR HDR color");
    dispatchDesc.depth = makeFsrResource(
        resources.depth, FFX_RESOURCE_USAGE_DEPTHTARGET,
        FFX_RESOURCE_STATE_COMPUTE_READ, L"Mecraft FSR depth");
    dispatchDesc.motionVectors = makeFsrResource(
        resources.velocity, FFX_RESOURCE_USAGE_READ_ONLY,
        FFX_RESOURCE_STATE_COMPUTE_READ, L"Mecraft FSR velocity");
    dispatchDesc.exposure = makeFsrResource(
        resources.exposure, FFX_RESOURCE_USAGE_READ_ONLY,
        FFX_RESOURCE_STATE_COMPUTE_READ, L"Mecraft FSR exposure");
    dispatchDesc.reactive = makeFsrResource(
        resources.reactiveMask, FFX_RESOURCE_USAGE_READ_ONLY,
        FFX_RESOURCE_STATE_COMPUTE_READ, L"Mecraft FSR reactive mask");
    dispatchDesc.transparencyAndComposition = makeFsrResource(
        resources.transparencyMask, FFX_RESOURCE_USAGE_READ_ONLY,
        FFX_RESOURCE_STATE_COMPUTE_READ, L"Mecraft FSR transparency mask");
    dispatchDesc.dilatedDepth = makeFsrResource(
        *dilatedDepth, FFX_RESOURCE_USAGE_UAV,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS, L"Mecraft FSR dilated depth");
    dispatchDesc.dilatedMotionVectors = makeFsrResource(
        *dilatedMotionVectors, FFX_RESOURCE_USAGE_UAV,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS,
        L"Mecraft FSR dilated motion vectors");
    dispatchDesc.reconstructedPrevNearestDepth = makeFsrResource(
        *reconstructedPrevNearestDepth, FFX_RESOURCE_USAGE_UAV,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS,
        L"Mecraft FSR reconstructed previous nearest depth");
    dispatchDesc.output = makeFsrResource(
        resources.outputHdrColor, FFX_RESOURCE_USAGE_UAV,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS, L"Mecraft FSR output");
    dispatchDesc.jitterOffset = {frame.jitter.pixels.x, frame.jitter.pixels.y};
    const glm::vec2 motionVectorScale = fsr31MotionVectorScale(
        frame.motionVectorScale);
    dispatchDesc.motionVectorScale = {
        motionVectorScale.x, motionVectorScale.y};
    dispatchDesc.renderSize = {
        frame.extents.renderRect.width, frame.extents.renderRect.height};
    dispatchDesc.upscaleSize = {
        frame.extents.outputExtent.width, frame.extents.outputExtent.height};
    dispatchDesc.enableSharpening = desc.enableSharpening;
    dispatchDesc.sharpness = desc.sharpness;
    dispatchDesc.frameTimeDelta = frame.frameDeltaMilliseconds;
    dispatchDesc.preExposure = frame.preExposure;
    dispatchDesc.reset = requiresTemporalReset(frame.resetReasons);
    dispatchDesc.cameraNear = frame.cameraNear;
    dispatchDesc.cameraFar = frame.cameraFar;
    dispatchDesc.cameraFovAngleVertical = frame.verticalFovRadians;
    dispatchDesc.viewSpaceToMetersFactor = 1.0f;
    dispatchDesc.flags = desc.drawDebugView
        ? static_cast<uint32_t>(FFX_FSR3UPSCALER_DISPATCH_DRAW_DEBUG_VIEW)
        : 0u;

    const FfxErrorCode error = ffxFsr3UpscalerContextDispatch(
        &m_impl->context, &dispatchDesc);
    if (error != FFX_OK) {
        return Fsr31VulkanDispatchResult{
            Fsr31VulkanDispatchStatus::SdkError, error};
    }
    m_impl->sharedResourcesInitialized = true;
    return Fsr31VulkanDispatchResult{
        Fsr31VulkanDispatchStatus::Success};
}

bool Fsr31VulkanContext::isInitialized() const {
    return m_impl->initialized;
}

TemporalExtent Fsr31VulkanContext::maxRenderExtent() const {
    return m_impl->maxRenderExtent;
}

TemporalExtent Fsr31VulkanContext::maxOutputExtent() const {
    return m_impl->maxOutputExtent;
}

size_t Fsr31VulkanContext::scratchMemorySize() const {
    return m_impl->scratchMemorySize;
}
