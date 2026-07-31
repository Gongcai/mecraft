#include <cstdlib>
#include <iostream>

#include "../src/world/DayNightSystem.h"

namespace {
int fail(const char* message) {
    std::cerr << "[day_night_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main() {
    DayNightSystem dayNight;

    dayNight.setTimeOfDay(0.0f);
    if (dayNight.getDayProgress01() < 0.0f || dayNight.getDayProgress01() >= 1.0f) {
        return fail("day progress must stay in [0, 1)");
    }
    if (dayNight.getCelestialAngleRadians() < 0.0f || dayNight.getCelestialAngleRadians() >= 6.284f) {
        return fail("celestial angle must stay inside one revolution");
    }

    dayNight.setTimeOfDay(600.0f);
    if (dayNight.getDayProgress01() < 0.499f || dayNight.getDayProgress01() > 0.501f) {
        return fail("midday progress should be 0.5");
    }
    if (dayNight.isNightTime() || dayNight.isFullDaytime()) {
        return fail("sunset transition should not be sleep night or full day");
    }

    dayNight.setTimeOfDay(900.0f);
    if (!dayNight.isNightTime() || dayNight.isFullDaytime()) {
        return fail("midnight should allow sleeping and should not be full day");
    }

    dayNight.setTimeOfDay(120.0f);
    if (dayNight.isNightTime() || !dayNight.isFullDaytime()) {
        return fail("morning should be full daytime and should not allow sleeping");
    }

    for (int i = 0; i < 8; ++i) {
        dayNight.update(1200.0f);
    }
    if (dayNight.getMoonPhaseIndex() != 0) {
        return fail("moon phase should wrap after eight elapsed days");
    }

    dayNight.update(1200.0f);
    if (dayNight.getMoonPhaseIndex() != 1) {
        return fail("moon phase should advance once per elapsed day");
    }

    std::cout << "[day_night_system_test] PASS\n";
    return EXIT_SUCCESS;
}
