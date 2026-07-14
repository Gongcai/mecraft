#include "renderer/upscaling/Fsr31VulkanContext.h"

#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <cstdlib>
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

} // namespace

struct Fsr31VulkanContext::Impl {
    FfxFsr3UpscalerContext context{};
    std::unique_ptr<uint8_t, FsrScratchMemoryDeleter> scratchMemory;
    size_t scratchMemorySize = 0u;
    TemporalExtent maxRenderExtent;
    TemporalExtent maxOutputExtent;
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
    const VkRhiDevice& device,
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
    m_impl->maxRenderExtent = desc.maxRenderExtent;
    m_impl->maxOutputExtent = desc.maxOutputExtent;
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
    m_impl->context = {};
    m_impl->scratchMemory.reset();
    m_impl->scratchMemorySize = 0u;
    m_impl->maxRenderExtent = {};
    m_impl->maxOutputExtent = {};
    m_impl->initialized = false;
    return {Fsr31VulkanContextDestroyStatus::Success, 0};
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
