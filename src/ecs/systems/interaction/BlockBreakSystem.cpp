#include "BlockBreakSystem.h"

#include <algorithm>
#include <string_view>

#include "../../util/AudioEventBuffer.h"
#include "../../util/DropSpawnEventBuffer.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../components/Components.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../game/inventory/ChestInventoryLifecycle.h"
#include "../../../game/inventory/FurnaceInventoryLifecycle.h"
#include "../../../client/GameClient.h"
#include "../../../item/Item.h"
#include "../../../world/IWorldView.h"
#include "../../../world/World.h"

namespace ecs {

namespace {

const IGameplayModeRules& resolveModeRules(const GameplayRegistry& registry) {
    if (registry.ctxHas<GameplayRuntimeContext>()) {
        const auto& runtime = registry.ctxGet<GameplayRuntimeContext>();
        if (runtime.modeRules != nullptr) {
            return *runtime.modeRules;
        }
    }
    return SurvivalModeRules::instance();
}

void resetBreakSession(BlockBreakComponent& blockBreak,
                       BlockInteractionRuntimeComponent& runtime) {
    runtime.breakActive = false;
    runtime.breakElapsedMs = 0.0f;
    runtime.breakRequiredMs = 0.0f;
    runtime.breakBlockPos = glm::ivec3{};
    blockBreak.active = false;
    blockBreak.blockPos = glm::ivec3{};
    blockBreak.progress01 = 0.0f;
}

std::string_view requiredToolKind(const BlockDef& blockDef) {
    switch (blockDef.materialKind) {
        case BlockMaterialKinds::STONE:
        case BlockMaterialKinds::ORE:
        case BlockMaterialKinds::METAL:
            return "pickaxe";
        case BlockMaterialKinds::WOOD:
            return "axe";
        case BlockMaterialKinds::DIRT:
        case BlockMaterialKinds::GRASS:
        case BlockMaterialKinds::SAND:
            return "shovel";
        default:
            return {};
    }
}

float survivalBreakDurationMs(const BlockID targetBlock, const ItemStack& heldStack) {
    const BlockDef& blockDef = BlockRegistry::get(targetBlock);
    const float baseDurationMs = std::max(1.0f, static_cast<float>(blockDef.timeToBreak));
    const std::string_view requiredKind = requiredToolKind(blockDef);

    if (heldStack.isEmpty() || requiredKind.empty()) {
        return baseDurationMs;
    }

    const ItemDef& itemDef = ItemRegistry::get(heldStack.itemId);
    if (!itemDef.isTool || itemDef.toolKind != requiredKind) {
        return baseDurationMs;
    }

    return std::max(1.0f, baseDurationMs / itemDef.toolEfficiency);
}

void applyToolWear(Inventory& inventory) {
    ItemStack heldStack = inventory.getSelectedStack();
    if (heldStack.isEmpty()) {
        return;
    }

    const ItemDef& itemDef = ItemRegistry::get(heldStack.itemId);
    if (!itemDef.isTool || itemDef.maxDurability == 0) {
        return;
    }

    if (heldStack.durability == 0) {
        heldStack.durability = itemDef.maxDurability;
    }

    --heldStack.durability;
    if (heldStack.durability == 0) {
        inventory.setSlotStack(inventory.getSelectedSlot(), {});
        return;
    }

    inventory.setSlotStack(inventory.getSelectedSlot(), heldStack);
}

} // namespace

void BlockBreakSystem::update(SystemContext& ctx) {
    if (!ctx.services.worldView) return;
    const auto& worldView = *ctx.services.worldView;
    World* mutableWorld = ctx.services.world.get();
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    const IGameplayModeRules& modeRules = resolveModeRules(registry);
    auto& audioBus = ensureAudioEventBus(registry);
    auto& particleBus = ensureParticleEventBus(registry);
    auto& dropBus = ensureDropSpawnEventBus(registry);

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              BlockTargetComponent,
                              BlockBreakComponent,
                              BlockInteractionRuntimeComponent,
                              InventoryComponent,
                              InventoryDataComponent,
                              TransformComponent>();
    for (auto e : view) {
        auto& runtime = view.get<BlockInteractionRuntimeComponent>(e);
        auto& blockBreak = view.get<BlockBreakComponent>(e);
        const auto& intent = view.get<BlockActionIntentComponent>(e);
        const auto& target = view.get<BlockTargetComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);
        const auto& inventoryState = view.get<InventoryComponent>(e);
        auto& inventoryData = view.get<InventoryDataComponent>(e);

        inventoryData.inventory.setSelectedSlot(inventoryState.selectedHotbarSlot);

        runtime.creativeBreakCooldownRemaining =
            std::max(0.0f, runtime.creativeBreakCooldownRemaining - dt);

        if (!intent.wantsBreak || !target.hasTarget) {
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        const glm::ivec3 hitBlock = target.targetBlock;

        // Server-side distance validation: player must be within reach
        constexpr float kMaxBreakDistance = 6.5f;
        const glm::vec3 blockCenter = glm::vec3(hitBlock) + glm::vec3(0.5f);
        const glm::vec3 diff = transform.position - blockCenter;
        const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
        if (distSq > kMaxBreakDistance * kMaxBreakDistance) {
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        const BlockID targetBlock = worldView.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
        if (targetBlock == 0 || !BlockRegistry::get(targetBlock).isSelectable) {
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        if (!modeRules.shouldReportBreakProgress()) {
            // Creative instant break
            if (runtime.creativeBreakCooldownRemaining > 0.0f) continue;
            if (mutableWorld == nullptr) {
                if (ctx.services.gameClient) {
                    net::ClientBlockAction action;
                    action.sequence = ++runtime.heldItemSwingSequence;
                    action.action = net::ClientBlockActionType::Break;
                    action.targetBlock = hitBlock;
                    action.placeBlock = target.placeBlock;
                    action.hitNormal = target.hitNormal;
                    action.playerPosition = transform.position;
                    ctx.services.gameClient->sendBlockAction(action);
                } else {
                    ++runtime.heldItemSwingSequence;
                }
                runtime.creativeBreakCooldownRemaining = modeRules.breakDurationMs(targetBlock) / 1000.0f;
                resetBreakSession(blockBreak, runtime);
                continue;
            }

            const BlockID brokenBlock = mutableWorld->getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            mutableWorld->setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            const bool handledChest = handleChestInventoryBreak(registry, brokenBlock, hitBlock, false);
            const bool handledFurnace = handleFurnaceInventoryBreak(registry, brokenBlock, hitBlock, false);
            static_cast<void>(handledChest);
            static_cast<void>(handledFurnace);
            audioBus.push({"block.generic.break", glm::vec3(hitBlock), true, 1.0f});
            particleBus.push({hitBlock, brokenBlock});
            runtime.creativeBreakCooldownRemaining = modeRules.breakDurationMs(targetBlock) / 1000.0f;
            ++runtime.heldItemSwingSequence;
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        // Survival break with progress
        const float requiredMs = survivalBreakDurationMs(targetBlock, inventoryData.inventory.getSelectedStack());
        if (!runtime.breakActive || runtime.breakBlockPos != hitBlock) {
            runtime.breakActive = true;
            runtime.breakBlockPos = hitBlock;
            runtime.breakElapsedMs = 0.0f;
            runtime.breakRequiredMs = requiredMs;
        }
        runtime.breakRequiredMs = requiredMs;

        runtime.breakElapsedMs += dt * 1000.0f;
        blockBreak.active = true;
        blockBreak.blockPos = hitBlock;
        blockBreak.progress01 = std::clamp(runtime.breakElapsedMs / runtime.breakRequiredMs, 0.0f, 1.0f);

        if (runtime.breakElapsedMs >= runtime.breakRequiredMs) {
            if (mutableWorld == nullptr) {
                if (ctx.services.gameClient) {
                    net::ClientBlockAction action;
                    action.sequence = ++runtime.heldItemSwingSequence;
                    action.action = net::ClientBlockActionType::Break;
                    action.targetBlock = hitBlock;
                    action.placeBlock = target.placeBlock;
                    action.hitNormal = target.hitNormal;
                    action.playerPosition = transform.position;
                    ctx.services.gameClient->sendBlockAction(action);
                } else {
                    ++runtime.heldItemSwingSequence;
                }
                resetBreakSession(blockBreak, runtime);
                continue;
            }
            const BlockID brokenBlock = mutableWorld->getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            mutableWorld->setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            const bool handledChest = handleChestInventoryBreak(registry, brokenBlock, hitBlock, true);
            const bool handledFurnace = handleFurnaceInventoryBreak(registry, brokenBlock, hitBlock, true);
            static_cast<void>(handledChest);
            static_cast<void>(handledFurnace);
            audioBus.push({"block.generic.break", glm::vec3(hitBlock), true, 1.0f});
            particleBus.push({hitBlock, brokenBlock});
            dropBus.push({brokenBlock, hitBlock});
            applyToolWear(inventoryData.inventory);
            ++runtime.heldItemSwingSequence;
            resetBreakSession(blockBreak, runtime);
        }
    }
}

} // namespace ecs
