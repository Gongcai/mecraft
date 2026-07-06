#include "TextureSamplerController.h"

#include "../Diagnostics.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

#if defined(GL_TEXTURE_MAX_ANISOTROPY)
constexpr GLenum kTextureMaxAnisotropyPName = GL_TEXTURE_MAX_ANISOTROPY;
#elif defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
constexpr GLenum kTextureMaxAnisotropyPName = GL_TEXTURE_MAX_ANISOTROPY_EXT;
#else
constexpr GLenum kTextureMaxAnisotropyPName = 0;
#endif

#if defined(GL_MAX_TEXTURE_MAX_ANISOTROPY)
constexpr GLenum kMaxTextureMaxAnisotropyPName = GL_MAX_TEXTURE_MAX_ANISOTROPY;
#elif defined(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)
constexpr GLenum kMaxTextureMaxAnisotropyPName = GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT;
#else
constexpr GLenum kMaxTextureMaxAnisotropyPName = 0;
#endif

[[nodiscard]] bool hasAnisotropyConstants() {
    return kTextureMaxAnisotropyPName != 0 && kMaxTextureMaxAnisotropyPName != 0;
}

} // namespace

void TextureSamplerController::refreshAnisotropySupport() {
    m_anisotropySupported = hasAnisotropyConstants();
    if (!m_anisotropySupported) {
        m_maxAnisotropy = 1.0f;
        m_anisotropy = 1.0f;
        return;
    }

    GLfloat maxAniso = 1.0f;
    glGetFloatv(kMaxTextureMaxAnisotropyPName, &maxAniso);
    m_maxAnisotropy = std::max(1.0f, static_cast<float>(maxAniso));
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

void TextureSamplerController::applyToTexture2D(const uint32_t textureID) const {
    if (textureID == 0) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] TextureSamplerController::applyToTexture2D requires a valid texture\n");
        return;
    }
    if (!m_anisotropySupported) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyPName, m_anisotropy);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureSamplerController::applyToTexture2DArray(const uint32_t textureID) const {
    if (textureID == 0) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] TextureSamplerController::applyToTexture2DArray requires a valid texture\n");
        return;
    }
    if (!m_anisotropySupported) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, textureID);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, kTextureMaxAnisotropyPName, m_anisotropy);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}
