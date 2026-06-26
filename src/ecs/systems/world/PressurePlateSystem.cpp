#include "PressurePlateSystem.h"

#include "../../GameplayRegistry.h"
#include "../../components/Components.h"
#include "../../util/SimulationDistance.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace ecs {

namespace {

constexpr float kPlateInset = 1.0f / 16.0f;
constexpr float kPlateMaxContactHeight = 0.25f;
constexpr float kCellBoundaryEpsilon = 0.001f;

struct IVec3Hash {
    std::size_t operator()(const glm::ivec3& value) const noexcept {
        std::size_t seed = 0;
        const auto mix = [&seed](const int component) {
            seed ^= std::hash<int>{}(component) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };
        mix(value.x);
        mix(value.y);
        mix(value.z);
        return seed;
    }
};

using PositionSet = std::unordered_set<glm::ivec3, IVec3Hash>;

struct PressurePlateRuntimeState {
    PositionSet activePlates;
};

struct EntityContactBox {
    glm::vec3 min{};
    glm::vec3 max{};
};

enum class PressurePlateEntityKind : uint8_t {
    Living,
    LooseItem
};

uint16_t getRequiredProperty(const StateID stateId, const uint16_t property, const char* propertyName) {
    if (property == PropIndices::INVALID) {
        throw std::runtime_error(std::string("Pressure plates require registered property: ") + propertyName);
    }
    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(std::string("Pressure plate state is missing property: ") + propertyName);
    }
    return value;
}

StateID withPowered(const StateID stateId, const bool powered) {
    const uint16_t value = powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Pressure plates require registered powered boolean values");
    }

    const uint16_t current = getRequiredProperty(stateId, PropIndices::POWERED, "powered");
    if (current == value) {
        return stateId;
    }

    const StateID updatedState = BlockStateRegistry::withProperty(stateId, PropIndices::POWERED, value);
    if (BlockStateRegistry::getPropertyIndex(updatedState, PropIndices::POWERED) != value) {
        throw std::runtime_error("Pressure plate powered state transition failed");
    }
    return updatedState;
}

bool isPressurePlateState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "plate";
}

bool isWoodenPressurePlateState(const StateID stateId) {
    static const BlockID oakPressurePlateBlock = BlockRegistry::requireIdByName("minecraft:oak_pressure_plate");
    return BlockStateRegistry::getBlockId(stateId) == oakPressurePlateBlock;
}

bool pressurePlateAcceptsEntity(const StateID stateId, const PressurePlateEntityKind entityKind) {
    if (entityKind == PressurePlateEntityKind::Living) {
        return true;
    }
    return isWoodenPressurePlateState(stateId);
}

bool horizontalFootprintIntersectsPlate(const EntityContactBox& box, const glm::ivec3& platePos) {
    const float plateMinX = static_cast<float>(platePos.x) + kPlateInset;
    const float plateMaxX = static_cast<float>(platePos.x + 1) - kPlateInset;
    const float plateMinZ = static_cast<float>(platePos.z) + kPlateInset;
    const float plateMaxZ = static_cast<float>(platePos.z + 1) - kPlateInset;

    return box.min.x < plateMaxX && box.max.x > plateMinX &&
           box.min.z < plateMaxZ && box.max.z > plateMinZ;
}

bool bottomTouchesPlateHeight(const EntityContactBox& box, const glm::ivec3& platePos) {
    const float plateY = static_cast<float>(platePos.y);
    return box.min.y >= plateY - kCellBoundaryEpsilon &&
           box.min.y <= plateY + kPlateMaxContactHeight;
}

void addContactingPlates(const World& world,
                         const EntityContactBox& box,
                         const PressurePlateEntityKind entityKind,
                         PositionSet& occupiedPlates) {
    const int y = static_cast<int>(std::floor(box.min.y + kCellBoundaryEpsilon));
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kCellBoundaryEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kCellBoundaryEpsilon));

    for (int x = minX; x <= maxX; ++x) {
        for (int z = minZ; z <= maxZ; ++z) {
            const glm::ivec3 platePos(x, y, z);
            const StateID stateId = world.getBlockState(platePos.x, platePos.y, platePos.z);
            if (!isPressurePlateState(stateId)) {
                continue;
            }
            if (!pressurePlateAcceptsEntity(stateId, entityKind)) {
                continue;
            }
            if (!horizontalFootprintIntersectsPlate(box, platePos) ||
                !bottomTouchesPlateHeight(box, platePos)) {
                continue;
            }
            occupiedPlates.insert(platePos);
        }
    }
}

EntityContactBox contactBoxFromPhysicsBody(const PhysicsBody& body) {
    const glm::vec3 center = body.position + body.colliderOffset;
    return {center - body.halfExtents, center + body.halfExtents};
}

EntityContactBox contactBoxFromBounds(const TransformComponent& transform, const BoundsComponent& bounds) {
    return {transform.position - bounds.halfExtents, transform.position + bounds.halfExtents};
}

void collectLivingPressurePlateContacts(SystemContext& ctx, PositionSet& occupiedPlates) {
    const World& world = *ctx.services.world;
    auto& reg = ctx.registry.registry();

    auto playerView = reg.view<LocalPlayerTag, PhysicsBodyComponent>();
    for (const entt::entity entity : playerView) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }
        const auto& body = playerView.get<PhysicsBodyComponent>(entity).body;
        addContactingPlates(world, contactBoxFromPhysicsBody(body), PressurePlateEntityKind::Living, occupiedPlates);
    }

    auto mobView = reg.view<MobTag, PhysicsBodyComponent>();
    for (const entt::entity entity : mobView) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }
        const auto& body = mobView.get<PhysicsBodyComponent>(entity).body;
        addContactingPlates(world, contactBoxFromPhysicsBody(body), PressurePlateEntityKind::Living, occupiedPlates);
    }
}

void collectLooseItemPressurePlateContacts(SystemContext& ctx, PositionSet& occupiedPlates) {
    const World& world = *ctx.services.world;
    auto& reg = ctx.registry.registry();

    auto dropView = reg.view<DropItemTag, TransformComponent, BoundsComponent>();
    for (const entt::entity entity : dropView) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }
        const auto& transform = dropView.get<TransformComponent>(entity);
        const auto& bounds = dropView.get<BoundsComponent>(entity);
        addContactingPlates(world, contactBoxFromBounds(transform, bounds), PressurePlateEntityKind::LooseItem, occupiedPlates);
    }

    auto projectileView = reg.view<ProjectileTag, TransformComponent, BoundsComponent>();
    for (const entt::entity entity : projectileView) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }
        const auto& transform = projectileView.get<TransformComponent>(entity);
        const auto& bounds = projectileView.get<BoundsComponent>(entity);
        addContactingPlates(world, contactBoxFromBounds(transform, bounds), PressurePlateEntityKind::LooseItem, occupiedPlates);
    }

    auto fallingBlockView = reg.view<FallingBlockTag, TransformComponent, BoundsComponent>();
    for (const entt::entity entity : fallingBlockView) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }
        const auto& transform = fallingBlockView.get<TransformComponent>(entity);
        const auto& bounds = fallingBlockView.get<BoundsComponent>(entity);
        addContactingPlates(world, contactBoxFromBounds(transform, bounds), PressurePlateEntityKind::LooseItem, occupiedPlates);
    }
}

size_t applyPressurePlateState(World& world, const glm::ivec3& position, const bool powered) {
    const StateID currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isPressurePlateState(currentState)) {
        return 0;
    }

    const StateID updatedState = withPowered(currentState, powered);
    if (updatedState == currentState) {
        return 0;
    }

    world.setBlockState(position.x, position.y, position.z, updatedState);
    return 1;
}

size_t applyPressurePlateStates(World& world,
                                PressurePlateRuntimeState& runtime,
                                const PositionSet& occupiedPlates) {
    std::vector<glm::ivec3> releasedPlates;
    releasedPlates.reserve(runtime.activePlates.size());
    for (const glm::ivec3& position : runtime.activePlates) {
        if (occupiedPlates.find(position) == occupiedPlates.end()) {
            releasedPlates.push_back(position);
        }
    }

    size_t changed = 0;
    for (const glm::ivec3& position : releasedPlates) {
        changed += applyPressurePlateState(world, position, false);
        runtime.activePlates.erase(position);
    }

    for (const glm::ivec3& position : occupiedPlates) {
        changed += applyPressurePlateState(world, position, true);
        runtime.activePlates.insert(position);
    }

    return changed;
}

PressurePlateRuntimeState& runtimeState(GameplayRegistry& registry) {
    if (!registry.ctxHas<PressurePlateRuntimeState>()) {
        registry.ctxSet<PressurePlateRuntimeState>();
    }
    return registry.ctxGet<PressurePlateRuntimeState>();
}

} // namespace

void PressurePlateSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) {
        return;
    }
    if (ctx.services.gameClient) {
        return;
    }

    processWorldEntities(*ctx.services.world, ctx.registry);
}

size_t PressurePlateSystem::processWorldEntities(World& world, GameplayRegistry& registry) {
    GameplayServices services;
    services.world = &world;
    SystemContext context{registry, services, 0.0f, 0};

    PositionSet occupiedPlates;
    collectLivingPressurePlateContacts(context, occupiedPlates);
    collectLooseItemPressurePlateContacts(context, occupiedPlates);

    return applyPressurePlateStates(world, runtimeState(registry), occupiedPlates);
}

} // namespace ecs
