#ifndef MECRAFT_RENDERER_MESH_BLOCK_MESH_BUILDER_H
#define MECRAFT_RENDERER_MESH_BLOCK_MESH_BUILDER_H

#include <cstdint>
#include <vector>

#include "world/chunk/SubChunk.h"  // BlockVertex
#include "world/block/Block.h"     // BlockID

class ResourceMgr;

namespace renderer {

/// Simple owning GL mesh built from a flat BlockVertex list.
/// Vertex layout matches DropRenderer / GBufferPass expectations.
struct BlockCubeMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    uint32_t vertexCount = 0;

    [[nodiscard]] bool valid() const { return vao != 0 && vertexCount > 0; }
};

/// Build a unit-cube (or torch/cross where applicable) BlockVertex mesh for the
/// given block. The mesh occupies [0,1]^3 in local space with full-brightness
/// sunlight (suitable for entity rendering — lighting is queried per-frame at
/// render time). Returns an empty mesh (vertexCount=0) if the block has no mesh.
///
/// Extracted from DropRenderer::buildBlockMesh so falling-block entities and
/// drops share the same block-cube geometry source.
BlockCubeMesh buildBlockCubeMesh(BlockID blockId, const ResourceMgr& resourceMgr);

/// Upload a caller-built BlockVertex list to a GL mesh with the standard
/// block vertex layout (matches ChunkMesher / DropRenderer attrib bindings).
BlockCubeMesh uploadBlockCubeMesh(const std::vector<BlockVertex>& vertices);

/// Destroy GL resources owned by `mesh`. Safe to call on a zero-initialized mesh.
void destroyBlockCubeMesh(BlockCubeMesh& mesh);

} // namespace renderer

#endif // MECRAFT_RENDERER_MESH_BLOCK_MESH_BUILDER_H
