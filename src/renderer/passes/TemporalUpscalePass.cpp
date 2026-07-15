#include "TemporalUpscalePass.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"

#if defined(MECRAFT_ENABLE_FSR31)
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/upscaling/Fsr31VulkanContext.h"
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/upscaling/DlssVulkanContext.h"
#endif

#include <iostream>

namespace {

[[nodiscard]] TemporalUpscaleResult temporalFailure(
    const TemporalUpscaleStatus status,
    const std::optional<TemporalFrameValidationError> validationError = std::nullopt,
    const int32_t sdkError = 0) {
    TemporalUpscaleResult result;
    result.status = status;
    result.validationError = validationError;
    result.sdkError = sdkError;
    return result;
}

[[nodiscard]] constexpr bool sameHandle(
    const RhiTextureHandle lhs,
    const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] constexpr bool sameHandle(
    const RhiTextureViewHandle lhs,
    const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

#if defined(MECRAFT_ENABLE_FSR31)
[[nodiscard]] const char* fsr31ResourceRoleText(const Fsr31ResourceRole role) {
    switch (role) {
        case Fsr31ResourceRole::HdrColor: return "HDR color";
        case Fsr31ResourceRole::Depth: return "depth";
        case Fsr31ResourceRole::Velocity: return "velocity";
        case Fsr31ResourceRole::Exposure: return "exposure";
        case Fsr31ResourceRole::ReactiveMask: return "reactive mask";
        case Fsr31ResourceRole::TransparencyMask: return "transparency mask";
        case Fsr31ResourceRole::OutputHdrColor: return "output HDR color";
    }
    return "unknown resource";
}

[[nodiscard]] const char* fsr31ResourceErrorText(
    const Fsr31ResourceValidationError error) {
    switch (error) {
        case Fsr31ResourceValidationError::MissingNativeObject:
            return "missing Vulkan image or view";
        case Fsr31ResourceValidationError::InvalidImageType:
            return "invalid image type";
        case Fsr31ResourceValidationError::InvalidViewType:
            return "invalid view type";
        case Fsr31ResourceValidationError::InvalidFormat:
            return "invalid format";
        case Fsr31ResourceValidationError::InvalidExtent:
            return "invalid extent";
        case Fsr31ResourceValidationError::InvalidAspectMask:
            return "invalid aspect mask";
        case Fsr31ResourceValidationError::InvalidSubresourceRange:
            return "invalid subresource range";
        case Fsr31ResourceValidationError::MissingSampledUsage:
            return "missing sampled usage";
        case Fsr31ResourceValidationError::MissingStorageUsage:
            return "missing storage usage";
    }
    return "unknown validation error";
}

void reportFsr31ResourceFailure(const Fsr31VulkanDispatchResult& result) {
    std::cerr << "TemporalUpscalePass: FSR 3.1 resource resolution failed";
    if (result.validationFailure.has_value()) {
        std::cerr << " for "
                  << fsr31ResourceRoleText(result.validationFailure->role)
                  << ": "
                  << fsr31ResourceErrorText(result.validationFailure->error);
    } else if (result.resourceRole.has_value()) {
        std::cerr << " for " << fsr31ResourceRoleText(*result.resourceRole);
        if (result.resourceStatus ==
            Fsr31ResourceResolveStatus::MissingNativeResource) {
            std::cerr << ": the RHI texture handle and view do not resolve to one Vulkan image";
        }
    } else if (result.resourceStatus ==
               Fsr31ResourceResolveStatus::MissingNativeResource) {
        std::cerr << ": the RHI texture handle and view do not resolve to one Vulkan image";
    }
    std::cerr << '\n';
}
#endif

} // namespace

TemporalUpscalePass::TemporalUpscalePass() = default;

TemporalUpscalePass::~TemporalUpscalePass() {
    shutdown();
}

void TemporalUpscalePass::init(
    RhiDevice& device,
    RhiCommandListPool& commandListPool) {
    if ((m_device != nullptr && m_device != &device) ||
        (m_commandListPool != nullptr && m_commandListPool != &commandListPool)) {
        shutdown();
    }
    m_device = &device;
    m_commandListPool = &commandListPool;
}

void TemporalUpscalePass::shutdown() {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    static_cast<void>(releaseDlssContext());
#endif
#if defined(MECRAFT_ENABLE_FSR31)
    static_cast<void>(releaseFsr31Context());
#endif
    destroyOutputTarget();
    m_commandListPool = nullptr;
    m_device = nullptr;
}

bool TemporalUpscalePass::prepareOutputTarget(
    const UpscaleSettings& settings,
    const TemporalExtent renderExtent,
    const TemporalExtent outputExtent) {
    if (m_device == nullptr || m_commandListPool == nullptr ||
        !renderExtent.isValid() || !outputExtent.isValid()) {
        return false;
    }
    if (settings.type == TemporalUpscalerType::Native) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (!releaseDlssContext()) {
            return false;
        }
#endif
#if defined(MECRAFT_ENABLE_FSR31)
        if (!releaseFsr31Context()) {
            return false;
        }
#endif
        destroyOutputTarget();
        return true;
    }
    if (settings.type != TemporalUpscalerType::Fsr31 &&
        settings.type != TemporalUpscalerType::Dlss) {
        return false;
    }
    if (m_device->backend() != RhiBackend::Vulkan) {
        return false;
    }

#if defined(MECRAFT_ENABLE_FSR31)
    if (settings.type == TemporalUpscalerType::Dlss &&
        !releaseFsr31Context()) {
        return false;
    }
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (settings.type == TemporalUpscalerType::Fsr31 &&
        !releaseDlssContext()) {
        return false;
    }
#endif

    const bool targetReady = m_outputTexture.isValid() && m_outputView.isValid() &&
                             m_outputExtent == outputExtent;
    if (!targetReady) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (!releaseDlssContext()) {
            return false;
        }
#endif
#if defined(MECRAFT_ENABLE_FSR31)
        if (!releaseFsr31Context()) {
            return false;
        }
#endif
        destroyOutputTarget();
        RhiTextureDesc textureDesc;
        textureDesc.debugName = "TemporalUpscale.OutputHdr";
        textureDesc.format = RhiTextureFormat::Rgba16Float;
        textureDesc.width = outputExtent.width;
        textureDesc.height = outputExtent.height;
        textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                            rhiFlag(RhiTextureUsage::Storage) |
                            rhiFlag(RhiTextureUsage::TransferSrc) |
                            rhiFlag(RhiTextureUsage::TransferDst);
        m_outputTexture = m_device->createTexture(textureDesc, nullptr);
        if (!m_outputTexture.isValid()) {
            return false;
        }

        RhiTextureViewDesc viewDesc;
        viewDesc.texture = m_outputTexture;
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.format = RhiTextureFormat::Rgba16Float;
        viewDesc.mipCount = 1u;
        viewDesc.layerCount = 1u;
        m_outputView = m_device->createTextureView(viewDesc);
        if (!m_outputView.isValid()) {
            destroyOutputTarget();
            return false;
        }
        m_outputExtent = outputExtent;
        m_outputInitialized = false;
    }

#if defined(MECRAFT_ENABLE_FSR31)
    if (settings.type == TemporalUpscalerType::Fsr31) {
        const bool contextReady = m_fsr31Context != nullptr &&
            m_fsr31Context->isInitialized() &&
            m_fsr31Context->maxRenderExtent() == renderExtent &&
            m_fsr31Context->maxOutputExtent() == outputExtent &&
            m_fsr31DynamicResolution == settings.dynamicResolutionEnabled &&
            m_fsr31DebugChecking == settings.debugVisualizationEnabled;
        if (!contextReady) {
            if (!releaseFsr31Context()) {
                return false;
            }
            m_fsr31Context = std::make_unique<Fsr31VulkanContext>();
            const auto created = m_fsr31Context->initialize(
                static_cast<VkRhiDevice&>(*m_device),
                {renderExtent,
                 outputExtent,
                 settings.dynamicResolutionEnabled,
                 settings.debugVisualizationEnabled});
            if (!created.succeeded()) {
                std::cerr << "TemporalUpscalePass: FSR 3.1 initialization failed with status "
                          << static_cast<uint32_t>(created.status)
                          << " and SDK error " << created.sdkError << '\n';
                m_fsr31Context.reset();
                return false;
            }
            m_fsr31DynamicResolution = settings.dynamicResolutionEnabled;
            m_fsr31DebugChecking = settings.debugVisualizationEnabled;
        }
    }
#else
    if (settings.type == TemporalUpscalerType::Fsr31) {
        return false;
    }
#endif

#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (settings.type == TemporalUpscalerType::Dlss) {
        const bool contextReady = m_dlssContext != nullptr &&
            m_dlssContext->isInitialized() &&
            m_dlssContext->renderExtent() == renderExtent &&
            m_dlssContext->outputExtent() == outputExtent &&
            m_dlssContext->quality() == settings.quality;
        if (!contextReady) {
            if (!releaseDlssContext()) {
                return false;
            }
            m_dlssContext = std::make_unique<DlssVulkanContext>();
            if (!m_dlssContext->initialize(
                    settings.quality, renderExtent, outputExtent)) {
                std::cerr << "TemporalUpscalePass: DLSS initialization failed\n";
                m_dlssContext.reset();
                return false;
            }
        }
    }
#else
    if (settings.type == TemporalUpscalerType::Dlss) {
        return false;
    }
#endif
    return true;
}

void TemporalUpscalePass::destroyOutputTarget() {
    if (m_device != nullptr) {
        if (m_outputView.isValid()) {
            m_device->destroyTextureView(m_outputView);
        }
        if (m_outputTexture.isValid()) {
            m_device->destroyTexture(m_outputTexture);
        }
    }
    m_outputTexture = {};
    m_outputView = {};
    m_outputExtent = {};
    m_outputInitialized = false;
}

TemporalUpscaleResult TemporalUpscalePass::execute(
    const UpscaleSettings& settings,
    const TemporalFrameInput& frame) {
    const std::optional<TemporalFrameValidationError> validationError =
        validateTemporalFrame(frame);
    if (validationError.has_value()) {
        return temporalFailure(TemporalUpscaleStatus::InvalidFrame, validationError);
    }

    switch (settings.type) {
        case TemporalUpscalerType::Native:
            if (frame.renderExtent != frame.outputExtent) {
                return temporalFailure(TemporalUpscaleStatus::NativeExtentMismatch);
            }
            {
                TemporalUpscaleResult result;
                result.status = TemporalUpscaleStatus::Success;
                result.outputHdrColor = frame.textures.hdrColor;
                result.outputHdrColorView = frame.textures.hdrColorView;
                result.outputExtent = frame.outputExtent;
                return result;
            }
        case TemporalUpscalerType::Fsr31:
#if defined(MECRAFT_ENABLE_FSR31)
            if (m_device == nullptr || m_commandListPool == nullptr ||
                m_fsr31Context == nullptr || !m_fsr31Context->isInitialized() ||
                !sameHandle(frame.textures.outputHdrColor, m_outputTexture) ||
                !sameHandle(frame.textures.outputHdrColorView, m_outputView)) {
                return temporalFailure(TemporalUpscaleStatus::Fsr31Unavailable);
            }
            {
                RhiCommandList* const commandList = m_commandListPool->acquire(
                    RhiCommandListType::Graphics);
                if (commandList == nullptr ||
                    !commandList->begin(
                        {"FSR31.Dispatch.Commands", RhiCommandListType::Graphics})) {
                    return temporalFailure(TemporalUpscaleStatus::Fsr31CommandError);
                }

                const RhiTextureHandle sampledInputs[] = {
                    frame.textures.hdrColor,
                    frame.textures.velocity,
                    frame.textures.exposure,
                    frame.textures.reactiveMask,
                    frame.textures.transparencyMask
                };
                for (const RhiTextureHandle texture : sampledInputs) {
                    commandList->textureBarrier({
                        texture,
                        RhiResourceState::ShaderRead,
                        RhiResourceState::ShaderRead
                    });
                }
                commandList->textureBarrier({
                    frame.textures.depth,
                    RhiResourceState::DepthRead,
                    RhiResourceState::ShaderRead
                });
                commandList->textureBarrier({
                    m_outputTexture,
                    m_outputInitialized
                        ? RhiResourceState::ShaderRead
                        : RhiResourceState::Undefined,
                    RhiResourceState::ShaderWrite
                });

                const Fsr31VulkanDispatchResult dispatched =
                    m_fsr31Context->dispatch(
                        static_cast<const VkRhiDevice&>(*m_device),
                        *commandList,
                        frame,
                        {settings.sharpeningEnabled,
                         settings.sharpeningStrength,
                         settings.debugVisualizationEnabled});
                if (!dispatched.succeeded()) {
                    static_cast<void>(commandList->end());
                    const TemporalUpscaleStatus status =
                        dispatched.status == Fsr31VulkanDispatchStatus::InvalidResources
                        ? TemporalUpscaleStatus::Fsr31InvalidResources
                        : TemporalUpscaleStatus::Fsr31DispatchError;
                    if (dispatched.status ==
                        Fsr31VulkanDispatchStatus::InvalidResources) {
                        reportFsr31ResourceFailure(dispatched);
                    }
                    static_cast<void>(releaseFsr31Context());
                    return temporalFailure(status, std::nullopt, dispatched.sdkError);
                }

                commandList->textureBarrier({
                    m_outputTexture,
                    RhiResourceState::ShaderWrite,
                    RhiResourceState::ShaderRead
                });
                commandList->textureBarrier({
                    frame.textures.depth,
                    RhiResourceState::ShaderRead,
                    RhiResourceState::DepthRead
                });
                if (!commandList->end()) {
                    static_cast<void>(releaseFsr31Context());
                    return temporalFailure(TemporalUpscaleStatus::Fsr31CommandError);
                }
                RhiCommandList* commandLists[] = {commandList};
                if (!m_device->submit({
                        "FSR31.Dispatch.Submit", commandLists, 1u,
                        RhiQueueType::Graphics})) {
                    static_cast<void>(releaseFsr31Context());
                    return temporalFailure(TemporalUpscaleStatus::Fsr31SubmitError);
                }
                m_outputInitialized = true;
                TemporalUpscaleResult result;
                result.status = TemporalUpscaleStatus::Success;
                result.outputHdrColor = m_outputTexture;
                result.outputHdrColorView = m_outputView;
                result.outputExtent = frame.outputExtent;
                return result;
            }
#else
            return temporalFailure(TemporalUpscaleStatus::Fsr31Unavailable);
#endif
        case TemporalUpscalerType::Dlss:
#if defined(MECRAFT_ENABLE_STREAMLINE)
            if (m_device == nullptr || m_commandListPool == nullptr ||
                m_dlssContext == nullptr || !m_dlssContext->isInitialized() ||
                !sameHandle(frame.textures.outputHdrColor, m_outputTexture) ||
                !sameHandle(frame.textures.outputHdrColorView, m_outputView)) {
                return temporalFailure(TemporalUpscaleStatus::DlssUnavailable);
            }
            {
                RhiCommandList* const commandList = m_commandListPool->acquire(
                    RhiCommandListType::Graphics);
                if (commandList == nullptr ||
                    !commandList->begin(
                        {"DLSS.Dispatch.Commands", RhiCommandListType::Graphics})) {
                    return temporalFailure(TemporalUpscaleStatus::DlssCommandError);
                }
                const RhiTextureHandle sampledInputs[] = {
                    frame.textures.hdrColor,
                    frame.textures.velocity,
                    frame.textures.exposure
                };
                for (const RhiTextureHandle texture : sampledInputs) {
                    commandList->textureBarrier({
                        texture,
                        RhiResourceState::ShaderRead,
                        RhiResourceState::ShaderRead
                    });
                }
                commandList->textureBarrier({
                    frame.textures.depth,
                    RhiResourceState::DepthRead,
                    RhiResourceState::ShaderRead
                });
                commandList->textureBarrier({
                    m_outputTexture,
                    m_outputInitialized
                        ? RhiResourceState::ShaderRead
                        : RhiResourceState::Undefined,
                    RhiResourceState::ShaderWrite
                });

                const DlssVulkanDispatchResult dispatched =
                    m_dlssContext->dispatch(
                        static_cast<const VkRhiDevice&>(*m_device),
                        *commandList,
                        frame);
                if (!dispatched.succeeded()) {
                    static_cast<void>(commandList->end());
                    const TemporalUpscaleStatus status =
                        dispatched.status == DlssVulkanStatus::InvalidResources
                            ? TemporalUpscaleStatus::DlssInvalidResources
                        : dispatched.status == DlssVulkanStatus::MissingCommandBuffer
                            ? TemporalUpscaleStatus::DlssCommandError
                            : TemporalUpscaleStatus::DlssDispatchError;
                    static_cast<void>(releaseDlssContext());
                    return temporalFailure(status);
                }

                commandList->textureBarrier({
                    m_outputTexture,
                    RhiResourceState::ShaderWrite,
                    RhiResourceState::ShaderRead
                });
                commandList->textureBarrier({
                    frame.textures.depth,
                    RhiResourceState::ShaderRead,
                    RhiResourceState::DepthRead
                });
                if (!commandList->end()) {
                    static_cast<void>(releaseDlssContext());
                    return temporalFailure(TemporalUpscaleStatus::DlssCommandError);
                }
                RhiCommandList* commandLists[] = {commandList};
                if (!m_device->submit({
                        "DLSS.Dispatch.Submit", commandLists, 1u,
                        RhiQueueType::Graphics})) {
                    static_cast<void>(releaseDlssContext());
                    return temporalFailure(TemporalUpscaleStatus::DlssSubmitError);
                }
                m_outputInitialized = true;
                TemporalUpscaleResult result;
                result.status = TemporalUpscaleStatus::Success;
                result.outputHdrColor = m_outputTexture;
                result.outputHdrColorView = m_outputView;
                result.outputExtent = frame.outputExtent;
                return result;
            }
#else
            return temporalFailure(TemporalUpscaleStatus::DlssUnavailable);
#endif
    }
    return temporalFailure(TemporalUpscaleStatus::InvalidFrame);
}

#if defined(MECRAFT_ENABLE_FSR31)
bool TemporalUpscalePass::releaseFsr31Context() {
    if (m_fsr31Context == nullptr) {
        return true;
    }
    if (m_fsr31Context->isInitialized()) {
        if (m_device == nullptr) {
            return false;
        }
        m_device->waitIdle();
        const Fsr31VulkanContextDestroyResult destroyed =
            m_fsr31Context->shutdown();
        if (!destroyed.succeeded()) {
            return false;
        }
    }
    m_fsr31Context.reset();
    m_fsr31DynamicResolution = false;
    m_fsr31DebugChecking = false;
    return true;
}
#endif

#if defined(MECRAFT_ENABLE_STREAMLINE)
bool TemporalUpscalePass::releaseDlssContext() {
    if (m_dlssContext == nullptr) {
        return true;
    }
    if (m_dlssContext->isInitialized()) {
        if (m_device == nullptr) {
            return false;
        }
        m_device->waitIdle();
        if (!m_dlssContext->shutdown()) {
            return false;
        }
    }
    m_dlssContext.reset();
    return true;
}
#endif

const char* TemporalUpscalePass::statusText(const TemporalUpscaleStatus status) {
    switch (status) {
        case TemporalUpscaleStatus::Success:
            return "success";
        case TemporalUpscaleStatus::InvalidFrame:
            return "temporal frame contract is invalid";
        case TemporalUpscaleStatus::NativeExtentMismatch:
            return "native temporal reconstruction requires matching render and output extents";
        case TemporalUpscaleStatus::Fsr31Unavailable:
            return "FSR 3.1 temporal reconstruction is not initialized";
        case TemporalUpscaleStatus::Fsr31InvalidResources:
            return "FSR 3.1 temporal resources violate the dispatch contract";
        case TemporalUpscaleStatus::Fsr31CommandError:
            return "FSR 3.1 command recording failed";
        case TemporalUpscaleStatus::Fsr31DispatchError:
            return "FSR 3.1 SDK dispatch failed";
        case TemporalUpscaleStatus::Fsr31SubmitError:
            return "FSR 3.1 command submission failed";
        case TemporalUpscaleStatus::DlssUnavailable:
            return "DLSS temporal reconstruction is not initialized";
        case TemporalUpscaleStatus::DlssInvalidResources:
            return "DLSS temporal resources violate the dispatch contract";
        case TemporalUpscaleStatus::DlssCommandError:
            return "DLSS command recording failed";
        case TemporalUpscaleStatus::DlssDispatchError:
            return "DLSS Streamline evaluation failed";
        case TemporalUpscaleStatus::DlssSubmitError:
            return "DLSS command submission failed";
    }
    return "unknown temporal reconstruction status";
}
