#include "renderer/renderers/HumanoidSkinLayoutCatalog.h"
#include "Paths.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>

struct PngDimensions {
    uint32_t width;
    uint32_t height;
};

static void require(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static bool rectEquals(const renderer::HumanoidSkinPixelRect& lhs,
                       const renderer::HumanoidSkinPixelRect& rhs) {
    return lhs.x0 == rhs.x0 &&
           lhs.y0 == rhs.y0 &&
           lhs.x1 == rhs.x1 &&
           lhs.y1 == rhs.y1;
}

static void requireRectInsideTexture(const renderer::HumanoidSkinPixelRect& rect,
                                     const float textureWidth,
                                     const float textureHeight) {
    require(rect.x0 >= 0.0f && rect.x0 < rect.x1, "UV rect x range should be valid");
    require(rect.y0 >= 0.0f && rect.y0 < rect.y1, "UV rect y range should be valid");
    require(rect.x1 <= textureWidth, "UV rect x range should fit texture width");
    require(rect.y1 <= textureHeight, "UV rect y range should fit texture height");
}

static PngDimensions readPngDimensions(const char* path) {
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "entity texture PNG should open");

    std::array<unsigned char, 24> header{};
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    require(file.good(), "entity texture PNG should include an IHDR header");

    constexpr std::array<unsigned char, 8> signature{{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}};
    for (std::size_t i = 0; i < signature.size(); ++i) {
        require(header[i] == signature[i], "entity texture should use PNG signature");
    }
    require(header[12] == 'I' && header[13] == 'H' && header[14] == 'D' && header[15] == 'R',
            "entity texture PNG should start with IHDR");

    const auto readBigEndianU32 = [&header](const std::size_t offset) {
        return (static_cast<uint32_t>(header[offset]) << 24U) |
               (static_cast<uint32_t>(header[offset + 1]) << 16U) |
               (static_cast<uint32_t>(header[offset + 2]) << 8U) |
               static_cast<uint32_t>(header[offset + 3]);
    };

    return {readBigEndianU32(16), readBigEndianU32(20)};
}

static void requireTextureMatchesLayout(const PngDimensions dimensions,
                                        const renderer::HumanoidSkinLayoutDefinition& layout,
                                        const char* message) {
    require(static_cast<float>(dimensions.width) == layout.textureWidth, message);
    require(static_cast<float>(dimensions.height) == layout.textureHeight, message);
}

int main() {
    const renderer::HumanoidSkinLayoutDefinitions& definitions = renderer::humanoidSkinLayoutDefinitions();

    const auto steveIndex = renderer::humanoidSkinLayoutIndex(ecs::EntitySkinLayoutKind::Steve64x64);
    const auto classic64x64Index = renderer::humanoidSkinLayoutIndex(ecs::EntitySkinLayoutKind::Classic64x64);
    const auto classic64x32Index = renderer::humanoidSkinLayoutIndex(ecs::EntitySkinLayoutKind::Classic64x32);
    const auto leftArmIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::LeftArm);
    const auto rightArmIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::RightArm);
    const auto leftLegIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::LeftLeg);
    const auto rightLegIndex = renderer::humanoidPartTypeIndex(ecs::StevePartType::RightLeg);

    require(steveIndex != classic64x64Index, "skin layout indexes should be distinct");
    require(classic64x64Index != classic64x32Index, "classic skin layout indexes should be distinct");
    require(leftArmIndex != rightArmIndex, "arm part indexes should be distinct");
    require(leftLegIndex != rightLegIndex, "leg part indexes should be distinct");

    const auto& steveLayout = definitions[steveIndex];
    const auto& classic64x64Layout = definitions[classic64x64Index];
    const auto& classic64x32Layout = definitions[classic64x32Index];

    require(steveLayout.textureWidth == 64.0f && steveLayout.textureHeight == 64.0f,
            "Steve layout should declare a 64x64 texture");
    require(classic64x64Layout.textureWidth == 64.0f && classic64x64Layout.textureHeight == 64.0f,
            "classic 64x64 layout should declare the current zombie texture canvas");
    require(classic64x32Layout.textureWidth == 64.0f && classic64x32Layout.textureHeight == 32.0f,
            "classic 64x32 layout should declare the legacy texture canvas");

    requireTextureMatchesLayout(readPngDimensions(STEVE_TEXTURE_PATH), steveLayout,
                                "Steve texture should match its declared skin layout canvas");
    requireTextureMatchesLayout(readPngDimensions(MOBS_TEXTURE_DIR "/zombie.png"), classic64x64Layout,
                                "zombie texture should match its configured skin layout canvas");
    requireTextureMatchesLayout(readPngDimensions(MOBS_TEXTURE_DIR "/herobrine.png"), steveLayout,
                                "Herobrine texture should match its configured skin layout canvas");

    for (const auto& layout : definitions) {
        for (const auto& part : layout.parts) {
            require(part.halfWidth > 0.0f, "part half width should be positive");
            require(part.halfHeight > 0.0f, "part half height should be positive");
            require(part.halfDepth > 0.0f, "part half depth should be positive");
            for (const auto& rect : part.faceUvs) {
                requireRectInsideTexture(rect, layout.textureWidth, layout.textureHeight);
            }
        }
    }

    const auto& steveLeftArm = steveLayout.parts[leftArmIndex];
    const auto& classicLeftArm = classic64x64Layout.parts[leftArmIndex];
    const auto& classicRightArm = classic64x64Layout.parts[rightArmIndex];
    const auto& classicLeftLeg = classic64x64Layout.parts[leftLegIndex];
    const auto& classicRightLeg = classic64x64Layout.parts[rightLegIndex];
    const auto& legacyClassicLeftArm = classic64x32Layout.parts[leftArmIndex];

    require(!rectEquals(steveLeftArm.faceUvs[0], classicLeftArm.faceUvs[0]),
            "64x64 left arm should use its own top UV region");
    require(rectEquals(classicLeftArm.faceUvs[0], classicRightArm.faceUvs[0]),
            "classic left arm should reuse right arm top UV region");
    require(rectEquals(classicLeftArm.faceUvs[4], classicRightArm.faceUvs[5]),
            "classic left arm should swap side UV regions");
    require(rectEquals(classicLeftArm.faceUvs[5], classicRightArm.faceUvs[4]),
            "classic left arm should swap side UV regions");
    require(rectEquals(classicLeftLeg.faceUvs[4], classicRightLeg.faceUvs[5]),
            "classic left leg should swap side UV regions");
    require(rectEquals(classicLeftLeg.faceUvs[5], classicRightLeg.faceUvs[4]),
            "classic left leg should swap side UV regions");
    require(rectEquals(legacyClassicLeftArm.faceUvs[0], classicLeftArm.faceUvs[0]),
            "classic 64x32 and 64x64 mirrored layouts should share limb UV regions");

    std::printf("All HumanoidSkinLayoutCatalog tests passed!\n");
    return 0;
}
