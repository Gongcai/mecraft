#pragma once

#include <array>
#include <cstdint>

namespace renderer::gl {

class ScopedStateSnapshot {
public:
    ScopedStateSnapshot();
    ~ScopedStateSnapshot();

    ScopedStateSnapshot(const ScopedStateSnapshot&) = delete;
    ScopedStateSnapshot& operator=(const ScopedStateSnapshot&) = delete;

private:
    static constexpr int kTrackedTextureUnits = 16;

    static void restoreCapability(uint32_t capability, uint8_t enabled);

    int32_t m_viewport[4] = {0, 0, 0, 0};
    int32_t m_scissorBox[4] = {0, 0, 0, 0};
    int32_t m_cullFaceMode = 0;
    int32_t m_depthFunc = 0;
    int32_t m_blendSrcRgb = 0;
    int32_t m_blendDstRgb = 0;
    int32_t m_blendSrcAlpha = 0;
    int32_t m_blendDstAlpha = 0;
    int32_t m_activeTexture = 0;
    int32_t m_currentProgram = 0;
    uint8_t m_depthTestEnabled = 0;
    uint8_t m_depthMask = 1;
    uint8_t m_cullFaceEnabled = 0;
    uint8_t m_blendEnabled = 0;
    uint8_t m_scissorEnabled = 0;
    double m_depthRange[2] = {0.0, 1.0};
    double m_depthClearValue = 1.0;
    std::array<int32_t, kTrackedTextureUnits> m_texture2DBindings{};
    std::array<int32_t, kTrackedTextureUnits> m_texture2DArrayBindings{};
};

} // namespace renderer::gl
