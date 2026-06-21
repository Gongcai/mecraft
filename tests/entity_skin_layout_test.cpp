#include "ecs/entity/EntitySkinLayout.h"

#include <cstdio>
#include <cstdlib>

static void require(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

int main() {
    const auto steve = ecs::tryParseEntitySkinLayoutId("minecraft:steve_64x64");
    require(steve.has_value(), "Steve skin layout id should parse");
    require(ecs::entitySkinLayoutId(*steve) == "minecraft:steve_64x64",
            "Steve skin layout should keep its canonical id");
    const auto classic64x64 = ecs::tryParseEntitySkinLayoutId("minecraft:classic_64x64");
    require(classic64x64.has_value(), "classic 64x64 skin layout id should parse");
    require(ecs::entitySkinLayoutId(*classic64x64) == "minecraft:classic_64x64",
            "classic 64x64 skin layout should keep its canonical id");
    const auto classic64x32 = ecs::tryParseEntitySkinLayoutId("minecraft:classic_64x32");
    require(classic64x32.has_value(), "classic 64x32 skin layout id should parse");
    require(ecs::entitySkinLayoutId(*classic64x32) == "minecraft:classic_64x32",
            "classic 64x32 skin layout should keep its canonical id");
    require(!ecs::tryParseEntitySkinLayoutId("").has_value(),
            "empty skin layout id should be rejected");
    require(!ecs::tryParseEntitySkinLayoutId("steve_64x64").has_value(),
            "short skin layout id should be rejected");
    require(!ecs::entitySkinLayoutUsesMirroredLeftLimbs(ecs::EntitySkinLayoutKind::Steve64x64),
            "modern 64x64 skins should use explicit left limb UVs");
    require(ecs::entitySkinLayoutUsesMirroredLeftLimbs(ecs::EntitySkinLayoutKind::Classic64x64),
            "classic 64x64 skins should mirror left limbs from right limbs");
    require(ecs::entitySkinLayoutUsesMirroredLeftLimbs(ecs::EntitySkinLayoutKind::Classic64x32),
            "classic 64x32 skins should mirror left limbs from right limbs");

    std::printf("All EntitySkinLayout tests passed!\n");
    return 0;
}
