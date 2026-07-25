#include "renderer/presentation/PresentedFrameRateSampler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[presented_frame_rate_sampler_test] FAIL: %s\n", message);
        std::abort();
    }
}

[[nodiscard]] bool near(const double lhs,
                        const double rhs,
                        const double epsilon = 0.001) {
    return std::abs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
    PresentedFrameRateSampler sampler;

    require(!sampler.update(100u, 0.01).has_value(),
            "the first observation must establish the counter baseline");
    require(!sampler.update(124u, 0.24).has_value(),
            "a partial interval must not publish a sample");

    const std::optional<double> firstSample = sampler.update(150u, 0.26);
    require(firstSample.has_value(),
            "a complete interval must publish a sample");
    require(near(*firstSample, 100.0),
            "the sample must use the displayed-frame counter delta");

    require(!sampler.update(154u, 0.25).has_value(),
            "the next interval must begin from the published counter");
    const std::optional<double> secondSample = sampler.update(157u, 0.25);
    require(secondSample.has_value(),
            "the second complete interval must publish a sample");
    require(near(*secondSample, 14.0),
            "the sample must report observed frames instead of an assumed multiplier");

    std::printf("[presented_frame_rate_sampler_test] PASS\n");
    return EXIT_SUCCESS;
}
