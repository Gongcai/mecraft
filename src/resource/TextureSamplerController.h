#ifndef MECRAFT_TEXTURE_SAMPLER_CONTROLLER_H
#define MECRAFT_TEXTURE_SAMPLER_CONTROLLER_H

class RhiDevice;

class TextureSamplerController {
public:
    void refreshAnisotropySupport(const RhiDevice& rhiDevice);

    void setAnisotropy(float anisotropy);
    [[nodiscard]] float anisotropy() const;
    [[nodiscard]] float maxAnisotropy() const;

private:
    float m_anisotropy = 1.0f;
    float m_maxAnisotropy = 1.0f;
};

#endif // MECRAFT_TEXTURE_SAMPLER_CONTROLLER_H
