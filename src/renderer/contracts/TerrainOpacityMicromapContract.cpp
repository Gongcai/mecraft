#include "renderer/contracts/TerrainOpacityMicromapContract.h"

#include "renderer/contracts/ContentHashContract.h"
#include "renderer/contracts/TerrainMaterialSamplingContract.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace renderer::contracts {
namespace {

constexpr uint8_t kOpaqueState = 1u;
constexpr uint8_t kTransparentState = 0u;
constexpr uint8_t kUnknownOpaqueState = 3u;
constexpr uint8_t kFourStatePackedUnknown = 0xffu;

[[nodiscard]] bool checkedExpectedByteSize(const uint32_t tileSize, const uint32_t layerCount, size_t& bytes) {
    if (tileSize == 0u || layerCount == 0u) {
        return false;
    }
    const uint64_t texelCount = static_cast<uint64_t>(tileSize) * tileSize * layerCount;
    if (texelCount > std::numeric_limits<size_t>::max() / 4u) {
        return false;
    }
    bytes = static_cast<size_t>(texelCount) * 4u;
    return true;
}

[[nodiscard]] uint32_t wrapTexel(const int64_t coordinate, const uint32_t extent) {
    const int64_t modulus = coordinate % static_cast<int64_t>(extent);
    return static_cast<uint32_t>(modulus < 0 ? modulus + static_cast<int64_t>(extent) : modulus);
}

[[nodiscard]] bool validTriangleInput(const TerrainOpacityMicromapTriangleInput& triangle,
                                      const TerrainOpacityMicromapSource& source) {
    if (triangle.animationFrameCount == 0u || triangle.firstTextureLayer >= source.layerCount ||
        triangle.animationFrameCount > source.layerCount - triangle.firstTextureLayer) {
        return false;
    }
    return std::all_of(triangle.uv.begin(), triangle.uv.end(), [](const float value) { return std::isfinite(value); });
}

[[nodiscard]] float triangleUv(const TerrainOpacityMicromapTriangleInput& triangle, const uint32_t component,
                               const float b, const float c) {
    const float a = 1.0f - b - c;
    return triangle.uv[component] * a + triangle.uv[2u + component] * b + triangle.uv[4u + component] * c;
}

[[nodiscard]] uint32_t interleaveBits(uint32_t value) {
    value = (value | (value << 8u)) & 0x00ff00ffu;
    value = (value | (value << 4u)) & 0x0f0f0f0fu;
    value = (value | (value << 2u)) & 0x33333333u;
    return (value | (value << 1u)) & 0x55555555u;
}

[[nodiscard]] uint32_t terrainOpacityMicromapIndex(float b, float c, const uint32_t subdivisionLevel) {
    b = std::clamp(b, 0.0f, 1.0f);
    c = std::clamp(c, 0.0f, 1.0f);
    const uint32_t dimension = 1u << subdivisionLevel;
    const float scaledB = b * static_cast<float>(dimension);
    const float scaledC = c * static_cast<float>(dimension);
    uint32_t ib = static_cast<uint32_t>(scaledB);
    uint32_t ic = static_cast<uint32_t>(scaledC);
    const float fractionalB = scaledB - static_cast<float>(ib);
    const float fractionalC = scaledC - static_cast<float>(ic);
    ib = std::min(ib, dimension - 1u);
    ic = std::min(ic, dimension - 1u);
    const uint32_t sum = ib + ic;
    if (sum >= dimension) {
        ib -= sum - dimension + 1u;
    }
    uint32_t iw = ~(ib + ic);
    if (fractionalB + fractionalC >= 1.0f && sum < dimension - 1u) {
        --iw;
    }
    uint32_t bitZero = ~(ib ^ iw) & (dimension - 1u);
    const uint32_t transition = (ib ^ ic) & bitZero;
    uint32_t fold = transition;
    fold ^= fold >> 1u;
    fold ^= fold >> 2u;
    fold ^= fold >> 4u;
    fold ^= fold >> 8u;
    const uint32_t bitOne = ((fold ^ ib) & ~bitZero) | transition;
    return interleaveBits(bitZero) | (interleaveBits(bitOne) << 1u);
}

void writeState(std::vector<uint8_t>& data, const size_t triangleOffset, const uint32_t microIndex,
                const uint8_t state) {
    const size_t bitOffset = triangleOffset * 8u + static_cast<size_t>(microIndex) * 2u;
    const size_t byteIndex = bitOffset >> 3u;
    const uint32_t shift = static_cast<uint32_t>(bitOffset & 7u);
    const uint8_t mask = static_cast<uint8_t>(0x3u << shift);
    data[byteIndex] = static_cast<uint8_t>((data[byteIndex] & ~mask) | ((state & 0x3u) << shift));
}

[[nodiscard]] uint8_t classifyRegion(const TerrainOpacityMicromapSource& source,
                                     const TerrainOpacityMicromapTriangleInput& triangle, const float minU,
                                     const float maxU, const float minV, const float maxV) {
    if (!std::isfinite(minU) || !std::isfinite(maxU) || !std::isfinite(minV) || !std::isfinite(maxV)) {
        return kUnknownOpaqueState;
    }
    const double scaledMinU = static_cast<double>(minU) * source.tileSize;
    const double scaledMaxU = static_cast<double>(maxU) * source.tileSize;
    const double scaledMinV = static_cast<double>(minV) * source.tileSize;
    const double scaledMaxV = static_cast<double>(maxV) * source.tileSize;
    constexpr double kBoundaryEpsilon = 1.0e-3;
    if (scaledMinU < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        scaledMaxU > static_cast<double>(std::numeric_limits<int64_t>::max()) ||
        scaledMinV < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        scaledMaxV > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return kUnknownOpaqueState;
    }
    int64_t x0 = static_cast<int64_t>(std::floor(scaledMinU + kBoundaryEpsilon));
    int64_t x1 = static_cast<int64_t>(std::ceil(scaledMaxU - kBoundaryEpsilon));
    int64_t y0 = static_cast<int64_t>(std::floor(scaledMinV + kBoundaryEpsilon));
    int64_t y1 = static_cast<int64_t>(std::ceil(scaledMaxV - kBoundaryEpsilon));
    x1 = std::max(x1, x0 + 1);
    y1 = std::max(y1, y0 + 1);
    if (x1 - x0 > static_cast<int64_t>(source.tileSize)) {
        x1 = x0 + static_cast<int64_t>(source.tileSize);
    }
    if (y1 - y0 > static_cast<int64_t>(source.tileSize)) {
        y1 = y0 + static_cast<int64_t>(source.tileSize);
    }

    bool anyPass = false;
    bool anyFail = false;
    const uint32_t frameCount = triangle.animated ? triangle.animationFrameCount : 1u;
    const size_t layerByteSize = static_cast<size_t>(source.tileSize) * source.tileSize * 4u;
    for (uint32_t frame = 0u; frame < frameCount; ++frame) {
        const uint8_t* layer = source.rgba8 + layerByteSize * (triangle.firstTextureLayer + frame);
        for (int64_t y = y0; y < y1; ++y) {
            for (int64_t x = x0; x < x1; ++x) {
                const size_t alphaOffset = (static_cast<size_t>(wrapTexel(y, source.tileSize)) * source.tileSize +
                                            wrapTexel(x, source.tileSize)) *
                                               4u +
                                           3u;
                const bool passes = static_cast<float>(layer[alphaOffset]) * (1.0f / 255.0f) >= kTerrainAlphaCutoff;
                anyPass |= passes;
                anyFail |= !passes;
                if (anyPass && anyFail) {
                    return kUnknownOpaqueState;
                }
            }
        }
    }
    return anyPass ? kOpaqueState : kTransparentState;
}

void classifyMicroTriangle(std::vector<uint8_t>& data, const size_t triangleByteOffset,
                           const TerrainOpacityMicromapSource& source,
                           const TerrainOpacityMicromapTriangleInput& triangle, const uint32_t microIndex,
                           const float b0, const float c0, const float b1, const float c1, const float b2,
                           const float c2, TerrainOpacityMicromapCounters& counters) {
    const float u0 = triangleUv(triangle, 0u, b0, c0);
    const float v0 = triangleUv(triangle, 1u, b0, c0);
    const float u1 = triangleUv(triangle, 0u, b1, c1);
    const float v1 = triangleUv(triangle, 1u, b1, c1);
    const float u2 = triangleUv(triangle, 0u, b2, c2);
    const float v2 = triangleUv(triangle, 1u, b2, c2);
    const uint8_t state = classifyRegion(source, triangle, std::min({u0, u1, u2}), std::max({u0, u1, u2}),
                                         std::min({v0, v1, v2}), std::max({v0, v1, v2}));
    writeState(data, triangleByteOffset, microIndex, state);
    if (state == kOpaqueState) {
        ++counters.opaque;
    } else if (state == kTransparentState) {
        ++counters.transparent;
    } else {
        ++counters.unknown;
    }
}

} // namespace

bool validTerrainOpacityMicromapSource(const TerrainOpacityMicromapSource& source) {
    size_t expectedBytes = 0u;
    return source.rgba8 != nullptr && source.alphaTextureHash != 0u &&
           checkedExpectedByteSize(source.tileSize, source.layerCount, expectedBytes) &&
           source.sizeBytes == expectedBytes;
}

bool validTerrainOpacityMicromapProfile(const TerrainOpacityMicromapProfile& profile) {
    return profile.subdivisionLevel <= 10u && std::isfinite(profile.alphaCutoff) &&
           profile.alphaCutoff == kTerrainAlphaCutoff;
}

uint64_t terrainOpacityMicromapProfileHash(const TerrainOpacityMicromapProfile& profile) {
    if (!validTerrainOpacityMicromapProfile(profile)) {
        return 0u;
    }
    StableContentHashBuilder hash;
    hash.addString("terrain-omm-4-state-v1");
    hash.addUint64(profile.subdivisionLevel);
    hash.addDouble(profile.alphaCutoff);
    return hash.value();
}

std::optional<TerrainOpacityMicromapCpuData>
buildTerrainOpacityMicromapCpuData(const TerrainOpacityMicromapSource& source,
                                   const TerrainOpacityMicromapProfile& profile,
                                   const std::vector<TerrainOpacityMicromapTriangleInput>& triangles) {
    const uint64_t profileHash = terrainOpacityMicromapProfileHash(profile);
    if (!validTerrainOpacityMicromapSource(source) || profileHash == 0u || triangles.empty()) {
        return std::nullopt;
    }
    for (const TerrainOpacityMicromapTriangleInput& triangle : triangles) {
        if (!validTriangleInput(triangle, source)) {
            return std::nullopt;
        }
    }
    const uint64_t microTriangleCount = 1ull << (profile.subdivisionLevel * 2u);
    const uint64_t bytesPerTriangle = (microTriangleCount * 2u + 7u) / 8u;
    if (triangles.size() > std::numeric_limits<uint32_t>::max() ||
        bytesPerTriangle > std::numeric_limits<size_t>::max() ||
        triangles.size() > std::numeric_limits<size_t>::max() / static_cast<size_t>(bytesPerTriangle)) {
        return std::nullopt;
    }

    TerrainOpacityMicromapCpuData result;
    result.alphaTextureHash = source.alphaTextureHash;
    result.profileHash = profileHash;
    result.subdivisionLevel = profile.subdivisionLevel;
    result.opacityData.assign(triangles.size() * static_cast<size_t>(bytesPerTriangle), kFourStatePackedUnknown);
    result.triangleRecords.resize(triangles.size());
    const uint32_t dimension = 1u << profile.subdivisionLevel;
    const float inverseDimension = 1.0f / static_cast<float>(dimension);
    for (size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex) {
        const size_t byteOffset = triangleIndex * static_cast<size_t>(bytesPerTriangle);
        if (byteOffset > std::numeric_limits<uint32_t>::max()) {
            return std::nullopt;
        }
        result.triangleRecords[triangleIndex] = {static_cast<uint32_t>(byteOffset),
                                                 static_cast<uint16_t>(profile.subdivisionLevel),
                                                 kTerrainOpacityMicromapFourStateFormat};
        for (uint32_t row = 0u; row < dimension; ++row) {
            for (uint32_t column = 0u; row + column < dimension; ++column) {
                const float b = static_cast<float>(row) * inverseDimension;
                const float c = static_cast<float>(column) * inverseDimension;
                classifyMicroTriangle(
                    result.opacityData, byteOffset, source, triangles[triangleIndex],
                    terrainOpacityMicromapIndex((static_cast<float>(row) + 1.0f / 3.0f) * inverseDimension,
                                                (static_cast<float>(column) + 1.0f / 3.0f) * inverseDimension,
                                                profile.subdivisionLevel),
                    b, c, b + inverseDimension, c, b, c + inverseDimension, result.counters);
                if (row + column + 1u < dimension) {
                    classifyMicroTriangle(
                        result.opacityData, byteOffset, source, triangles[triangleIndex],
                        terrainOpacityMicromapIndex((static_cast<float>(row) + 2.0f / 3.0f) * inverseDimension,
                                                    (static_cast<float>(column) + 2.0f / 3.0f) * inverseDimension,
                                                    profile.subdivisionLevel),
                        b + inverseDimension, c, b + inverseDimension, c + inverseDimension, b, c + inverseDimension,
                        result.counters);
                }
            }
        }
    }
    return result;
}

} // namespace renderer::contracts
