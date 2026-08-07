#ifndef MECRAFT_TEMPORAL_REPROJECTION_CONTRACT_H
#define MECRAFT_TEMPORAL_REPROJECTION_CONTRACT_H

#include <glm/glm.hpp>

namespace renderer::contracts {

/// Three homogeneous rows that map a current far-plane clip position to the
/// previous frame without exceeding Vulkan's minimum push-constant capacity.
struct alignas(16) TemporalSkyReprojectionHomography final {
    glm::vec4 row0{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 row1{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 row2{0.0f, 0.0f, 1.0f, 0.0f};
};

static_assert(sizeof(TemporalSkyReprojectionHomography) == 48u);

/// Compose clip-space reprojection for infinitely distant sky pixels.
/// @param currentProjection Current unjittered camera projection.
/// @param currentView Current camera view matrix.
/// @param previousProjection Previous unjittered camera projection.
/// @param previousView Previous camera view matrix.
/// @param currentJitterOffset Current NDC projection offset applied to both frames.
/// @param projectionJitterEnabled Whether scene rasterization uses temporal jitter.
/// @return Current-clip to previous-clip transform with camera translation removed.
[[nodiscard]] inline glm::mat4
makeTemporalSkyClipToPrevClip(const glm::mat4& currentProjection, const glm::mat4& currentView,
                              const glm::mat4& previousProjection, const glm::mat4& previousView,
                              const glm::vec2& currentJitterOffset, const bool projectionJitterEnabled) {
    glm::dmat4 currentRasterProjection(currentProjection);
    glm::dmat4 previousRasterProjection(previousProjection);
    if (projectionJitterEnabled) {
        for (int column = 0; column < 4; ++column) {
            currentRasterProjection[column][0] +=
                static_cast<double>(currentJitterOffset.x) * currentRasterProjection[column][3];
            currentRasterProjection[column][1] +=
                static_cast<double>(currentJitterOffset.y) * currentRasterProjection[column][3];
            previousRasterProjection[column][0] +=
                static_cast<double>(currentJitterOffset.x) * previousRasterProjection[column][3];
            previousRasterProjection[column][1] +=
                static_cast<double>(currentJitterOffset.y) * previousRasterProjection[column][3];
        }
    }

    glm::dmat4 currentSkyView(currentView);
    glm::dmat4 previousSkyView(previousView);
    currentSkyView[3] = glm::dvec4(0.0, 0.0, 0.0, 1.0);
    previousSkyView[3] = glm::dvec4(0.0, 0.0, 0.0, 1.0);

    const glm::dmat4 currentSkyRaster = currentRasterProjection * currentSkyView;
    const glm::dmat4 previousSkyRaster = previousRasterProjection * previousSkyView;
    return glm::mat4(previousSkyRaster * glm::inverse(currentSkyRaster));
}

/// Reduce a far-plane 4x4 reprojection into a screen-space homography.
/// @param clipToPrevClip Current-clip to previous-clip sky transform.
/// @return Three rows mapping (clipX, clipY, 1) to (previousX, previousY, previousW).
[[nodiscard]] inline TemporalSkyReprojectionHomography
makeTemporalSkyReprojectionHomography(const glm::mat4& clipToPrevClip) {
    TemporalSkyReprojectionHomography result;
    result.row0 =
        glm::vec4(clipToPrevClip[0][0], clipToPrevClip[1][0], clipToPrevClip[2][0] + clipToPrevClip[3][0], 0.0f);
    result.row1 =
        glm::vec4(clipToPrevClip[0][1], clipToPrevClip[1][1], clipToPrevClip[2][1] + clipToPrevClip[3][1], 0.0f);
    result.row2 =
        glm::vec4(clipToPrevClip[0][3], clipToPrevClip[1][3], clipToPrevClip[2][3] + clipToPrevClip[3][3], 0.0f);
    return result;
}

} // namespace renderer::contracts

#endif // MECRAFT_TEMPORAL_REPROJECTION_CONTRACT_H
