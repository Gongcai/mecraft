#include "HumanoidSkinLayoutCatalog.h"

#include <cstdlib>

namespace renderer {
namespace {

constexpr HumanoidPartMeshDefinition torso{
    0.25f,
    0.375f,
    0.125f,
    0.0f,
    {{{20, 16, 28, 20}, {28, 16, 36, 20}, {20, 20, 28, 32}, {32, 20, 40, 32}, {16, 20, 20, 32}, {28, 20, 32, 32}}}};

constexpr HumanoidPartMeshDefinition head{
    0.25f,
    0.25f,
    0.25f,
    0.25f,
    {{{8, 0, 16, 8}, {16, 0, 24, 8}, {8, 8, 16, 16}, {24, 8, 32, 16}, {0, 8, 8, 16}, {16, 8, 24, 16}}}};

constexpr HumanoidPartMeshDefinition rightArm{
    0.125f,
    0.375f,
    0.125f,
    -0.375f,
    {{{44, 16, 48, 20}, {48, 16, 52, 20}, {44, 20, 48, 32}, {52, 20, 56, 32}, {40, 20, 44, 32}, {48, 20, 52, 32}}}};

constexpr HumanoidPartMeshDefinition leftArm64x64{
    0.125f,
    0.375f,
    0.125f,
    -0.375f,
    {{{36, 48, 40, 52}, {40, 48, 44, 52}, {36, 52, 40, 64}, {44, 52, 48, 64}, {32, 52, 36, 64}, {40, 52, 44, 64}}}};

constexpr HumanoidPartMeshDefinition leftArmClassicMirrored{
    0.125f,
    0.375f,
    0.125f,
    -0.375f,
    {{{44, 16, 48, 20}, {48, 16, 52, 20}, {44, 20, 48, 32}, {52, 20, 56, 32}, {48, 20, 52, 32}, {40, 20, 44, 32}}}};

constexpr HumanoidPartMeshDefinition rightLeg{
    0.125f,
    0.375f,
    0.125f,
    -0.375f,
    {{{4, 16, 8, 20}, {8, 16, 12, 20}, {4, 20, 8, 32}, {12, 20, 16, 32}, {0, 20, 4, 32}, {8, 20, 12, 32}}}};

constexpr HumanoidPartMeshDefinition leftLeg64x64{
    0.125f,
    0.375f,
    0.125f,
    -0.375f,
    {{{20, 48, 24, 52}, {24, 48, 28, 52}, {20, 52, 24, 64}, {28, 52, 32, 64}, {16, 52, 20, 64}, {24, 52, 28, 64}}}};

constexpr HumanoidPartMeshDefinition leftLegClassicMirrored{
    0.125f,
    0.375f,
    0.125f,
    -0.375f,
    {{{4, 16, 8, 20}, {8, 16, 12, 20}, {4, 20, 8, 32}, {12, 20, 16, 32}, {8, 20, 12, 32}, {0, 20, 4, 32}}}};

constexpr HumanoidSkinLayoutDefinitions kSkinLayouts{
    {{64.0f, 64.0f, {{torso, head, rightArm, leftArm64x64, rightLeg, leftLeg64x64}}},
     {64.0f, 64.0f, {{torso, head, rightArm, leftArmClassicMirrored, rightLeg, leftLegClassicMirrored}}},
     {64.0f, 32.0f, {{torso, head, rightArm, leftArmClassicMirrored, rightLeg, leftLegClassicMirrored}}}}};

} // namespace

std::size_t humanoidPartTypeIndex(const ecs::StevePartType partType) {
    switch (partType) {
    case ecs::StevePartType::Torso: return 0;
    case ecs::StevePartType::Head: return 1;
    case ecs::StevePartType::RightArm: return 2;
    case ecs::StevePartType::LeftArm: return 3;
    case ecs::StevePartType::RightLeg: return 4;
    case ecs::StevePartType::LeftLeg: return 5;
    }
    std::abort();
}

std::size_t humanoidSkinLayoutIndex(const ecs::EntitySkinLayoutKind skinLayout) {
    switch (skinLayout) {
    case ecs::EntitySkinLayoutKind::Steve64x64: return 0;
    case ecs::EntitySkinLayoutKind::Classic64x64: return 1;
    case ecs::EntitySkinLayoutKind::Classic64x32: return 2;
    }
    std::abort();
}

const HumanoidSkinLayoutDefinitions& humanoidSkinLayoutDefinitions() {
    return kSkinLayouts;
}

} // namespace renderer
