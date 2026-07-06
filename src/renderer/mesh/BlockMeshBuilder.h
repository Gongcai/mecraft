#ifndef MECRAFT_RENDERER_MESH_BLOCK_MESH_BUILDER_H
#define MECRAFT_RENDERER_MESH_BLOCK_MESH_BUILDER_H

#include <cstdint>
#include <vector>

#include "world/chunk/SubChunk.h"  // BlockVertex
#include "world/block/Block.h"     // BlockID
#include "world/block/BlockStateRegistry.h"

class ResourceMgr;

namespace renderer {

/// Simple owning GL mesh built from a flat BlockVertex list.
/// Vertex layout matches DropRenderer / GBufferPass expectations.
struct BlockCubeMesh {
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t vertexCount = 0;

    [[nodiscard]] bool valid() const { return vao != 0 && vertexCount > 0; }
};

/// Build local-space BlockVertex geometry for a block-backed item/entity mesh.
/// Model-shaped blocks use their default state model variant; cube, cross, and
/// torch blocks use compact local meshes. The geometry occupies [0,1]^3 and
/// uses full brightness vertex values so entity renderers can shade it per draw.
[[nodiscard]] std::vector<BlockVertex> buildBlockMeshVertices(BlockID blockId, const ResourceMgr& resourceMgr);

/// Build local-space geometry for a specific block state.
/// Model-shaped blocks use the state's selected model variant.
[[nodiscard]] std::vector<BlockVertex> buildBlockMeshVerticesForState(BlockStateId stateId,
                                                                      const ResourceMgr& resourceMgr);

/// Build and upload a block-backed item/entity mesh. Returns an empty mesh
/// (vertexCount=0) if the block has no renderable geometry.
BlockCubeMesh buildBlockCubeMesh(BlockID blockId, const ResourceMgr& resourceMgr);

/// Build and upload a block-backed item/entity mesh for a specific state.
BlockCubeMesh buildBlockStateCubeMesh(BlockStateId stateId, const ResourceMgr& resourceMgr);

/// Upload a caller-built BlockVertex list to a GL mesh with the standard
/// block vertex layout (matches ChunkMesher / DropRenderer attrib bindings).
BlockCubeMesh uploadBlockCubeMesh(const std::vector<BlockVertex>& vertices);

/// Destroy GL resources owned by `mesh`. Safe to call on a zero-initialized mesh.
void destroyBlockCubeMesh(BlockCubeMesh& mesh);

} // namespace renderer

#endif // MECRAFT_RENDERER_MESH_BLOCK_MESH_BUILDER_H
