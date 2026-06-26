#pragma once

#include <string>
#include <unordered_map>
#include <vector>

enum class ContainerStorageKind {
    BlockEntity,
    Transient
};

struct ContainerStorageDef {
    ContainerStorageKind kind = ContainerStorageKind::BlockEntity;
    int slots = 0;
};

struct ContainerSlotRuleDef {
    std::string id;
    int slot = 0;
    std::string accepts;
    bool outputOnly = false;
};

struct ContainerProcessorDef {
    std::string id;
    std::string type;
    int inputSlot = -1;
    int fuelSlot = -1;
    int outputSlot = -1;
    int gridSize = 0;
    int resultSlot = -1;
};

struct ContainerBehaviorDef {
    std::string id;
    std::string handler;
    ContainerStorageDef storage;
    std::vector<ContainerSlotRuleDef> slotRules;
    std::vector<ContainerProcessorDef> processors;
};

class ContainerBehaviorRegistry final {
public:
    static void init();
    static void ensureInitialized();
    [[nodiscard]] static const ContainerBehaviorDef& require(const std::string& id);
    [[nodiscard]] static const std::unordered_map<std::string, ContainerBehaviorDef>& all();

private:
    static bool s_initialized;
    static std::unordered_map<std::string, ContainerBehaviorDef> s_defs;
};
