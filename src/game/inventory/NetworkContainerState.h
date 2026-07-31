#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>

#include <glm/vec3.hpp>

#include "../states/GameStateMachine.h"
#include "../states/IGameState.h"
#include "engine/input/InputContextManager.h"
#include "BlockEntityInventoryStore.h"
#include "ContainerBehaviorRegistry.h"
#include "InventoryStateContext.h"
#include "MachineInventoryStore.h"
#include "../../client/GameClient.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../net/Protocol.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ui/inventory/ContainerUiRegistry.h"

[[noreturn]] inline void failNetworkContainerState(const std::string& message) {
    std::fputs(message.c_str(), stderr);
    std::fputc('\n', stderr);
    std::abort();
}

class NetworkContainerState final : public IGameState {
public:
    NetworkContainerState(InventoryStateContext deps, std::string containerUiId, const glm::ivec3 blockPosition)
        : m_deps(deps), m_containerUiId(std::move(containerUiId)), m_blockPosition(blockPosition) {}

    void onEnter() override {
        if (m_deps.gameClient == nullptr) {
            failNetworkContainerState("Network container state requires a game client");
        }

        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_deps.uiRenderer.setStoragePanelVisible(false);
        m_deps.uiRenderer.setMachinePanelVisible(false);

        ecs::PlayerQuery query(m_deps.ecsRegistry);
        m_deps.gameClient->sendContainerOpenRequest(m_blockPosition, query.getPosition());
    }

    void onExit() override {
        if (m_deps.gameClient != nullptr && m_containerId != 0) {
            m_deps.gameClient->sendContainerClose(m_containerId);
        }
        m_deps.input.clearUIDragItem();
        m_deps.uiRenderer.setStoragePanelVisible(false);
        m_deps.uiRenderer.setStoragePanelSource(nullptr);
        m_deps.uiRenderer.setMachinePanelVisible(false);
        m_deps.uiRenderer.setMachinePanelSource(nullptr);
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
            m_deps.input.captureMouse(true);
        }
    }

    void update(float /*dt*/, const InputSnapshot& snapshot) override {
        if (m_deps.gameClient == nullptr) {
            m_deps.fsm.popState();
            return;
        }

        if (m_containerId != 0 && m_deps.gameClient->consumeContainerClose(m_containerId)) {
            m_deps.fsm.popState();
            return;
        }

        const net::ContainerSnapshotMessage* remote = currentSnapshot();
        if (remote != nullptr) {
            applySnapshot(*remote);
        }

        const UIInputRouteResult uiRouteResult =
            UIInputAdapter::routeInput(m_deps.uiRenderer, snapshot, m_deps.context);

        if (m_deps.context.isActionTriggered(Action::Inventory) || m_deps.context.isActionTriggered(Action::Menu) ||
            (m_deps.context.isActionTriggered(Action::Cancel) && uiRouteResult.aggregate != UIEventResult::Consumed)) {
            m_deps.fsm.popState();
            return;
        }

        if (m_containerId == 0 || remote == nullptr) {
            return;
        }

        const bool secondaryHeld = m_deps.context.isActionHeld(Action::UISecondaryClick);
        if (!secondaryHeld) {
            m_lastSecondaryPlaceSlot = {};
        }

        if (uiRouteResult.secondaryPressed || secondaryHeld) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0 && dragged.count > 0) {
                const SlotRef hovered = hoveredSlot();
                if (hovered.valid() && hovered != m_lastSecondaryPlaceSlot) {
                    sendSlotAction(net::ContainerSlotActionType::SecondaryPlace, hovered);
                    m_lastSecondaryPlaceSlot = hovered;
                }
                m_deps.uiRenderer.clearStoragePanelActivations();
                m_deps.uiRenderer.clearMachinePanelActivations();
                return;
            }
        }

        if (!uiRouteResult.primaryPressed || uiRouteResult.primaryDown != UIEventResult::Consumed) {
            return;
        }

        const SlotRef slot = activatedSlot();
        if (slot.valid()) {
            sendSlotAction(net::ContainerSlotActionType::PrimaryClick, slot);
        }
        m_deps.uiRenderer.clearStoragePanelActivations();
        m_deps.uiRenderer.clearMachinePanelActivations();
    }

    [[nodiscard]] GameStateKind kind() const override { return GameStateKind::NetworkContainer; }

private:
    struct SlotRef {
        net::ContainerSlotSpace space = net::ContainerSlotSpace::None;
        int slot = -1;

        [[nodiscard]] bool valid() const { return space != net::ContainerSlotSpace::None && slot >= 0; }

        [[nodiscard]] bool operator!=(const SlotRef& other) const { return space != other.space || slot != other.slot; }
    };

    [[nodiscard]] const net::ContainerSnapshotMessage* currentSnapshot() const {
        if (m_containerId != 0) {
            return m_deps.gameClient->findContainerSnapshot(m_containerId);
        }
        return m_deps.gameClient->findContainerSnapshotAt(m_blockPosition);
    }

    void configurePanel(const net::ContainerSnapshotMessage& snapshot, const ContainerBehaviorDef& behavior) {
        if (m_panelConfigured) {
            return;
        }

        const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(snapshot.containerUiId);
        if (behavior.handler == "storage") {
            m_deps.uiRenderer.setStoragePanelDefinition(uiDef);
            m_deps.uiRenderer.setStoragePanelSource(&m_storageMirror);
            m_deps.uiRenderer.setStoragePanelVisible(true);
        } else if (behavior.handler == "smelting") {
            m_machineMirror = std::make_unique<MachineInventory>(static_cast<int>(snapshot.containerSlots.size()));
            m_deps.uiRenderer.setMachinePanelDefinition(uiDef);
            m_deps.uiRenderer.setMachinePanelSource(m_machineMirror.get());
            m_deps.uiRenderer.setMachinePanelVisible(true);
        } else {
            failNetworkContainerState("Network container does not support behavior handler: " + behavior.handler);
        }
        m_panelConfigured = true;
    }

    void applySnapshot(const net::ContainerSnapshotMessage& snapshot) {
        if (snapshot.blockPosition != m_blockPosition) {
            return;
        }

        m_containerId = snapshot.containerId;
        const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(snapshot.behaviorId);
        configurePanel(snapshot, behavior);
        m_containerSlotCount = static_cast<int>(snapshot.containerSlots.size());

        if (behavior.handler == "storage") {
            for (int slot = 0; slot < BlockEntityInventory::SLOT_COUNT; ++slot) {
                m_storageMirror.setSlotStack(slot, {});
            }
            for (int slot = 0; slot < static_cast<int>(snapshot.containerSlots.size()); ++slot) {
                m_storageMirror.setSlotStack(slot, stackFromSlotData(snapshot.containerSlots[slot]));
            }
        } else if (behavior.handler == "smelting") {
            if (m_machineMirror == nullptr ||
                m_machineMirror->slotCount() != static_cast<int>(snapshot.containerSlots.size())) {
                m_machineMirror = std::make_unique<MachineInventory>(static_cast<int>(snapshot.containerSlots.size()));
                m_deps.uiRenderer.setMachinePanelSource(m_machineMirror.get());
            }
            for (int slot = 0; slot < m_machineMirror->slotCount(); ++slot) {
                m_machineMirror->setSlotStack(slot, stackFromSlotData(snapshot.containerSlots[slot]));
            }
            m_machineMirror->setProgress(snapshot.burnFraction, 1.0f, snapshot.cookFraction, 1.0f);
            m_deps.uiRenderer.setMachinePanelProgress(snapshot.burnFraction, snapshot.cookFraction);
        }

        for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
            const ItemStack stack = slot < static_cast<int>(snapshot.playerSlots.size())
                                        ? stackFromSlotData(snapshot.playerSlots[slot])
                                        : ItemStack{};
            m_deps.inventory.setSlotStack(slot, stack);
        }

        if (snapshot.cursor.itemId != 0 && snapshot.cursor.stackCount != 0) {
            m_deps.input.beginUIDragItem(snapshot.cursor.itemId, snapshot.cursor.stackCount, -1);
        } else {
            m_deps.input.clearUIDragItem();
        }
    }

    [[nodiscard]] SlotRef activatedSlot() const {
        const bool machine = m_machineMirror != nullptr;
        const int containerSlot = machine ? m_deps.uiRenderer.getMachinePanelLastActivatedSlot()
                                          : m_deps.uiRenderer.getStoragePanelLastActivatedSlot();
        if (containerSlot >= 0 && containerSlot < m_containerSlotCount) {
            return {net::ContainerSlotSpace::Container, containerSlot};
        }

        const int playerSlot = machine ? m_deps.uiRenderer.getMachinePanelPlayerLastActivatedSlot()
                                       : m_deps.uiRenderer.getStoragePanelPlayerLastActivatedSlot();
        if (m_deps.inventory.isValidSlot(playerSlot)) {
            return {net::ContainerSlotSpace::Player, playerSlot};
        }
        return {};
    }

    [[nodiscard]] SlotRef hoveredSlot() const {
        const bool machine = m_machineMirror != nullptr;
        const int containerSlot =
            machine ? m_deps.uiRenderer.getMachinePanelHoveredSlot() : m_deps.uiRenderer.getStoragePanelHoveredSlot();
        if (containerSlot >= 0 && containerSlot < m_containerSlotCount) {
            return {net::ContainerSlotSpace::Container, containerSlot};
        }

        const int playerSlot = machine ? m_deps.uiRenderer.getMachinePanelPlayerHoveredSlot()
                                       : m_deps.uiRenderer.getStoragePanelPlayerHoveredSlot();
        if (m_deps.inventory.isValidSlot(playerSlot)) {
            return {net::ContainerSlotSpace::Player, playerSlot};
        }
        return {};
    }

    void sendSlotAction(const net::ContainerSlotActionType action, const SlotRef slot) {
        net::ClientContainerSlotAction message;
        message.containerId = m_containerId;
        message.action = action;
        message.slotSpace = slot.space;
        message.slot = static_cast<int16_t>(slot.slot);
        m_deps.gameClient->sendContainerSlotAction(message);
    }

    [[nodiscard]] static ItemStack stackFromSlotData(const net::InventorySlotData& slot) {
        ItemStack stack;
        if (slot.itemId != 0 && slot.stackCount != 0) {
            stack.itemId = static_cast<ItemID>(slot.itemId);
            stack.count = slot.stackCount;
        }
        return stack;
    }

    InventoryStateContext m_deps;
    std::string m_containerUiId;
    glm::ivec3 m_blockPosition{};
    uint32_t m_containerId = 0;
    bool m_panelConfigured = false;
    int m_containerSlotCount = 0;
    SlotRef m_lastSecondaryPlaceSlot;
    BlockEntityInventory m_storageMirror;
    std::unique_ptr<MachineInventory> m_machineMirror;
};
