#ifndef MECRAFT_SHADOW_RENDERER_H
#define MECRAFT_SHADOW_RENDERER_H

#include "ShadowMatrices.h"
#include "ShadowCasterCuller.h"

#include <glm/glm.hpp>
#include <array>

class Camera;

// Mecraft CSM shadow renderer.
//
// Owns per-frame shadow state: cascade matrices, light direction, legacy
// single-matrix compatibility fields. Computes cascades via ShadowMatrices
// and binds all shadow uniforms consumed by deferred lighting, volumetric
// fog, and debug shaders.
//
// Shadow depth rendering and cascade attachment selection
// stays in Renderer because it calls Renderer-internal chunk render methods.
// ShadowRenderer is the data + uniform layer.

namespace shadow {

class ShadowRenderer {
public:
    static constexpr int CASCADE_COUNT = ShadowMatrices::CASCADE_COUNT;
    using Cascade = ShadowMatrices::Cascade;

    // Compute light direction from sky colors and store it.
    // Clamps minimum elevation to 0.12 above horizon.
    // Returns the computed direction.
    glm::vec3 computeLightDirection(const glm::vec3& sunDirection,
                                    float sunVisibility,
                                    const glm::vec3& moonDirection,
                                    float moonVisibility,
                                    bool* moonShadowActive = nullptr);

    // Set the light direction directly (e.g. from external source).
    void setLightDirection(const glm::vec3& direction);

    // Build CSM cascades for the current frame and cache all shadow state.
    void update(const Camera& camera,
                const ShadowMatrices::Settings& settings,
                int framebufferWidth,
                int framebufferHeight);

    // Build CSM cascades from a CameraBasis directly (no Camera dependency).
    // Cascades whose updateCascade entry is false keep their previous
    // matrices so interleaved far-cascade updates sample a shadow map that
    // still matches the frozen projection.
    void updateFromBasis(const ShadowMatrices::CameraBasis& basis,
                         const ShadowMatrices::Settings& settings,
                         const std::array<bool, CASCADE_COUNT>& updateCascade = {
                             true, true, true, true});

    // Accessors
    const Cascade& cascade(int index) const { return m_cascades[index]; }
    const std::array<Cascade, CASCADE_COUNT>& cascades() const { return m_cascades; }
    const glm::vec3& lightDirection() const { return m_lightDirection; }
    float shadowExtent() const { return m_shadowExtent; }
    float texelWorldSize() const { return m_texelWorldSize; }
    float shadowDistance() const { return m_shadowDistance; }

    // Legacy cascade-0 matrices for debug/history compatibility.
    const glm::mat4& modelView() const { return m_modelView; }
    const glm::mat4& projection() const { return m_projection; }
    const glm::mat4& projectionInverse() const { return m_projectionInverse; }
    const glm::mat4& viewProj() const { return m_viewProj; }

private:
    std::array<Cascade, CASCADE_COUNT> m_cascades{};
    glm::mat4 m_modelView         = glm::mat4(1.0f);
    glm::mat4 m_projection        = glm::mat4(1.0f);
    glm::mat4 m_projectionInverse = glm::mat4(1.0f);
    glm::mat4 m_viewProj          = glm::mat4(1.0f);
    glm::vec3 m_lightDirection    = glm::vec3(0.0f, 1.0f, 0.0f);
    float m_shadowExtent          = 1.0f;
    float m_texelWorldSize        = 1.0f;
    float m_shadowDistance         = 192.0f;
};

} // namespace shadow

#endif // MECRAFT_SHADOW_RENDERER_H
