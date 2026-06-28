#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "game/camera/CameraController.h"
#include "world/IWorldView.h"

namespace {

class FakeWorldView final : public IWorldView {
public:
    void addSolidBlock(const glm::ivec3& pos) {
        m_solidBlocks.push_back(pos);
    }

    [[nodiscard]] const ChunkMap& getActiveChunks() const override {
        return m_chunks;
    }

    [[nodiscard]] uint64_t getActiveChunkRevision() const override {
        return 1;
    }

    [[nodiscard]] uint64_t getBlockContentRevision() const override {
        return 1;
    }

    [[nodiscard]] BlockStateId getBlock(const int x, const int y, const int z) const override {
        const glm::ivec3 pos(x, y, z);
        for (const glm::ivec3& block : m_solidBlocks) {
            if (block == pos) {
                return BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone"));
            }
        }
        return NULL_BLOCK_STATE;
    }

    [[nodiscard]] uint8_t getPackedLight(int, int, int) const override {
        return 0;
    }

    [[nodiscard]] BlockStateId getBlockState(const int x, const int y, const int z) const override {
        return getBlock(x, y, z);
    }

    [[nodiscard]] BlockStateId getFluidState(int, int, int) const override {
        return NULL_BLOCK_STATE;
    }

    [[nodiscard]] bool isChunkLoadedForBlock(int, int, int) const override {
        return true;
    }

    [[nodiscard]] int getRenderDistance() const override {
        return 8;
    }

    [[nodiscard]] glm::ivec2 getChunkCoords(const int worldX, const int worldZ) const override {
        return {worldX >> 4, worldZ >> 4};
    }

    [[nodiscard]] TerrainBiome getBiome(int, int) const override {
        return TerrainBiome::Temperate;
    }

private:
    ChunkMap m_chunks;
    std::vector<glm::ivec3> m_solidBlocks;
};

int fail(const char* message) {
    std::cerr << "[camera_controller_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool nearlyEqual(const float a, const float b, const float epsilon = 0.001f) {
    return std::abs(a - b) <= epsilon;
}

bool nearlyEqualVec3(const glm::vec3& a, const glm::vec3& b, const float epsilon = 0.001f) {
    return nearlyEqual(a.x, b.x, epsilon) &&
           nearlyEqual(a.y, b.y, epsilon) &&
           nearlyEqual(a.z, b.z, epsilon);
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    Camera eyeCamera;
    const glm::vec3 eyePosition(0.5f, 1.5f, 0.5f);
    eyeCamera.setPosition(eyePosition);
    eyeCamera.setYawPitch(-90.0f, 0.0f);

    CameraController controller;
    controller.setViewMode(CameraController::ViewMode::ThirdPerson);
    controller.setThirdPersonDistance(4.0f);
    controller.setThirdPersonHeight(0.0f);

    {
        const FakeWorldView worldView;
        const Camera renderCamera = controller.computeRenderCamera(eyeCamera, eyePosition, worldView);
        if (!nearlyEqualVec3(renderCamera.getPosition(), glm::vec3(0.5f, 1.5f, 4.5f))) {
            return fail("unobstructed third-person camera should keep the configured distance");
        }
    }

    {
        FakeWorldView worldView;
        worldView.addSolidBlock(glm::ivec3(0, 1, 2));
        const Camera renderCamera = controller.computeRenderCamera(eyeCamera, eyePosition, worldView);
        if (renderCamera.getPosition().z >= 2.0f) {
            return fail("third-person camera should stop before the blocking voxel");
        }
        if (!nearlyEqual(renderCamera.getPosition().z, 1.82f, 0.01f)) {
            return fail("third-person camera should leave a small gap before the hit surface");
        }
    }

    std::cout << "[camera_controller_test] PASS\n";
    return EXIT_SUCCESS;
}
