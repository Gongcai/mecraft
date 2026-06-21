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
    require(ecs::normalizeEntitySkinLayoutId("") == "minecraft:steve_64x64",
            "empty skin layout should normalize to the modern Steve layout");
    require(ecs::normalizeEntitySkinLayoutId("steve_64x64") == "minecraft:steve_64x64",
            "short Steve layout id should normalize to a namespaced id");
    require(ecs::normalizeEntitySkinLayoutId("classic_64x32") == "minecraft:classic_64x32",
            "short classic layout id should normalize to a namespaced id");
    require(!ecs::entitySkinLayoutUsesMirroredLeftLimbs("minecraft:steve_64x64"),
            "modern 64x64 skins should use explicit left limb UVs");
    require(ecs::entitySkinLayoutUsesMirroredLeftLimbs("minecraft:classic_64x32"),
            "classic 64x32 skins should mirror left limbs from right limbs");

    std::printf("All EntitySkinLayout tests passed!\n");
    return 0;
}
