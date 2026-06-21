#include "renderer/renderers/HumanoidSkinLayoutCatalog.h"

#include <cstdio>
#include <cstdlib>

static void require(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static bool rectEquals(const renderer::HumanoidSkinPixelRect& lhs,
                       const renderer::HumanoidSkinPixelRect& rhs) {
    return lhs.x0 == rhs.x0 &&
           lhs.y0 == rhs.y0 &&
           lhs.x1 == rhs.x1 &&
           lhs.y1 == rhs.y1;
}

int main() {
    const renderer::HumanoidSkinLayoutDefinitions& definitions = renderer::humanoidSkinLayoutDefinitions();

    const auto steveIndex = renderer::humanoidSkinLayoutIndex(ecs::EntitySkinLayoutKind::Steve64x64);
    const auto classicIndex = renderer::humanoidSkinLayoutIndex(ecs::EntitySkinLayoutKind::Classic64x32);
    const auto leftArmIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::LeftArm);
    const auto rightArmIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::RightArm);
    const auto leftLegIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::LeftLeg);
    const auto rightLegIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::RightLeg);

    require(steveIndex != classicIndex, "skin layout indexes should be distinct");
    require(leftArmIndex != rightArmIndex, "arm part indexes should be distinct");
    require(leftLegIndex != rightLegIndex, "leg part indexes should be distinct");

    const auto& steveLeftArm = definitions[steveIndex][leftArmIndex];
    const auto& classicLeftArm = definitions[classicIndex][leftArmIndex];
    const auto& classicRightArm = definitions[classicIndex][rightArmIndex];
    const auto& classicLeftLeg = definitions[classicIndex][leftLegIndex];
    const auto& classicRightLeg = definitions[classicIndex][rightLegIndex];

    require(!rectEquals(steveLeftArm.faceUvs[0], classicLeftArm.faceUvs[0]),
            "64x64 left arm should use its own top UV region");
    require(rectEquals(classicLeftArm.faceUvs[0], classicRightArm.faceUvs[0]),
            "classic left arm should reuse right arm top UV region");
    require(rectEquals(classicLeftArm.faceUvs[4], classicRightArm.faceUvs[5]),
            "classic left arm should swap side UV regions");
    require(rectEquals(classicLeftArm.faceUvs[5], classicRightArm.faceUvs[4]),
            "classic left arm should swap side UV regions");
    require(rectEquals(classicLeftLeg.faceUvs[4], classicRightLeg.faceUvs[5]),
            "classic left leg should swap side UV regions");
    require(rectEquals(classicLeftLeg.faceUvs[5], classicRightLeg.faceUvs[4]),
            "classic left leg should swap side UV regions");

    std::printf("All HumanoidSkinLayoutCatalog tests passed!\n");
    return 0;
}
