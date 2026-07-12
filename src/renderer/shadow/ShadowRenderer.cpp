#include "ShadowRenderer.h"
#include "engine/camera/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace shadow {

glm::vec3 ShadowRenderer::computeLightDirection(
        const glm::vec3& sunDirection,
        const float sunVisibility,
        const glm::vec3& moonDirection,
        const float moonVisibility,
        bool* moonShadowActive) {
    const bool useMoonShadow = moonVisibility > sunVisibility;
    glm::vec3 direction = useMoonShadow ? moonDirection : sunDirection;
    direction = glm::normalize(direction);
    if (direction.y < 0.12f) {
        const glm::vec3 horizontal = glm::normalize(glm::vec3(direction.x, 0.0f, direction.z));
        constexpr float kMinShadowElevation = 0.12f;
        const float horizontalScale = std::sqrt(
            std::max(0.0f, 1.0f - kMinShadowElevation * kMinShadowElevation));
        direction = horizontal * horizontalScale + glm::vec3(0.0f, kMinShadowElevation, 0.0f);
    }
    if (moonShadowActive != nullptr) {
        *moonShadowActive = useMoonShadow;
    }
    m_lightDirection = glm::normalize(direction);
    return m_lightDirection;
}

void ShadowRenderer::setLightDirection(const glm::vec3& direction) {
    m_lightDirection = glm::normalize(direction);
}

void ShadowRenderer::update(const Camera& camera,
                             const ShadowMatrices::Settings& settings,
                             int framebufferWidth,
                             int framebufferHeight) {
    ShadowMatrices::CameraBasis basis;
    basis.position = camera.getPosition();
    basis.forward = camera.getFront();
    basis.right = camera.getRight();
    basis.up = camera.getUp();
    basis.nearPlane = camera.getNear();
    basis.verticalFovDegrees = camera.getFOV();
    basis.aspectRatio = static_cast<float>(std::max(1, framebufferWidth)) /
                        static_cast<float>(std::max(1, framebufferHeight));

    m_cascades = ShadowMatrices::buildCascades(basis, m_lightDirection, settings);

    // Cache legacy cascade-0 matrices for debug/history compatibility.
    m_modelView = m_cascades[0].view;
    m_projection = m_cascades[0].projection;
    m_projectionInverse = glm::inverse(m_cascades[0].projection);
    m_viewProj = m_cascades[0].viewProj;
    m_shadowExtent = std::max(1.0f, m_cascades[0].splitFar);
    m_texelWorldSize = m_cascades[0].texelWorldSize;
    m_shadowDistance = settings.shadowDistance;
}

void ShadowRenderer::updateFromBasis(const ShadowMatrices::CameraBasis& basis,
                                     const ShadowMatrices::Settings& settings) {
    m_cascades = ShadowMatrices::buildCascades(basis, m_lightDirection, settings);

    // Cache legacy cascade-0 matrices for debug/history compatibility.
    m_modelView = m_cascades[0].view;
    m_projection = m_cascades[0].projection;
    m_projectionInverse = glm::inverse(m_cascades[0].projection);
    m_viewProj = m_cascades[0].viewProj;
    m_shadowExtent = std::max(1.0f, m_cascades[0].splitFar);
    m_texelWorldSize = m_cascades[0].texelWorldSize;
    m_shadowDistance = settings.shadowDistance;
}

} // namespace shadow
