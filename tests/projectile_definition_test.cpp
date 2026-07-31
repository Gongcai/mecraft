#include <cstdlib>
#include <iostream>

#include "../src/ecs/util/ProjectileDefinitions.h"
#include "../src/item/Item.h"

namespace {

int fail(const char* message) {
    std::cerr << "[projectile_definition_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    ItemRegistry::init();

    ecs::ProjectileDefinition apple;
    if (!ecs::getThrowableProjectileDefinition(ItemRegistry::requireIdByName("minecraft:apple"), apple)) {
        return fail("apple should load as a throwable projectile from projectiles.json");
    }
    if (apple.itemId != ItemRegistry::requireIdByName("minecraft:apple") || apple.damage != 5 ||
        apple.entityImpactParticleBlock != BlockRegistry::requireIdByName("minecraft:rose") ||
        apple.entityImpactParticleCount != 14 || apple.throwSoundId != "item.apple.throw" ||
        apple.impactSoundId != "item.apple.impact") {
        return fail("apple projectile should match configured values");
    }

    ecs::ProjectileDefinition coal;
    if (ecs::getThrowableProjectileDefinition(ItemRegistry::requireIdByName("minecraft:coal"), coal)) {
        return fail("coal should not be throwable without a projectile definition");
    }

    const ecs::ProjectileDefinition fallback =
        ecs::projectileDefinitionForItemOrDefault(ItemRegistry::requireIdByName("minecraft:coal"));
    if (fallback.itemId != ItemRegistry::requireIdByName("minecraft:coal") ||
        fallback.entityImpactParticleBlock != ecs::defaultProjectileEntityImpactParticleBlock() ||
        !fallback.throwSoundId.empty() || !fallback.impactSoundId.empty()) {
        return fail("fallback projectile definition should only preserve item and generic impact particles");
    }

    std::cout << "[projectile_definition_test] PASS\n";
    return EXIT_SUCCESS;
}
