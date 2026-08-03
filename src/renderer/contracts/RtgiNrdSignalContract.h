#ifndef MECRAFT_RTGI_NRD_SIGNAL_CONTRACT_H
#define MECRAFT_RTGI_NRD_SIGNAL_CONTRACT_H

#include <glm/glm.hpp>

#include <optional>

namespace renderer::contracts {

inline constexpr float kRtgiNrdFp16Max = 65504.0f;
inline constexpr float kRtgiNrdEpsilon = 1.0e-6f;

/// Parameters shared with NRD's REBLUR hit-distance normalization.
struct RtgiReblurHitDistanceParameters final {
    float constantScale = 3.0f;
    float viewZScale = 0.1f;
    float roughnessScale = 20.0f;
};

/// Push constants shared by the RTGI NRD signal pack shader and Vulkan validation.
struct alignas(16) RtgiSignalPackPushConstants final {
    glm::mat4 inverseProjection{1.0f};
    glm::vec4 renderExtentAndInverse{1.0f};
    glm::vec4 reblurParametersAndDiffuseRoughness{3.0f, 0.1f, 20.0f, 1.0f};
    glm::vec4 preExposureAndInverse{1.0f};
};

static_assert(sizeof(RtgiSignalPackPushConstants) == 112u);

/// Removes the finite positive pre-exposure scale before radiance enters NRD history.
/// @param radiance Non-negative radiance stored in the pre-exposed scene domain.
/// @param preExposure Finite positive scale applied before HDR storage.
/// @return Scene-referred radiance, or no value for an invalid contract.
[[nodiscard]] std::optional<glm::vec3> rtgiRemovePreExposure(const glm::vec3& radiance, float preExposure);

/// Restores scene radiance to the current pre-exposed domain after NRD resolves history.
/// @param radiance Non-negative scene-referred NRD output radiance.
/// @param preExposure Finite positive scale applied by the current frame.
/// @return Pre-exposed radiance, or no value for an invalid contract.
[[nodiscard]] std::optional<glm::vec3> rtgiApplyPreExposure(const glm::vec3& radiance, float preExposure);

/// Validates the three positive scales required by NRD REBLUR normalization.
/// @param parameters Constant, view-Z, and roughness scales passed to NRD.
/// @return True when the values satisfy the fixed REBLUR input domain.
[[nodiscard]] bool rtgiReblurHitDistanceParametersValid(const RtgiReblurHitDistanceParameters& parameters);

/// Reproduces REBLUR_FrontEnd_GetNormHitDist for one diffuse sample.
/// @param hitDistance Finite non-negative first-bounce distance in world units.
/// @param viewZ Finite primary-surface view-space Z coordinate.
/// @param parameters REBLUR hit-distance normalization parameters.
/// @param roughness Linear roughness in the closed interval [0, 1].
/// @return Normalized hit distance in [NRD_EPS, 1], or no value for an invalid contract.
[[nodiscard]] std::optional<float> rtgiReblurNormalizedHitDistance(float hitDistance, float viewZ,
                                                                   const RtgiReblurHitDistanceParameters& parameters,
                                                                   float roughness);

/// Reproduces RELAX_FrontEnd_PackRadianceAndHitDist with explicit input validation.
/// @param radiance Finite non-negative linear diffuse radiance.
/// @param hitDistance Finite non-negative first-bounce distance.
/// @return FP16-bounded RGB radiance and raw hit distance, or no value for invalid input.
[[nodiscard]] std::optional<glm::vec4> rtgiPackRelaxRadianceAndHitDistance(const glm::vec3& radiance,
                                                                           float hitDistance);

/// Reproduces REBLUR_FrontEnd_PackRadianceAndNormHitDist with explicit input validation.
/// @param radiance Finite non-negative linear diffuse radiance.
/// @param normalizedHitDistance Value produced by rtgiReblurNormalizedHitDistance.
/// @return YCoCg radiance and normalized hit distance, or no value for invalid input.
[[nodiscard]] std::optional<glm::vec4> rtgiPackReblurRadianceAndNormalizedHitDistance(const glm::vec3& radiance,
                                                                                      float normalizedHitDistance);

} // namespace renderer::contracts

#endif // MECRAFT_RTGI_NRD_SIGNAL_CONTRACT_H
