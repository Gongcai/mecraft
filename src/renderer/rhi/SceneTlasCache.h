#ifndef MECRAFT_SCENE_TLAS_CACHE_H
#define MECRAFT_SCENE_TLAS_CACHE_H

#include "renderer/contracts/TerrainRayTracingContract.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/SceneBlasResource.h"
#include "renderer/rhi/RhiTypes.h"

#include <glm/mat4x4.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class RhiCommandList;
class RhiDevice;

namespace renderer::rt {

/// Classifies stable scene identities so heterogeneous instances sort deterministically.
enum class SceneTlasInstanceKind : uint8_t { Terrain = 0u, StaticMesh = 1u, FirstPerson = 2u };

/// Stable CPU identity used to order TLAS instances and resolve custom indices.
struct SceneTlasInstanceKey {
    SceneTlasInstanceKind kind = SceneTlasInstanceKind::Terrain;
    int64_t primary = 0;
    int64_t secondary = 0;

    [[nodiscard]] bool operator==(const SceneTlasInstanceKey& other) const {
        return kind == other.kind && primary == other.primary && secondary == other.secondary;
    }

    [[nodiscard]] bool operator<(const SceneTlasInstanceKey& other) const {
        if (kind != other.kind) {
            return static_cast<uint8_t>(kind) < static_cast<uint8_t>(other.kind);
        }
        if (primary != other.primary) {
            return primary < other.primary;
        }
        return secondary < other.secondary;
    }
};

/// Eight-bit visibility classes consumed by ray-query and ray-tracing pipelines.
enum class SceneTlasInstanceMask : uint8_t {
    GiOpaque = 1u << 0u,
    GiCutout = 1u << 1u,
    ShadowCaster = 1u << 2u,
    ReflectionVisible = 1u << 3u,
    FirstPerson = 1u << 4u
};

[[nodiscard]] constexpr uint8_t sceneTlasMaskBit(const SceneTlasInstanceMask mask) {
    return static_cast<uint8_t>(mask);
}

/// Binds canonical terrain hit-data addresses to the exact retained buffers that provide each role.
struct SceneTlasTerrainHitData final {
    renderer::contracts::TerrainRayTracingHitData rayTracing;
    RhiBufferHandle vertexBuffer;
    RhiBufferHandle primitiveMetadataBuffer;

    [[nodiscard]] bool operator==(const SceneTlasTerrainHitData& other) const {
        return rayTracing == other.rayTracing && vertexBuffer.index == other.vertexBuffer.index &&
               vertexBuffer.generation == other.vertexBuffer.generation &&
               primitiveMetadataBuffer.index == other.primitiveMetadataBuffer.index &&
               primitiveMetadataBuffer.generation == other.primitiveMetadataBuffer.generation;
    }
};

/// CPU instance descriptor supplied by terrain and static-mesh scene producers.
struct SceneTlasInstanceInput {
    SceneTlasInstanceKey key;
    SceneBlasResourcePtr blas;
    glm::mat4 transform{1.0f};
    uint8_t mask = 0u;
    bool doubleSided = false;
    std::optional<SceneTlasTerrainHitData> terrainHitData;
};

/// Resolves one generated 24-bit custom index back to its stable identity and immutable terrain hit data.
struct SceneTlasInstanceMapping {
    uint32_t customIndex = 0u;
    SceneTlasInstanceKey key;
    std::optional<SceneTlasTerrainHitData> terrainHitData;
};

/// Classifies an instance-list transaction independently from graph recording.
enum class SceneTlasSetResult : uint8_t { Accepted, Unchanged, Unsupported, InvalidInstance };

/// Read-only snapshot of the latest completed TLAS generation.
struct SceneTlasView {
    uint64_t revision = 0u;
    RhiAccelerationStructureHandle accelerationStructure;
    RhiBufferHandle instanceBuffer;
    RhiBufferHandle terrainHitDataBuffer;
    uint64_t deviceAddress = 0u;
    uint32_t instanceCount = 0u;
    uint32_t blasCount = 0u;
    uint64_t instanceBytes = 0u;
    uint64_t terrainHitDataBytes = 0u;
    uint64_t blasBytes = 0u;
    uint64_t tlasBytes = 0u;
    std::vector<SceneTlasInstanceMapping> mappings;
};

/// Aggregated TLAS generation and resource-lifetime diagnostics.
struct SceneTlasStats {
    bool supported = false;
    bool healthy = true;
    bool active = false;
    bool pending = false;
    uint32_t desiredInstanceCount = 0u;
    uint32_t activeInstanceCount = 0u;
    uint32_t activeBlasCount = 0u;
    uint32_t retiredGenerationCount = 0u;
    uint64_t desiredRevision = 0u;
    uint64_t activeRevision = 0u;
    uint64_t activeInstanceBytes = 0u;
    uint64_t activeTerrainHitDataBytes = 0u;
    uint64_t activeBlasBytes = 0u;
    uint64_t activeTlasBytes = 0u;
    uint64_t buildsRecorded = 0u;
    uint64_t buildsCompleted = 0u;
};

/// Builds transactional TLAS generations while retaining every referenced BLAS until GPU retirement completes.
class SceneTlasCache final {
public:
    /// Initializes the cache against one RHI device.
    /// @param device Device that owns all generated TLAS resources.
    /// @return True when initialization state is valid; unsupported backends remain explicit no-AS caches.
    [[nodiscard]] bool init(RhiDevice* device);

    /// Waits for submitted generations and releases every TLAS and shared BLAS reference.
    void shutdown();

    /// Polls submitted generations, promotes completed builds, and reclaims retired resources.
    void beginFrame();

    /// Replaces the desired scene instance snapshot after validation and stable sorting.
    /// @param instances Complete scene snapshot; an empty vector explicitly retires the active TLAS.
    /// @return Accepted, Unchanged, Unsupported, or InvalidInstance.
    [[nodiscard]] SceneTlasSetResult setInstances(std::vector<SceneTlasInstanceInput> instances);

    /// Records one required TLAS build into the current Render Graph command list.
    /// @return False when resource creation or command recording fails.
    [[nodiscard]] bool recordFrame(RhiCommandList& commandList);

    /// Commits the recorded build and active-generation usage to the graph completion token.
    /// @param succeeded True only when the complete graph submission succeeded.
    /// @param completionToken Last accepted graph submission, including partial-failure submissions.
    void finishGraphExecution(bool succeeded, RhiSubmissionToken completionToken);

    [[nodiscard]] bool supported() const { return m_supported; }
    [[nodiscard]] bool healthy() const { return m_healthy; }
    [[nodiscard]] bool isSettled() const;
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }
    [[nodiscard]] std::optional<SceneTlasView> activeView() const;
    [[nodiscard]] SceneTlasStats stats() const;

    /// Converts a finite affine GLM transform into the native row-major 3x4 instance layout.
    /// @param transform Column-major local-to-world transform used by raster rendering.
    /// @param nativeTransform Receives the exact row-major TLAS transform on success.
    /// @return False for non-finite, singular, or non-affine transforms.
    [[nodiscard]] static bool encodeTransform(const glm::mat4& transform, std::array<float, 12u>& nativeTransform);

    /// Returns instance-facing flags that match the raster CCW and double-sided contract.
    /// @param doubleSided True when any geometry in the referenced BLAS disables raster culling.
    /// @return Counter-clockwise facing plus optional cull disable.
    [[nodiscard]] static RhiAccelerationStructureInstanceFlags instanceFlags(bool doubleSided);

private:
    enum class PendingState : uint8_t { Recorded, Submitted };

    struct NormalizedInput {
        SceneTlasInstanceInput source;
        RhiAccelerationStructureInstance native;

        [[nodiscard]] bool operator==(const NormalizedInput& other) const;
    };

    struct Generation {
        uint64_t revision = 0u;
        RhiBufferHandle instanceBuffer;
        RhiBufferHandle terrainHitDataBuffer;
        RhiBufferHandle storageBuffer;
        RhiBufferHandle scratchBuffer;
        RhiAccelerationStructureHandle accelerationStructure;
        uint64_t deviceAddress = 0u;
        uint64_t instanceBytes = 0u;
        uint64_t terrainHitDataBytes = 0u;
        uint64_t blasBytes = 0u;
        uint64_t tlasBytes = 0u;
        std::vector<SceneBlasResourcePtr> blasResources;
        std::vector<SceneTlasInstanceMapping> mappings;
        RhiSubmissionToken submissionToken;
        RhiSubmissionToken lastUseToken;
    };

    struct PendingGeneration {
        PendingState state = PendingState::Recorded;
        Generation generation;
    };

    struct RetiredGeneration {
        Generation generation;
        RhiSubmissionToken completionToken;
    };

    [[nodiscard]] bool pollSubmittedGeneration();
    [[nodiscard]] bool pollRetiredGenerations();
    void retireActiveGeneration();
    void applyEmptyDesiredGeneration();
    void destroyGeneration(Generation& generation);
    void setTransientError(const char* message);
    void setFatalError(const char* message);

    RhiDevice* m_device = nullptr;
    bool m_initialized = false;
    bool m_supported = false;
    bool m_healthy = true;
    uint64_t m_nextRevision = 1u;
    uint64_t m_buildsRecorded = 0u;
    uint64_t m_buildsCompleted = 0u;
    uint64_t m_desiredRevision = 0u;
    std::vector<NormalizedInput> m_desiredInputs;
    std::optional<PendingGeneration> m_pending;
    std::optional<Generation> m_active;
    std::vector<RetiredGeneration> m_retired;
    std::string m_lastError;
};

} // namespace renderer::rt

#endif // MECRAFT_SCENE_TLAS_CACHE_H
