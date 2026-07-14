#include "TemporalUpscalePass.h"

#include "renderer/rhi/RhiDevice.h"

TemporalUpscalePass::~TemporalUpscalePass() {
    shutdown();
}

void TemporalUpscalePass::init(RhiDevice& device) {
    if (m_device != nullptr && m_device != &device) {
        destroyOutputTarget();
    }
    m_device = &device;
}

void TemporalUpscalePass::shutdown() {
    destroyOutputTarget();
    m_device = nullptr;
}

bool TemporalUpscalePass::prepareOutputTarget(
    const TemporalUpscalerType type,
    const TemporalExtent outputExtent) {
    if (m_device == nullptr || !outputExtent.isValid()) {
        return false;
    }
    if (type == TemporalUpscalerType::Native) {
        destroyOutputTarget();
        return true;
    }
    if (type != TemporalUpscalerType::Fsr31 &&
        type != TemporalUpscalerType::Dlss) {
        return false;
    }
    if (m_device->backend() != RhiBackend::Vulkan) {
        return false;
    }
    if (m_outputTexture.isValid() && m_outputView.isValid() &&
        m_outputExtent == outputExtent) {
        return true;
    }

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
}

TemporalUpscaleResult TemporalUpscalePass::execute(
    const TemporalUpscalerType type,
    const TemporalFrameInput& frame) const {
    const std::optional<TemporalFrameValidationError> validationError =
        validateTemporalFrame(frame);
    if (validationError.has_value()) {
        return {TemporalUpscaleStatus::InvalidFrame, validationError, {}, {}, {}};
    }

    switch (type) {
        case TemporalUpscalerType::Native:
            if (frame.renderExtent != frame.outputExtent) {
                return {TemporalUpscaleStatus::NativeExtentMismatch, std::nullopt, {}, {}, {}};
            }
            return {
                TemporalUpscaleStatus::Success,
                std::nullopt,
                frame.textures.hdrColor,
                frame.textures.hdrColorView,
                frame.outputExtent
            };
        case TemporalUpscalerType::Fsr31:
            return {TemporalUpscaleStatus::Fsr31Unavailable, std::nullopt, {}, {}, {}};
        case TemporalUpscalerType::Dlss:
            return {TemporalUpscaleStatus::DlssUnavailable, std::nullopt, {}, {}, {}};
    }
    return {TemporalUpscaleStatus::InvalidFrame, std::nullopt, {}, {}, {}};
}

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
        case TemporalUpscaleStatus::DlssUnavailable:
            return "DLSS temporal reconstruction is not initialized";
    }
    return "unknown temporal reconstruction status";
}
