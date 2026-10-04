#include <cstdlib>
#include <cmath>
#include <iostream>

#include "../src/renderer/renderers/GameplaySkyRenderer.h"
#include "../src/world/DayNightSystem.h"

namespace {
int fail(const char* message) {
    std::cerr << "[gameplay_sky_renderer_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main() {
    constexpr float kPi = 3.14159265359f;
    const float fullMoonFlux = GameplaySkyRenderer::computeMoonPhaseFlux(0.0f);
    const float halfMoonFlux = GameplaySkyRenderer::computeMoonPhaseFlux(kPi * 0.5f);
    const float newMoonFlux = GameplaySkyRenderer::computeMoonPhaseFlux(kPi);
    if (!(fullMoonFlux > halfMoonFlux && halfMoonFlux > newMoonFlux && newMoonFlux > 0.0f) || fullMoonFlux > 0.0001f) {
        return fail("lunar energy must follow the visible phase and remain subdued at full moon");
    }

    GameplaySkyRenderer renderer;
    DayNightSystem dayNight;
    dayNight.setTimeOfDay(900.0f);
    for (int phase = 0; phase < 8; ++phase) {
        const auto colors = renderer.computeSkyColors(dayNight);
        const auto illuminance = renderer.computeSkyIlluminance(colors);
        const float flux = GameplaySkyRenderer::computeMoonPhaseFlux(colors.moonPhaseAngle);
        const float oppositeFlux = GameplaySkyRenderer::computeMoonPhaseFlux(-colors.moonPhaseAngle);
        if (std::abs(flux - oppositeFlux) > 1e-8f) {
            return fail("waxing and waning moons must have equal energy at equal illuminated area");
        }
        if (phase == 0 && std::abs(flux - newMoonFlux) > 1e-8f) {
            return fail("the phase-zero new moon must use minimum lunar energy");
        }
        if (phase == 4 && std::abs(flux - fullMoonFlux) > 1e-8f) {
            return fail("the phase-four full moon must use maximum lunar energy");
        }
        if (glm::length(illuminance.moonIlluminance) > fullMoonFlux * 4.0f ||
            glm::length(illuminance.directIlluminance - illuminance.moonIlluminance) > 1e-8f) {
            return fail("CPU nighttime lighting must use subdued lunar energy without sunlight");
        }
        const auto uv = GameplaySkyRenderer::getMoonPhaseUv(phase);
        const int col = phase % 4;
        const int row = phase / 4;

        if (uv.first.x < 0.0f || uv.first.y < 0.0f || uv.second.x > 1.0f || uv.second.y > 1.0f) {
            return fail("moon phase uv must remain inside atlas bounds");
        }
        if (uv.first.x != static_cast<float>(col) * 0.25f || uv.first.y != static_cast<float>(row) * 0.5f) {
            return fail("moon phase uv min does not match 4x2 grid");
        }
        if (uv.second.x - uv.first.x != 0.25f || uv.second.y - uv.first.y != 0.5f) {
            return fail("moon phase uv size must match one 32x32 tile in 128x64 atlas");
        }
        dayNight.update(1200.0f);
    }

    std::cout << "[gameplay_sky_renderer_test] PASS\n";
    return EXIT_SUCCESS;
}
