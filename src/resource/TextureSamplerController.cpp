#include "TextureSamplerController.h"

#include "renderer/rhi/RhiDevice.h"

#include <algorithm>
#include <cmath>
void TextureSamplerController::refreshAnisotropySupport(const RhiDevice& rhiDevice) {
    const RhiCapabilities& capabilities = rhiDevice.capabilities();
    m_maxAnisotropy = capabilities.samplerAnisotropy
        ? std::max(1.0f, capabilities.maxSamplerAnisotropy)
        : 1.0f;
    m_anisotropy = std::clamp(m_anisotropy, 1.0f, m_maxAnisotropy);
}

void TextureSamplerController::setAnisotropy(const float anisotropy) {
    m_anisotropy = std::clamp(anisotropy, 1.0f, std::max(1.0f, m_maxAnisotropy));
}

float TextureSamplerController::anisotropy() const {
    return m_anisotropy;
}

float TextureSamplerController::maxAnisotropy() const {
    return m_maxAnisotropy;
}
