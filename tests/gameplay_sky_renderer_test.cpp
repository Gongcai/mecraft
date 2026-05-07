#include <cstdlib>
#include <iostream>

#include "../src/renderer/GameplaySkyRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[gameplay_sky_renderer_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    for (int phase = 0; phase < 8; ++phase) {
        const auto uv = GameplaySkyRenderer::getMoonPhaseUv(phase);
        const int col = phase % 4;
        const int row = phase / 4;

        if (uv.first.x < 0.0f || uv.first.y < 0.0f ||
            uv.second.x > 1.0f || uv.second.y > 1.0f) {
            return fail("moon phase uv must remain inside atlas bounds");
        }
        if (uv.first.x != static_cast<float>(col) * 0.25f ||
            uv.first.y != static_cast<float>(row) * 0.5f) {
            return fail("moon phase uv min does not match 4x2 grid");
        }
        if (uv.second.x - uv.first.x != 0.25f ||
            uv.second.y - uv.first.y != 0.5f) {
            return fail("moon phase uv size must match one 32x32 tile in 128x64 atlas");
        }
    }

    std::cout << "[gameplay_sky_renderer_test] PASS\n";
    return EXIT_SUCCESS;
}
