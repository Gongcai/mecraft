#include <cstdlib>
#include <iostream>

#include "../src/world/block/Block.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/redstone/WireContainerParts.h"

namespace {

int fail(const char* message) {
    std::cerr << "[wire_container_parts_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

WirePart part(const uint16_t channelId,
              const uint16_t facing,
              const uint8_t power = 0,
              const uint8_t connections = 0) {
    WirePart wirePart;
    wirePart.channelId = channelId;
    wirePart.facing = facing;
    wirePart.power = power;
    wirePart.connections = connections;
    return wirePart;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    const uint16_t redChannel =
        BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:redstone_wire")).redstoneWireChannelId;
    const uint16_t blueChannel =
        BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:blue_redstone_wire")).redstoneWireChannelId;
    if (redChannel == 0 || blueChannel == 0 || redChannel == blueChannel) {
        return fail("red and blue wire channels should be distinct non-zero ids");
    }

    WireContainerParts parts;
    if (!parts.addPart(part(redChannel,
                            PropIndices::FACING_FLOOR,
                            7,
                            WireConnectionBits::AXIS1_POS | WireConnectionBits::AXIS2_NEG))) {
        return fail("wire container should add the first red floor part");
    }
    if (parts.addPart(part(redChannel, PropIndices::FACING_FLOOR))) {
        return fail("wire container should reject duplicate channel/facing parts");
    }
    if (!parts.addPart(part(redChannel, PropIndices::FACING_NORTH)) ||
        !parts.addPart(part(blueChannel, PropIndices::FACING_FLOOR))) {
        return fail("wire container should allow different facings and different channels");
    }

    const WirePart* redFloor = parts.find(redChannel, PropIndices::FACING_FLOOR);
    if (redFloor == nullptr || redFloor->power != 7 ||
        redFloor->connections != (WireConnectionBits::AXIS1_POS | WireConnectionBits::AXIS2_NEG)) {
        return fail("wire container should find the stored red floor part");
    }
    if (parts.find(blueChannel, PropIndices::FACING_NORTH) != nullptr) {
        return fail("wire container should not find a missing channel/facing part");
    }

    if (!parts.setPower(redChannel, PropIndices::FACING_FLOOR, 15) ||
        parts.find(redChannel, PropIndices::FACING_FLOOR)->power != 15) {
        return fail("wire container should update part power");
    }
    if (parts.setPower(blueChannel, PropIndices::FACING_NORTH, 3)) {
        return fail("wire container should report missing part power updates");
    }

    std::size_t iterated = 0;
    parts.forEach([&](const WirePart&) {
        ++iterated;
    });
    if (iterated != parts.size() || parts.size() != 3) {
        return fail("wire container iteration should visit each stored part exactly once");
    }

    if (!parts.removePart(redChannel, PropIndices::FACING_NORTH) ||
        parts.find(redChannel, PropIndices::FACING_NORTH) != nullptr ||
        parts.size() != 2) {
        return fail("wire container should remove and compact parts by channel/facing");
    }

    WireContainerParts fullParts;
    const uint16_t facings[6] = {
        PropIndices::FACING_FLOOR,
        PropIndices::FACING_CEILING,
        PropIndices::FACING_NORTH,
        PropIndices::FACING_SOUTH,
        PropIndices::FACING_EAST,
        PropIndices::FACING_WEST,
    };
    for (uint16_t channel = 1; channel <= 4; ++channel) {
        for (const uint16_t facing : facings) {
            if (!fullParts.addPart(part(channel, facing))) {
                return fail("wire container should accept unique parts up to capacity");
            }
        }
    }
    if (fullParts.size() != WireContainerParts::MAX_PARTS ||
        fullParts.addPart(part(5, PropIndices::FACING_FLOOR))) {
        return fail("wire container should enforce its fixed part capacity");
    }

    WireContainerPartStore store;
    const glm::ivec3 posA(1, 2, 3);
    const glm::ivec3 posB(-4, 5, -6);
    if (!store.getOrCreate(posA).addPart(part(redChannel, PropIndices::FACING_FLOOR)) ||
        !store.getOrCreate(posB).addPart(part(blueChannel, PropIndices::FACING_WEST))) {
        return fail("wire container store should add parts at occupied positions");
    }
    if (store.size() != 2 ||
        store.find(posA) == nullptr ||
        store.find(posB) == nullptr ||
        store.find(glm::ivec3(0, 0, 0)) != nullptr) {
        return fail("wire container store should index parts by block position");
    }

    std::size_t visitedStores = 0;
    store.forEach([&](const glm::ivec3&, const WireContainerParts&) {
        ++visitedStores;
    });
    if (visitedStores != 2) {
        return fail("wire container store iteration should visit each occupied position");
    }

    const WireContainerParts extracted = store.extractAndErase(posA);
    if (extracted.find(redChannel, PropIndices::FACING_FLOOR) == nullptr ||
        store.find(posA) != nullptr ||
        store.size() != 1) {
        return fail("wire container store should extract and erase one position");
    }

    store.erase(posB);
    if (!store.empty()) {
        return fail("wire container store should erase positions");
    }

    std::cout << "[wire_container_parts_test] PASS\n";
    return EXIT_SUCCESS;
}
