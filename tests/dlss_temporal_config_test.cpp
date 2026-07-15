#include "renderer/upscaling/DlssVulkanContext.h"

#include <cmath>
#include <iostream>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

bool nearValue(const float lhs, const float rhs) {
    return std::abs(lhs - rhs) <= 0.000001f;
}

bool testJitterSequence() {
    constexpr TemporalExtent renderExtent{1280u, 720u};
    constexpr TemporalExtent outputExtent{1920u, 1080u};
    const DlssJitterResult first = queryDlssJitter(
        0u, renderExtent, outputExtent);
    const DlssJitterResult wrapped = queryDlssJitter(
        first.phaseCount, renderExtent, outputExtent);
    return requireTrue(first.succeeded(),
                       "DLSS jitter query must accept valid extents") &&
           requireTrue(first.phaseCount >= 8u && first.phaseIndex == 0u,
                       "DLSS jitter must expose a stable multi-phase sequence") &&
           requireTrue(nearValue(first.jitter.pixels.x, wrapped.jitter.pixels.x) &&
                           nearValue(first.jitter.pixels.y, wrapped.jitter.pixels.y),
                       "DLSS jitter sequence must wrap at the reported phase count") &&
           requireTrue(nearValue(
                           first.jitter.projectionOffset.x,
                           2.0f * first.jitter.pixels.x /
                               static_cast<float>(renderExtent.width)) &&
                           nearValue(
                               first.jitter.projectionOffset.y,
                               -2.0f * first.jitter.pixels.y /
                                   static_cast<float>(renderExtent.height)),
                       "DLSS jitter projection conversion must match the renderer convention");
}

bool testInvalidJitterExtent() {
    return requireTrue(
        queryDlssJitter(0u, {}, {1920u, 1080u}).status ==
            DlssVulkanStatus::InvalidExtent,
        "DLSS jitter must reject an invalid render extent");
}

} // namespace

int main() {
    if (!testJitterSequence()) return 1;
    if (!testInvalidJitterExtent()) return 1;
    return 0;
}
