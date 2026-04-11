#include <cstdlib>
#include <cmath>
#include <iostream>

#include "../src/ui/BitmapFont.h"

namespace {
int fail(const char* message) {
    std::cerr << "[bitmap_font_layout_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool nearlyEqual(float a, float b) {
    return std::fabs(a - b) <= 1e-6f;
}
}

int main() {
    {
        const auto uv = BitmapFont::glyphUV(0);
        if (!nearlyEqual(uv.first.x, 0.0f) || !nearlyEqual(uv.second.x, 1.0f / 16.0f)) {
            return fail("ASCII 0 U range mismatch");
        }
        if (!nearlyEqual(uv.first.y, 15.0f / 16.0f) || !nearlyEqual(uv.second.y, 1.0f)) {
            return fail("ASCII 0 V range mismatch");
        }
    }

    {
        const auto uv = BitmapFont::glyphUV(static_cast<unsigned char>('A')); // 65 => row 4, col 1
        if (!nearlyEqual(uv.first.x, 1.0f / 16.0f) || !nearlyEqual(uv.second.x, 2.0f / 16.0f)) {
            return fail("ASCII 'A' U range mismatch");
        }
        if (!nearlyEqual(uv.first.y, 11.0f / 16.0f) || !nearlyEqual(uv.second.y, 12.0f / 16.0f)) {
            return fail("ASCII 'A' V range mismatch");
        }
    }

    {
        const auto uv = BitmapFont::glyphUV(255);
        if (!nearlyEqual(uv.first.x, 15.0f / 16.0f) || !nearlyEqual(uv.second.x, 1.0f)) {
            return fail("ASCII 255 U range mismatch");
        }
        if (!nearlyEqual(uv.first.y, 0.0f) || !nearlyEqual(uv.second.y, 1.0f / 16.0f)) {
            return fail("ASCII 255 V range mismatch");
        }
    }

    std::cout << "[bitmap_font_layout_test] PASS\n";
    return EXIT_SUCCESS;
}


