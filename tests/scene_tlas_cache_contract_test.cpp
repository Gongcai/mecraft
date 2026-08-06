#include "renderer/rhi/SceneTlasCache.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[scene_tlas_cache_contract_test] " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace renderer::rt;

    std::vector<SceneTlasInstanceKey> keys{{SceneTlasInstanceKind::StaticMesh, 5, 1},
                                           {SceneTlasInstanceKind::Terrain, 8, 3},
                                           {SceneTlasInstanceKind::Terrain, 8, 1},
                                           {SceneTlasInstanceKind::FirstPerson, 2, 0}};
    std::sort(keys.begin(), keys.end());

    const glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -2.0f, 7.0f)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(-2.0f, 3.0f, 4.0f));
    std::array<float, 12u> encoded{};
    const bool encodedTransform = SceneTlasCache::encodeTransform(transform, encoded);
    glm::mat4 singular(1.0f);
    singular[0][0] = 0.0f;
    glm::mat4 nonFinite(1.0f);
    nonFinite[2][1] = std::numeric_limits<float>::quiet_NaN();
    glm::mat4 nonAffine(1.0f);
    nonAffine[0][3] = 0.25f;

    const RhiAccelerationStructureInstanceFlags singleSidedFlags = SceneTlasCache::instanceFlags(false);
    const RhiAccelerationStructureInstanceFlags doubleSidedFlags = SceneTlasCache::instanceFlags(true);
    const uint8_t solidMask = sceneTlasMaskBit(SceneTlasInstanceMask::GiOpaque) |
                              sceneTlasMaskBit(SceneTlasInstanceMask::ShadowCaster) |
                              sceneTlasMaskBit(SceneTlasInstanceMask::ReflectionVisible);
    const uint8_t cutoutMask =
        sceneTlasMaskBit(SceneTlasInstanceMask::GiCutout) | sceneTlasMaskBit(SceneTlasInstanceMask::ShadowCaster);

    const std::optional<glm::vec3> cameraOrigin = SceneTlasCache::sceneOriginForCamera({257.25f, -0.25f, -129.0f});
    glm::mat4 rebasedTransform;
    const glm::mat4 largeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1000000.5f, -2048.25f, 300000.75f));
    const bool rebased = cameraOrigin.has_value() &&
                         SceneTlasCache::rebaseTransform(largeTransform, *cameraOrigin, rebasedTransform);
    const glm::vec3 expectedTranslation = cameraOrigin.has_value()
                                              ? glm::vec3(1000000.5f, -2048.25f, 300000.75f) - *cameraOrigin
                                              : glm::vec3(0.0f);

    const bool valid =
        requireTrue(keys[0] == SceneTlasInstanceKey{SceneTlasInstanceKind::Terrain, 8, 1} &&
                        keys[1] == SceneTlasInstanceKey{SceneTlasInstanceKind::Terrain, 8, 3} &&
                        keys[2].kind == SceneTlasInstanceKind::StaticMesh &&
                        keys[3].kind == SceneTlasInstanceKind::FirstPerson,
                    "heterogeneous instance keys must preserve deterministic kind and identity order") &&
        requireTrue(encodedTransform && encoded[0] == -2.0f && encoded[5] == 3.0f && encoded[10] == 4.0f &&
                        encoded[3] == 3.0f && encoded[7] == -2.0f && encoded[11] == 7.0f,
                    "negative-scale transforms must preserve raster local-to-world coefficients in row-major 3x4") &&
        requireTrue(!SceneTlasCache::encodeTransform(singular, encoded) &&
                        !SceneTlasCache::encodeTransform(nonFinite, encoded) &&
                        !SceneTlasCache::encodeTransform(nonAffine, encoded),
                    "singular, non-finite, and non-affine transforms must be rejected") &&
        requireTrue(
            (singleSidedFlags & rhiFlag(RhiAccelerationStructureInstanceFlag::TriangleFrontCounterClockwise)) != 0u &&
                (singleSidedFlags & rhiFlag(RhiAccelerationStructureInstanceFlag::TriangleFacingCullDisable)) == 0u &&
                (doubleSidedFlags & rhiFlag(RhiAccelerationStructureInstanceFlag::TriangleFacingCullDisable)) != 0u,
            "TLAS facing flags must match CCW raster winding and double-sided culling") &&
        requireTrue((solidMask & sceneTlasMaskBit(SceneTlasInstanceMask::GiOpaque)) != 0u &&
                        (solidMask & sceneTlasMaskBit(SceneTlasInstanceMask::GiCutout)) == 0u &&
                        (cutoutMask & sceneTlasMaskBit(SceneTlasInstanceMask::GiCutout)) != 0u &&
                        sceneTlasMaskBit(SceneTlasInstanceMask::FirstPerson) !=
                            sceneTlasMaskBit(SceneTlasInstanceMask::ReflectionVisible),
                    "GI, shadow, reflection, and first-person visibility masks must remain distinct") &&
        requireTrue(cameraOrigin.has_value() && *cameraOrigin == glm::vec3(256.0f, -128.0f, -256.0f) && rebased &&
                        glm::length(glm::vec3(rebasedTransform[3]) - expectedTranslation) <= 1.0e-4f,
                    "TLAS scene origin and affine translation rebasing must preserve large-world offsets");

    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
