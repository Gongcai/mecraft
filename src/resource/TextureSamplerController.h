#ifndef MECRAFT_TEXTURE_SAMPLER_CONTROLLER_H
#define MECRAFT_TEXTURE_SAMPLER_CONTROLLER_H

#include <glad/glad.h>

class TextureSamplerController {
public:
    void refreshAnisotropySupport();

    void setAnisotropy(float anisotropy);
    [[nodiscard]] float anisotropy() const;
    [[nodiscard]] float maxAnisotropy() const;

    void applyToTexture2D(GLuint textureID) const;
    void applyToTexture2DArray(GLuint textureID) const;

private:
    float m_anisotropy = 1.0f;
    float m_maxAnisotropy = 1.0f;
    bool m_anisotropySupported = false;
};

#endif // MECRAFT_TEXTURE_SAMPLER_CONTROLLER_H
