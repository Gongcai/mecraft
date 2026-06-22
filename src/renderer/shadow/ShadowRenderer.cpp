#include "ShadowRenderer.h"
#include "engine/camera/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace shadow {

glm::vec3 ShadowRenderer::computeLightDirection(
        const GameplaySkyRenderer::SkyColors& skyColors,
        bool* moonShadowActive) {
    const bool useMoonShadow = skyColors.moonVisibility > skyColors.sunVisibility;
    glm::vec3 direction = useMoonShadow ? skyColors.moonDirection : skyColors.sunDirection;
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

void ShadowRenderer::bindShadowUniforms(Shader& shader, bool moonShadowActive,
                                        const BiasSettings& bias) const {
    shader.setMat4("uShadowViewProj", m_viewProj);
    shader.setMat4("uShadowModelView", m_modelView);
    shader.setMat4("uShadowProjection", m_projection);
    shader.setMat4("uShadowProjectionInverse", m_projectionInverse);
    shader.setVec3("uShadowLightDirection", m_lightDirection);
    shader.setFloat("uShadowDistance", std::max(64.0f, m_shadowDistance));
    shader.setFloat("uShadowExtent", m_shadowExtent);
    shader.setFloat("uShadowTexelWorldSize", m_texelWorldSize);
    shader.setFloat("uShadowConstantBias", bias.constantBias);
    shader.setFloat("uShadowSlopeBias", bias.slopeBias);
    shader.setFloat("uShadowNormalOffset", bias.normalOffset);
    shader.setInt("uShadowLightMode", moonShadowActive ? 1 : 0);
    shader.setInt("uCsmCascadeCount", CASCADE_COUNT);
    static const std::string prefix_viewProj[] = {
        "uCsmCascades[0].viewProj", "uCsmCascades[1].viewProj",
        "uCsmCascades[2].viewProj", "uCsmCascades[3].viewProj"
    };
    static const std::string prefix_splitNear[] = {
        "uCsmCascades[0].splitNear", "uCsmCascades[1].splitNear",
        "uCsmCascades[2].splitNear", "uCsmCascades[3].splitNear"
    };
    static const std::string prefix_splitFar[] = {
        "uCsmCascades[0].splitFar", "uCsmCascades[1].splitFar",
        "uCsmCascades[2].splitFar", "uCsmCascades[3].splitFar"
    };
    static const std::string prefix_texelWorldSize[] = {
        "uCsmCascades[0].texelWorldSize", "uCsmCascades[1].texelWorldSize",
        "uCsmCascades[2].texelWorldSize", "uCsmCascades[3].texelWorldSize"
    };
    static const std::string prefix_resolutionScale[] = {
        "uCsmCascades[0].resolutionScale", "uCsmCascades[1].resolutionScale",
        "uCsmCascades[2].resolutionScale", "uCsmCascades[3].resolutionScale"
    };
    static const std::string prefix_depthExtent[] = {
        "uCsmCascades[0].depthExtent", "uCsmCascades[1].depthExtent",
        "uCsmCascades[2].depthExtent", "uCsmCascades[3].depthExtent"
    };

    for (int i = 0; i < CASCADE_COUNT; ++i) {
        shader.setMat4(prefix_viewProj[i], m_cascades[i].viewProj);
        shader.setFloat(prefix_splitNear[i], m_cascades[i].splitNear);
        shader.setFloat(prefix_splitFar[i], m_cascades[i].splitFar);
        shader.setFloat(prefix_texelWorldSize[i], m_cascades[i].texelWorldSize);
        shader.setFloat(prefix_resolutionScale[i], (i >= 2) ? 0.5f : 1.0f);
        shader.setFloat(prefix_depthExtent[i], m_cascades[i].depthExtent);
    }
}

} // namespace shadow
