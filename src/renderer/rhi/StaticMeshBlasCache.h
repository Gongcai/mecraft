#ifndef MECRAFT_STATIC_MESH_BLAS_CACHE_H
#define MECRAFT_STATIC_MESH_BLAS_CACHE_H

#include "renderer/contracts/SceneIdentityContract.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/SceneBlasResource.h"

#include <cstdint>
#include <string>
#include <vector>

class RhiCommandListPool;
class RhiDevice;

namespace renderer::rt {

/// Describes one raster primitive included as a geometry in an asset-level static BLAS.
struct StaticMeshBlasGeometry {
    renderer::contracts::StableGeometryId geometryId;
    RhiBufferHandle vertexBuffer;
    RhiBufferHandle indexBuffer;
    uint64_t positionOffset = 0u;
    uint64_t vertexStride = 0u;
    uint32_t vertexCount = 0u;
    uint32_t indexCount = 0u;
    bool opaque = true;
    bool doubleSided = false;

    /// Returns the indexed triangle count consumed by the BLAS builder.
    [[nodiscard]] uint32_t primitiveCount() const { return indexCount / 3u; }
};

/// Classifies one asset-level build without conflating unsupported backends with failures.
enum class StaticMeshBlasBuildResult : uint8_t { Built, Empty, Unsupported, InvalidGeometry, Failed };

/// Reports immutable residency and geometry semantics for one asset-level static BLAS.
struct StaticMeshBlasStats {
    bool supported = false;
    bool resident = false;
    bool containsOpaque = false;
    bool containsCutout = false;
    bool containsDoubleSided = false;
    uint32_t geometryCount = 0u;
    uint64_t primitiveCount = 0u;
    uint64_t uncompactedBlasBytes = 0u;
    uint64_t compactedBlasBytes = 0u;
};

/// Builds and owns one compacted BLAS shared by every runtime instance of a static mesh asset.
class StaticMeshBlasCache final {
public:
    /// Initializes the cache against one device without creating acceleration structures.
    /// @param device Device that owns build resources and the final shared BLAS.
    /// @return True when the cache received a valid device.
    [[nodiscard]] bool init(RhiDevice* device);

    /// Releases the cache reference to the shared BLAS and resets all diagnostics.
    void shutdown();

    /// Builds and compacts one asset-level BLAS synchronously during asset loading.
    /// @param commandListPool Graphics command-list source used for build and compact submissions.
    /// @param geometries Solid raster primitives in stable asset order.
    /// @return Precise build classification; Failed and InvalidGeometry populate lastError().
    [[nodiscard]] StaticMeshBlasBuildResult build(RhiCommandListPool& commandListPool,
                                                  const std::vector<StaticMeshBlasGeometry>& geometries);

    [[nodiscard]] bool supported() const { return m_supported; }
    [[nodiscard]] const SceneBlasResourcePtr& resource() const { return m_resource; }
    [[nodiscard]] const StaticMeshBlasStats& stats() const { return m_stats; }
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

private:
    void setError(const char* message);

    RhiDevice* m_device = nullptr;
    bool m_initialized = false;
    bool m_supported = false;
    SceneBlasResourcePtr m_resource;
    StaticMeshBlasStats m_stats;
    std::string m_lastError;
};

} // namespace renderer::rt

#endif // MECRAFT_STATIC_MESH_BLAS_CACHE_H
