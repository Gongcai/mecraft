#include "VoxelGiClipmap.h"

#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiCommandListPool.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiTypes.h"
#include "../../world/IWorldView.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../resource/BlockTextureColorProvider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string_view>

namespace {

constexpr int kMinClipmapResolution = 16;
constexpr int kMaxClipmapResolution = 128;
constexpr float kMinVoxelSize = 1.0f;
constexpr float kMaxVoxelSize = 4.0f;
const std::array<glm::ivec3, 6> kFaceOffsets = {
    glm::ivec3(0, 1, 0),
    glm::ivec3(0, -1, 0),
    glm::ivec3(-1, 0, 0),
    glm::ivec3(1, 0, 0),
    glm::ivec3(0, 0, 1),
    glm::ivec3(0, 0, -1),
};
const std::array<glm::vec3, 6> kFaceNormals = {
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
};

[[nodiscard]] int normalizedResolution(const int resolution) {
    return std::clamp(resolution, kMinClipmapResolution, kMaxClipmapResolution);
}

[[nodiscard]] float normalizedVoxelSize(const float voxelSize) {
    return std::clamp(voxelSize, kMinVoxelSize, kMaxVoxelSize);
}

[[nodiscard]] int normalizedUpdateInterval(const int updateInterval) {
    return std::clamp(updateInterval, 1, 60);
}

[[nodiscard]] int normalizedOriginSnap(const int originSnap) {
    return std::clamp(originSnap, 1, 64);
}

[[nodiscard]] int mipLevelCount(const int resolution) {
    int levels = 1;
    int size = std::max(1, resolution);
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

[[nodiscard]] uint16_t floatToHalf(const float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    const uint32_t exponent = (bits >> 23u) & 0xFFu;
    const uint32_t mantissa = bits & 0x7FFFFFu;

    if (exponent == 0xFFu) {
        return static_cast<uint16_t>(sign | (mantissa == 0u ? 0x7C00u : 0x7E00u));
    }

    const int32_t halfExponent = static_cast<int32_t>(exponent) - 127 + 15;
    if (halfExponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (halfExponent <= 0) {
        if (halfExponent < -10) {
            return static_cast<uint16_t>(sign);
        }
        const uint32_t normalizedMantissa = mantissa | 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - halfExponent);
        uint32_t halfMantissa = normalizedMantissa >> shift;
        const uint32_t remainder = normalizedMantissa & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (halfMantissa & 1u) != 0u)) {
            ++halfMantissa;
        }
        return static_cast<uint16_t>(sign | halfMantissa);
    }

    uint32_t halfMantissa = mantissa >> 13u;
    const uint32_t remainder = mantissa & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (halfMantissa & 1u) != 0u)) {
        ++halfMantissa;
        if (halfMantissa == 0x400u) {
            halfMantissa = 0u;
            if (halfExponent + 1 >= 31) {
                return static_cast<uint16_t>(sign | 0x7C00u);
            }
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(halfExponent + 1) << 10u));
        }
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(halfExponent) << 10u) | halfMantissa);
}

[[nodiscard]] bool containsToken(const std::string_view text, const std::string_view token) {
    return text.find(token) != std::string_view::npos;
}

[[nodiscard]] bool transmitsGiThroughFace(const BlockDef& def) {
    return def.opacity < 15 || def.renderLayer != BlockRenderLayer::Opaque;
}

[[nodiscard]] bool faceReceivesExteriorLight(const IWorldView& worldView,
                                             const glm::ivec3& blockPos,
                                             const glm::ivec3& faceOffset) {
    const glm::ivec3 neighborPos = blockPos + faceOffset;
    const BlockStateId neighborState = worldView.getBlockState(neighborPos.x, neighborPos.y, neighborPos.z);
    if (neighborState == NULL_BLOCK_STATE) {
        return true;
    }

    const BlockID neighborBlockId = BlockStateRegistry::getBlockId(neighborState);
    return transmitsGiThroughFace(BlockRegistry::getFast(neighborBlockId));
}

[[nodiscard]] glm::vec3 normalizedColor(const glm::vec3& color) {
    const float maxChannel = std::max(color.r, std::max(color.g, color.b));
    return glm::clamp(color / std::max(maxChannel, 1.0f), glm::vec3(0.0f), glm::vec3(2.0f));
}

[[nodiscard]] glm::vec3 averageTextureRefColor(const IBlockTextureColorProvider& textureColors,
                                               const AnimatedTextureRef& textureRef) {
    const int frameCount = textureRef.isAnimated ? std::max(1, static_cast<int>(textureRef.frameCount)) : 1;
    glm::vec3 color(0.0f);
    for (int frame = 0; frame < frameCount; ++frame) {
        color += textureColors.blockTextureAverageColor(textureRef.firstLayer + frame);
    }
    return color / static_cast<float>(frameCount);
}

[[nodiscard]] glm::vec3 materialAlbedo(const BlockDef& def,
                                       const IBlockTextureColorProvider& textureColors) {
    const std::array<AnimatedTextureRef, 6> faces = {
        def.faceTop,
        def.faceBottom,
        def.faceLeft,
        def.faceRight,
        def.faceFront,
        def.faceBack,
    };

    glm::vec3 albedo(0.0f);
    for (const AnimatedTextureRef& face : faces) {
        albedo += averageTextureRefColor(textureColors, face);
    }
    albedo *= (1.0f / static_cast<float>(faces.size()));
    return glm::clamp(albedo, glm::vec3(0.0f), glm::vec3(1.0f));
}

[[nodiscard]] glm::vec3 emissiveColor(const BlockDef& def) {
    const std::string_view path = def.namespacedId.path();
    if (path == "lava") return glm::vec3(1.00f, 0.30f, 0.04f);
    if (containsToken(path, "soul")) return glm::vec3(0.24f, 0.68f, 1.00f);
    if (containsToken(path, "sea_lantern")) return glm::vec3(0.55f, 0.92f, 0.88f);
    if (containsToken(path, "glowstone")) return glm::vec3(1.00f, 0.78f, 0.34f);
    if (containsToken(path, "shroomlight")) return glm::vec3(1.00f, 0.45f, 0.18f);
    if (containsToken(path, "jack_o_lantern")) return glm::vec3(1.00f, 0.48f, 0.08f);
    if (containsToken(path, "torch") || containsToken(path, "lantern") || containsToken(path, "campfire")) {
        return glm::vec3(1.00f, 0.62f, 0.22f);
    }
    if (containsToken(path, "froglight")) {
        if (containsToken(path, "verdant")) return glm::vec3(0.72f, 1.00f, 0.56f);
        if (containsToken(path, "pearlescent")) return glm::vec3(1.00f, 0.62f, 0.78f);
        return glm::vec3(1.00f, 0.76f, 0.36f);
    }
    if (containsToken(path, "redstone")) return glm::vec3(1.00f, 0.05f, 0.02f);
    if (containsToken(path, "amethyst")) return glm::vec3(0.66f, 0.46f, 0.95f);

    switch (def.derivativeMaterialId) {
        case DerivativeMaterialIds::LAVA:
        case DerivativeMaterialIds::FIRE:
            return glm::vec3(1.00f, 0.34f, 0.06f);
        case DerivativeMaterialIds::SOUL_FIRE:
            return glm::vec3(0.28f, 0.72f, 1.00f);
        case DerivativeMaterialIds::SEA_LANTERN_LIKE:
            return glm::vec3(0.55f, 0.92f, 0.88f);
        case DerivativeMaterialIds::AMETHYST:
            return glm::vec3(0.66f, 0.46f, 0.95f);
        case DerivativeMaterialIds::REDSTONE:
            return glm::vec3(1.00f, 0.05f, 0.02f);
        default:
            return glm::vec3(1.00f, 0.76f, 0.38f);
    }
}

} // namespace

VoxelGiClipmap::~VoxelGiClipmap() {
    shutdown();
}

void VoxelGiClipmap::shutdown() {
    if (m_rhiDevice != nullptr) {
        if (m_texture.isValid()) {
            m_rhiDevice->destroyTexture(m_texture);
        }
        if (m_shiftScratchTexture.isValid()) {
            m_rhiDevice->destroyTexture(m_shiftScratchTexture);
        }
    }
    m_rhiDevice = nullptr;
    m_commandListPool = nullptr;
    m_texture = {};
    m_shiftScratchTexture = {};
    m_textureState = RhiResourceState::Undefined;
    m_shiftScratchTextureState = RhiResourceState::Undefined;
    m_voxels.clear();
    m_materialAlbedoCache.clear();
    m_materialAlbedoCacheValid.clear();
    m_valid = false;
    m_resolution = 0;
    m_mipLevels = 1;
    m_lastActiveChunkRevision = 0;
    m_lastBlockContentRevision = 0;
    m_lastSkyRadianceScale = -1.0f;
    m_lastSunRadianceScale = -1.0f;
    m_lastSkyBounceStrength = -1.0f;
    m_lastSunBounceStrength = -1.0f;
    m_lastSunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    m_lastUpdateFrame = 0;
    m_stats = {};
}

void VoxelGiClipmap::update(const FrameContext& ctx,
                            const VoxelGiSettings& settings,
                            const IBlockTextureColorProvider& textureColors,
                            RhiDevice& rhiDevice,
                            RhiCommandListPool& commandListPool) {
    if ((m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) ||
        (m_commandListPool != nullptr && m_commandListPool != &commandListPool)) {
        shutdown();
    }
    m_rhiDevice = &rhiDevice;
    m_commandListPool = &commandListPool;
    if (!settings.enabled || ctx.worldView == nullptr) {
        m_valid = false;
        m_stats = {};
        return;
    }

    const int resolution = normalizedResolution(settings.resolution);
    const float voxelSize = normalizedVoxelSize(settings.voxelSize);
    const int mipLevels = mipLevelCount(resolution);
    const int updateInterval = normalizedUpdateInterval(settings.updateInterval);
    const int originSnap = normalizedOriginSnap(settings.originSnap);
    const glm::ivec3 originBlock = computeOrigin(ctx.camera.position, resolution, voxelSize, originSnap);
    const uint64_t activeRevision = ctx.worldView->getActiveChunkRevision();
    const uint64_t blockRevision = ctx.worldView->getBlockContentRevision();
    LightingSampleParams lighting;
    lighting.sunDirection = glm::normalize(ctx.skyColors.sunDirection);
    lighting.sunTint = normalizedColor(ctx.skyColors.sunLightColor);
    lighting.skyTint = normalizedColor(ctx.skyColors.skyAmbientColor);
    lighting.skyRadianceScale = std::clamp(ctx.skyColors.dayFactor * ctx.skyIntensity +
                                           ctx.skyColors.moonVisibility * 0.035f,
                                           0.0f,
                                           1.5f);
    lighting.sunRadianceScale = std::clamp(ctx.skyColors.dayFactor * ctx.skyColors.sunVisibility * ctx.skyIntensity,
                                           0.0f,
                                           1.5f);
    lighting.skyBounceStrength = std::max(settings.skyBounceStrength, 0.0f);
    lighting.sunBounceStrength = std::max(settings.sunBounceStrength, 0.0f);

    const bool parametersChanged = resolution != m_resolution ||
                                   mipLevels != m_mipLevels ||
                                   std::abs(voxelSize - m_voxelSize) > 0.0001f;
    const bool originChanged = originBlock != m_originBlock;
    const bool worldChanged = activeRevision != m_lastActiveChunkRevision ||
                              blockRevision != m_lastBlockContentRevision;
    const bool skyRadianceChanged = std::abs(lighting.skyRadianceScale - m_lastSkyRadianceScale) > 0.025f;
    const bool sunRadianceChanged = std::abs(lighting.sunRadianceScale - m_lastSunRadianceScale) > 0.025f;
    const bool bounceStrengthChanged = std::abs(lighting.skyBounceStrength - m_lastSkyBounceStrength) > 0.001f ||
                                       std::abs(lighting.sunBounceStrength - m_lastSunBounceStrength) > 0.001f;
    const bool sunDirectionChanged = glm::dot(glm::normalize(lighting.sunDirection), glm::normalize(m_lastSunDirection)) < 0.9985f;
    const bool intervalReady = ctx.frameIndex >= m_lastUpdateFrame + static_cast<uint64_t>(updateInterval);
    if (m_valid && !parametersChanged && !originChanged &&
        !((worldChanged || skyRadianceChanged || sunRadianceChanged || sunDirectionChanged || bounceStrengthChanged) && intervalReady)) {
        updateStatsBase(VoxelGiClipmapUpdateMode::Idle, m_originBlock);
        return;
    }

    if (!allocateTexture(resolution, rhiDevice)) {
        m_valid = false;
        m_stats = {};
        return;
    }
    glm::ivec3 deltaVoxels(0);
    const bool lightingChanged = skyRadianceChanged || sunRadianceChanged || sunDirectionChanged || bounceStrengthChanged;
    const bool canReuseVolume = m_valid && !parametersChanged && originChanged && !worldChanged && !lightingChanged &&
                                computeVoxelDelta(originBlock, deltaVoxels);
    m_resolution = resolution;
    m_mipLevels = mipLevels;
    m_voxelSize = voxelSize;

    const bool shiftedVolume = canReuseVolume &&
                               shiftCachedVolume(*ctx.worldView, textureColors, lighting, originBlock, deltaVoxels);
    if (!shiftedVolume) {
        rebuildVolume(*ctx.worldView, textureColors, lighting, originBlock);
    }

    m_originBlock = originBlock;
    m_origin = glm::vec3(originBlock);
    RhiCommandList& commandList = beginCommandList("VoxelGI.Upload");
    std::vector<RhiBufferHandle> stagingBuffers;
    stagingBuffers.reserve(6u);
    bool uploadSucceeded = true;
    if (shiftedVolume) {
        const uint64_t overlapVoxels = overlapVoxelCount(deltaVoxels);
        const uint64_t totalVoxels = static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution);
        updateStatsBase(VoxelGiClipmapUpdateMode::Shifted, originBlock);
        m_stats.deltaVoxels = deltaVoxels;
        m_stats.reusedVoxels = overlapVoxels;
        m_stats.sampledVoxels = totalVoxels - overlapVoxels;
        m_stats.uploadedVoxels = m_stats.sampledVoxels;
        m_stats.copiedVoxels = overlapVoxels;
        m_stats.uploadedBoxes = uploadShiftedVolume(deltaVoxels, commandList, rhiDevice,
                                                    stagingBuffers);
        uploadSucceeded = m_stats.uploadedBoxes >= 0;
    } else {
        uploadSucceeded = uploadFullVolume(commandList, rhiDevice, stagingBuffers);
        const uint64_t totalVoxels = static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution);
        updateStatsBase(VoxelGiClipmapUpdateMode::Full, originBlock);
        m_stats.sampledVoxels = totalVoxels;
        m_stats.uploadedVoxels = totalVoxels;
        m_stats.uploadedBoxes = 1;
    }
    submitCommandList(commandList, "VoxelGI.Upload");
    for (const RhiBufferHandle staging : stagingBuffers) {
        rhiDevice.destroyBuffer(staging);
    }
    if (!uploadSucceeded) {
        m_valid = false;
        m_stats = {};
        return;
    }
    m_lastActiveChunkRevision = activeRevision;
    m_lastBlockContentRevision = blockRevision;
    m_lastSkyRadianceScale = lighting.skyRadianceScale;
    m_lastSunRadianceScale = lighting.sunRadianceScale;
    m_lastSkyBounceStrength = lighting.skyBounceStrength;
    m_lastSunBounceStrength = lighting.sunBounceStrength;
    m_lastSunDirection = lighting.sunDirection;
    m_lastUpdateFrame = ctx.frameIndex;
    m_valid = true;
    m_stats.valid = true;
    m_stats.skyRadianceScale = lighting.skyRadianceScale;
    m_stats.sunRadianceScale = lighting.sunRadianceScale;
}

RhiCommandList& VoxelGiClipmap::beginCommandList(const char* const debugName) const {
    if (m_commandListPool == nullptr) {
        std::abort();
    }
    RhiCommandList* const commandList =
        m_commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({debugName, RhiCommandListType::Graphics})) {
        std::abort();
    }
    return *commandList;
}

void VoxelGiClipmap::submitCommandList(RhiCommandList& commandList,
                                       const char* const debugName) const {
    if (m_rhiDevice == nullptr || !commandList.end()) {
        std::abort();
    }
    RhiCommandList* commandLists[] = {&commandList};
    const RhiSubmitInfo submitInfo{debugName, commandLists, 1u};
    if (!m_rhiDevice->submit(submitInfo)) {
        std::abort();
    }
}

bool VoxelGiClipmap::allocateTexture(const int resolution, RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        shutdown();
    }
    m_rhiDevice = &rhiDevice;
    if (m_texture.isValid() && m_shiftScratchTexture.isValid() && m_resolution == resolution) {
        return true;
    }

    if (m_texture.isValid()) {
        rhiDevice.destroyTexture(m_texture);
        m_texture = {};
        m_textureState = RhiResourceState::Undefined;
    }
    if (m_shiftScratchTexture.isValid()) {
        rhiDevice.destroyTexture(m_shiftScratchTexture);
        m_shiftScratchTexture = {};
        m_shiftScratchTextureState = RhiResourceState::Undefined;
    }

    const uint32_t usage = rhiFlag(RhiTextureUsage::Sampled) |
                           rhiFlag(RhiTextureUsage::TransferSrc) |
                           rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VoxelGI.ClipmapRadiance";
    textureDesc.dimension = RhiTextureDimension::Texture3D;
    textureDesc.format = RhiTextureFormat::Rgba16Float;
    textureDesc.width = static_cast<uint32_t>(resolution);
    textureDesc.height = static_cast<uint32_t>(resolution);
    textureDesc.depthOrLayers = static_cast<uint32_t>(resolution);
    textureDesc.mipLevels = static_cast<uint32_t>(mipLevelCount(resolution));
    textureDesc.usage = usage;
    m_texture = rhiDevice.createTexture(textureDesc, nullptr);
    if (!m_texture.isValid()) {
        return false;
    }
    m_textureState = RhiResourceState::Undefined;

    textureDesc.debugName = "VoxelGI.ShiftScratch";
    textureDesc.mipLevels = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::TransferSrc) | rhiFlag(RhiTextureUsage::TransferDst);
    m_shiftScratchTexture = rhiDevice.createTexture(textureDesc, nullptr);
    if (!m_shiftScratchTexture.isValid()) {
        rhiDevice.destroyTexture(m_texture);
        m_texture = {};
        m_textureState = RhiResourceState::Undefined;
        return false;
    }
    m_shiftScratchTextureState = RhiResourceState::Undefined;
    return true;
}

bool VoxelGiClipmap::uploadFullVolume(RhiCommandList& commandList,
                                      RhiDevice& rhiDevice,
                                      std::vector<RhiBufferHandle>& stagingBuffers) {
    transitionTexture(commandList, m_texture, m_textureState, RhiResourceState::TransferDst);
    if (!uploadSubVolume(0, 0, 0, m_resolution, m_resolution, m_resolution,
                         commandList, rhiDevice, stagingBuffers)) {
        return false;
    }
    commandList.generateMipmaps(m_texture);
    transitionTexture(commandList, m_texture, m_textureState, RhiResourceState::ShaderRead);
    return true;
}

int VoxelGiClipmap::uploadShiftedVolume(const glm::ivec3& deltaVoxels,
                                        RhiCommandList& commandList,
                                        RhiDevice& rhiDevice,
                                        std::vector<RhiBufferHandle>& stagingBuffers) {
    copyOverlapThroughScratch(deltaVoxels, commandList);

    const auto axisRange = [this](const int delta, int& start, int& end) {
        if (delta > 0) {
            start = 0;
            end = m_resolution - delta;
        } else if (delta < 0) {
            start = -delta;
            end = m_resolution;
        } else {
            start = 0;
            end = m_resolution;
        }
    };

    int x0 = 0;
    int x1 = m_resolution;
    int y0 = 0;
    int y1 = m_resolution;
    int z0 = 0;
    int z1 = m_resolution;
    axisRange(deltaVoxels.x, x0, x1);
    axisRange(deltaVoxels.y, y0, y1);
    axisRange(deltaVoxels.z, z0, z1);

    int uploadedBoxes = 0;
    const auto upload = [&](const int x, const int y, const int z,
                            const int width, const int height, const int depth) {
        if (!uploadSubVolume(x, y, z, width, height, depth,
                             commandList, rhiDevice, stagingBuffers)) {
            return false;
        }
        ++uploadedBoxes;
        return true;
    };
    if (x0 > 0 && !upload(0, 0, 0, x0, m_resolution, m_resolution)) return -1;
    if (x1 < m_resolution && !upload(x1, 0, 0, m_resolution - x1, m_resolution, m_resolution)) return -1;
    if (y0 > 0 && x1 > x0 && !upload(x0, 0, 0, x1 - x0, y0, m_resolution)) return -1;
    if (y1 < m_resolution && x1 > x0 &&
        !upload(x0, y1, 0, x1 - x0, m_resolution - y1, m_resolution)) return -1;
    if (z0 > 0 && x1 > x0 && y1 > y0 && !upload(x0, y0, 0, x1 - x0, y1 - y0, z0)) return -1;
    if (z1 < m_resolution && x1 > x0 && y1 > y0 &&
        !upload(x0, y0, z1, x1 - x0, y1 - y0, m_resolution - z1)) return -1;

    commandList.generateMipmaps(m_texture);
    transitionTexture(commandList, m_texture, m_textureState, RhiResourceState::ShaderRead);
    return uploadedBoxes;
}

bool VoxelGiClipmap::uploadSubVolume(const int x,
                                     const int y,
                                     const int z,
                                     const int width,
                                     const int height,
                                     const int depth,
                                     RhiCommandList& commandList,
                                     RhiDevice& rhiDevice,
                                     std::vector<RhiBufferHandle>& stagingBuffers) {
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }

    const size_t voxelCount = static_cast<size_t>(width) * height * depth;
    std::vector<uint16_t> packedVoxels(voxelCount * 4u);
    size_t packedIndex = 0u;
    for (int localZ = 0; localZ < depth; ++localZ) {
        for (int localY = 0; localY < height; ++localY) {
            for (int localX = 0; localX < width; ++localX) {
                const ClipmapVoxel& voxel = m_voxels[voxelIndex(x + localX, y + localY, z + localZ)];
                packedVoxels[packedIndex++] = floatToHalf(voxel.r);
                packedVoxels[packedIndex++] = floatToHalf(voxel.g);
                packedVoxels[packedIndex++] = floatToHalf(voxel.b);
                packedVoxels[packedIndex++] = floatToHalf(voxel.occupancy);
            }
        }
    }

    RhiBufferDesc stagingDesc;
    stagingDesc.debugName = "VoxelGI.UploadStaging";
    stagingDesc.size = packedVoxels.size() * sizeof(uint16_t);
    stagingDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                        rhiFlag(RhiBufferUsage::TransferDst);
    stagingDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    stagingDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle staging = rhiDevice.createBuffer(
        stagingDesc, packedVoxels.data(), static_cast<size_t>(stagingDesc.size));
    if (!staging.isValid()) {
        return false;
    }
    stagingBuffers.push_back(staging);

    RhiBufferTextureCopy copy;
    copy.srcBuffer = staging;
    copy.dstTexture = m_texture;
    copy.mipLevel = 0u;
    copy.arrayLayer = 0u;
    copy.dstX = static_cast<uint32_t>(x);
    copy.dstY = static_cast<uint32_t>(y);
    copy.dstZ = static_cast<uint32_t>(z);
    copy.width = static_cast<uint32_t>(width);
    copy.height = static_cast<uint32_t>(height);
    copy.depth = static_cast<uint32_t>(depth);
    commandList.copyBufferToTexture(copy);
    return true;
}

void VoxelGiClipmap::copyOverlapThroughScratch(const glm::ivec3& deltaVoxels,
                                               RhiCommandList& commandList) {
    const auto axisRange = [this](const int delta, int& srcStart, int& dstStart, int& size) {
        if (delta > 0) {
            srcStart = delta;
            dstStart = 0;
            size = m_resolution - delta;
        } else if (delta < 0) {
            srcStart = 0;
            dstStart = -delta;
            size = m_resolution + delta;
        } else {
            srcStart = 0;
            dstStart = 0;
            size = m_resolution;
        }
    };

    int srcX = 0;
    int srcY = 0;
    int srcZ = 0;
    int dstX = 0;
    int dstY = 0;
    int dstZ = 0;
    int sizeX = m_resolution;
    int sizeY = m_resolution;
    int sizeZ = m_resolution;
    axisRange(deltaVoxels.x, srcX, dstX, sizeX);
    axisRange(deltaVoxels.y, srcY, dstY, sizeY);
    axisRange(deltaVoxels.z, srcZ, dstZ, sizeZ);

    transitionTexture(commandList, m_texture, m_textureState, RhiResourceState::TransferSrc);
    transitionTexture(commandList, m_shiftScratchTexture, m_shiftScratchTextureState,
                      RhiResourceState::TransferDst);

    RhiTextureCopy copy;
    copy.src = m_texture;
    copy.dst = m_shiftScratchTexture;
    copy.srcSubresource = {0u, 0u, 1u};
    copy.dstSubresource = {0u, 0u, 1u};
    copy.srcOffset = {static_cast<uint32_t>(srcX), static_cast<uint32_t>(srcY), static_cast<uint32_t>(srcZ)};
    copy.dstOffset = {static_cast<uint32_t>(dstX), static_cast<uint32_t>(dstY), static_cast<uint32_t>(dstZ)};
    copy.extent = {static_cast<uint32_t>(sizeX), static_cast<uint32_t>(sizeY), static_cast<uint32_t>(sizeZ)};
    commandList.copyTexture(copy);

    transitionTexture(commandList, m_shiftScratchTexture, m_shiftScratchTextureState,
                      RhiResourceState::TransferSrc);
    transitionTexture(commandList, m_texture, m_textureState, RhiResourceState::TransferDst);
    copy.src = m_shiftScratchTexture;
    copy.dst = m_texture;
    copy.srcOffset = copy.dstOffset;
    commandList.copyTexture(copy);
}

void VoxelGiClipmap::transitionTexture(RhiCommandList& commandList,
                                       const RhiTextureHandle texture,
                                       RhiResourceState& currentState,
                                       const RhiResourceState newState) {
    if (currentState == newState) {
        return;
    }
    commandList.textureBarrier({texture, currentState, newState});
    currentState = newState;
}

void VoxelGiClipmap::rebuildVolume(const IWorldView& worldView,
                                   const IBlockTextureColorProvider& textureColors,
                                   const LightingSampleParams& lighting,
                                   const glm::ivec3& originBlock) {
    const size_t voxelCount = static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution);
    m_voxels.assign(voxelCount, ClipmapVoxel{});
    for (int z = 0; z < m_resolution; ++z) {
        for (int y = 0; y < m_resolution; ++y) {
            for (int x = 0; x < m_resolution; ++x) {
                m_voxels[voxelIndex(x, y, z)] =
                    sampleWorldVoxel(worldView, textureColors, lighting, voxelWorldPosition(originBlock, x, y, z));
            }
        }
    }
}

bool VoxelGiClipmap::shiftCachedVolume(const IWorldView& worldView,
                                       const IBlockTextureColorProvider& textureColors,
                                       const LightingSampleParams& lighting,
                                       const glm::ivec3& originBlock,
                                       const glm::ivec3& deltaVoxels) {
    const size_t voxelCount = static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution);
    if (m_voxels.size() != voxelCount) {
        return false;
    }

    if (std::abs(deltaVoxels.x) >= m_resolution ||
        std::abs(deltaVoxels.y) >= m_resolution ||
        std::abs(deltaVoxels.z) >= m_resolution) {
        return false;
    }

    std::vector<ClipmapVoxel> previous;
    previous.swap(m_voxels);
    m_voxels.assign(voxelCount, ClipmapVoxel{});

    for (int z = 0; z < m_resolution; ++z) {
        const int oldZ = z + deltaVoxels.z;
        for (int y = 0; y < m_resolution; ++y) {
            const int oldY = y + deltaVoxels.y;
            for (int x = 0; x < m_resolution; ++x) {
                const int oldX = x + deltaVoxels.x;
                const size_t index = voxelIndex(x, y, z);
                if (oldX >= 0 && oldX < m_resolution &&
                    oldY >= 0 && oldY < m_resolution &&
                    oldZ >= 0 && oldZ < m_resolution) {
                    m_voxels[index] = previous[voxelIndex(oldX, oldY, oldZ)];
                } else {
                    m_voxels[index] =
                        sampleWorldVoxel(worldView, textureColors, lighting, voxelWorldPosition(originBlock, x, y, z));
                }
            }
        }
    }

    return true;
}

bool VoxelGiClipmap::computeVoxelDelta(const glm::ivec3& originBlock, glm::ivec3& outDeltaVoxels) const {
    const glm::vec3 originDelta = glm::vec3(originBlock - m_originBlock) / m_voxelSize;
    outDeltaVoxels = glm::ivec3(
        static_cast<int>(std::round(originDelta.x)),
        static_cast<int>(std::round(originDelta.y)),
        static_cast<int>(std::round(originDelta.z)));
    const glm::vec3 snappedDelta = glm::vec3(outDeltaVoxels) * m_voxelSize;
    return glm::length(snappedDelta - glm::vec3(originBlock - m_originBlock)) <= 0.01f;
}

uint64_t VoxelGiClipmap::overlapVoxelCount(const glm::ivec3& deltaVoxels) const {
    const int extentX = std::max(0, m_resolution - std::abs(deltaVoxels.x));
    const int extentY = std::max(0, m_resolution - std::abs(deltaVoxels.y));
    const int extentZ = std::max(0, m_resolution - std::abs(deltaVoxels.z));
    return static_cast<uint64_t>(extentX) * static_cast<uint64_t>(extentY) * static_cast<uint64_t>(extentZ);
}

void VoxelGiClipmap::updateStatsBase(const VoxelGiClipmapUpdateMode mode, const glm::ivec3& originBlock) {
    m_stats = {};
    m_stats.mode = mode;
    m_stats.valid = m_valid;
    m_stats.resolution = m_resolution;
    m_stats.mipLevels = m_mipLevels;
    m_stats.originBlock = originBlock;
    m_stats.skyRadianceScale = std::max(m_lastSkyRadianceScale, 0.0f);
    m_stats.sunRadianceScale = std::max(m_lastSunRadianceScale, 0.0f);
}

size_t VoxelGiClipmap::voxelIndex(const int x, const int y, const int z) const {
    return (static_cast<size_t>(z) * static_cast<size_t>(m_resolution) +
            static_cast<size_t>(y)) * static_cast<size_t>(m_resolution) +
           static_cast<size_t>(x);
}

glm::ivec3 VoxelGiClipmap::voxelWorldPosition(const glm::ivec3& originBlock,
                                              const int x,
                                              const int y,
                                              const int z) const {
    return originBlock + glm::ivec3(
        static_cast<int>(std::floor((static_cast<float>(x) + 0.5f) * m_voxelSize)),
        static_cast<int>(std::floor((static_cast<float>(y) + 0.5f) * m_voxelSize)),
        static_cast<int>(std::floor((static_cast<float>(z) + 0.5f) * m_voxelSize)));
}

glm::ivec3 VoxelGiClipmap::computeOrigin(const glm::vec3& cameraPosition,
                                         const int resolution,
                                         const float voxelSize,
                                         const int originSnap) const {
    const float span = static_cast<float>(resolution) * voxelSize;
    const glm::vec3 minCorner = cameraPosition - glm::vec3(span * 0.5f);
    const int snap = std::max(1, originSnap);
    return glm::ivec3(
        static_cast<int>(std::floor(minCorner.x / static_cast<float>(snap))) * snap,
        static_cast<int>(std::floor(minCorner.y / static_cast<float>(snap))) * snap,
        static_cast<int>(std::floor(minCorner.z / static_cast<float>(snap))) * snap);
}

const glm::vec3& VoxelGiClipmap::cachedMaterialAlbedo(const BlockID blockId,
                                                      const BlockDef& def,
                                                      const IBlockTextureColorProvider& textureColors) {
    const size_t cacheIndex = static_cast<size_t>(blockId);
    if (cacheIndex >= m_materialAlbedoCache.size()) {
        const size_t newSize = cacheIndex + 1u;
        m_materialAlbedoCache.resize(newSize, glm::vec3(0.0f));
        m_materialAlbedoCacheValid.resize(newSize, 0u);
    }

    if (m_materialAlbedoCacheValid[cacheIndex] == 0u) {
        m_materialAlbedoCache[cacheIndex] = materialAlbedo(def, textureColors);
        m_materialAlbedoCacheValid[cacheIndex] = 1u;
    }

    return m_materialAlbedoCache[cacheIndex];
}

VoxelGiClipmap::ClipmapVoxel VoxelGiClipmap::sampleWorldVoxel(const IWorldView& worldView,
                                                              const IBlockTextureColorProvider& textureColors,
                                                              const LightingSampleParams& lighting,
                                                              const glm::ivec3& blockPos) {
    ClipmapVoxel voxel;
    const BlockStateId stateId = worldView.getBlockState(blockPos.x, blockPos.y, blockPos.z);
    if (stateId == NULL_BLOCK_STATE) {
        return voxel;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.renderLayer == BlockRenderLayer::Transparent && !def.isLightSource) {
        return voxel;
    }

    const glm::vec3 albedo = cachedMaterialAlbedo(blockId, def, textureColors);
    const uint8_t packedLight = worldView.getPackedLight(blockPos.x, blockPos.y, blockPos.z);
    const float blockLight = static_cast<float>(packedLight & 0x0F) * (1.0f / 15.0f);
    const float lightLevel = static_cast<float>(def.lightLevel) * (1.0f / 15.0f);

    float openFaceWeight = 0.0f;
    float skyFaceWeight = 0.0f;
    float sunFaceWeight = 0.0f;
    float blockFaceWeight = blockLight * 0.20f;
    for (size_t face = 0; face < kFaceOffsets.size(); ++face) {
        if (!faceReceivesExteriorLight(worldView, blockPos, kFaceOffsets[face])) {
            continue;
        }

        const glm::ivec3 samplePos = blockPos + kFaceOffsets[face];
        const uint8_t facePackedLight = worldView.getPackedLight(samplePos.x, samplePos.y, samplePos.z);
        const float faceBlockLight = static_cast<float>(facePackedLight & 0x0F) * (1.0f / 15.0f);
        const float faceSkyLight = static_cast<float>((facePackedLight >> 4) & 0x0F) * (1.0f / 15.0f);
        const float skyHemisphere = 0.30f + 0.70f * std::max(kFaceNormals[face].y, 0.0f);
        const float sunFacing = std::max(glm::dot(kFaceNormals[face], lighting.sunDirection), 0.0f);

        openFaceWeight += 1.0f;
        skyFaceWeight += faceSkyLight * skyHemisphere;
        sunFaceWeight += faceSkyLight * sunFacing;
        blockFaceWeight += faceBlockLight * 0.85f;
    }

    const float exposedRatio = openFaceWeight * (1.0f / 6.0f);
    const float skyBounce = (0.018f * exposedRatio + skyFaceWeight * (1.0f / 6.0f) * 0.34f) *
                            lighting.skyRadianceScale * lighting.skyBounceStrength;
    const float sunBounce = sunFaceWeight * (1.0f / 6.0f) * 0.92f *
                            lighting.sunRadianceScale * lighting.sunBounceStrength;
    const glm::vec3 skyRadiance = albedo * lighting.skyTint * skyBounce;
    const glm::vec3 sunRadiance = albedo * lighting.sunTint * sunBounce;
    const glm::vec3 blockRadiance = albedo * (blockFaceWeight * (1.0f / 6.0f) * 0.68f);
    const glm::vec3 emission = emissiveColor(def) * (std::pow(lightLevel, 1.25f) * 2.35f);
    const glm::vec3 radiance = skyRadiance + sunRadiance + blockRadiance + emission;

    voxel.r = radiance.r;
    voxel.g = radiance.g;
    voxel.b = radiance.b;
    voxel.occupancy = def.renderLayer == BlockRenderLayer::Opaque ? 1.0f : 0.45f;
    return voxel;
}
