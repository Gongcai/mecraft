#ifndef MECRAFT_HUMANOID_SKIN_LAYOUT_CATALOG_H
#define MECRAFT_HUMANOID_SKIN_LAYOUT_CATALOG_H

#include <array>
#include <cstddef>

#include "../../ecs/components/Components.h"
#include "../../ecs/entity/EntitySkinLayout.h"

namespace renderer {

struct HumanoidSkinPixelRect {
    float x0;
    float y0;
    float x1;
    float y1;
};

struct HumanoidPartMeshDefinition {
    float halfWidth;
    float halfHeight;
    float halfDepth;
    float offsetY;
    std::array<HumanoidSkinPixelRect, 6> faceUvs;
};

inline constexpr std::size_t kHumanoidSkinLayoutCount = 2;
inline constexpr std::size_t kHumanoidPartTypeCount = 6;

using HumanoidPartDefinitions = std::array<HumanoidPartMeshDefinition, kHumanoidPartTypeCount>;
using HumanoidSkinLayoutDefinitions = std::array<HumanoidPartDefinitions, kHumanoidSkinLayoutCount>;

[[nodiscard]] std::size_t humanoidPartTypeIndex(ecs::StevePartType partType);
[[nodiscard]] std::size_t humanoidSkinLayoutIndex(ecs::EntitySkinLayoutKind skinLayout);
[[nodiscard]] const HumanoidSkinLayoutDefinitions& humanoidSkinLayoutDefinitions();

} // namespace renderer

#endif // MECRAFT_HUMANOID_SKIN_LAYOUT_CATALOG_H
