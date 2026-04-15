#ifndef MECRAFT_CHUNK_H
#define MECRAFT_CHUNK_H

#include <array>
#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include <glm/vec3.hpp>

#include "Block.h"
#include "Palette.h"
#include "BitPackedArray.h"

struct BlockVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float normal;
    float sunlight;
    float blockLight;
    float ao;
    float layer;
};

struct ChunkMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    uint32_t vertexCount = 0;
    GLsizeiptr vboCapacity = 0;  // tracks allocated VBO size for orphaning optimization

    GLuint transparentVao = 0;
    GLuint transparentVbo = 0;
    uint32_t transparentVertexCount = 0;
    GLsizeiptr transparentVboCapacity = 0;

    GLuint cutoutVao = 0;
    GLuint cutoutVbo = 0;
    uint32_t cutoutVertexCount = 0;
    GLsizeiptr cutoutVboCapacity = 0;

    bool hasBounds = false;
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);

    void upload(const std::vector<BlockVertex>& vertices);
    void uploadCutout(const std::vector<BlockVertex>& cutoutVerts);
    void uploadTransparent(const std::vector<BlockVertex>& transparentVerts);
    void destroy();
};

class Chunk {
public:
    static constexpr int SIZE_X = 16;
    static constexpr int SIZE_Y = 256;
    static constexpr int SIZE_Z = 16;
    static constexpr std::size_t BLOCK_COUNT = static_cast<std::size_t>(SIZE_X) * SIZE_Y * SIZE_Z;

    Chunk(int chunkX, int chunkZ);
    ~Chunk();

    [[nodiscard]] BlockID getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);
    void copyBlocksTo(std::array<BlockID, BLOCK_COUNT>& out) const;

    // Compact palette by removing unused entries and re-encoding block data
    void optimizePalette();

    static glm::ivec3 worldToLocal(int wx, int wy, int wz);
    [[nodiscard]] glm::ivec3 getWorldOffset() const;

    [[nodiscard]] bool isDirty() const;
    void markDirty();
    [[nodiscard]] uint64_t getMeshRevision() const;
    void setMesh(const ChunkMesh& mesh);
    void markMeshClean();
    [[nodiscard]] const ChunkMesh& getMesh() const;
    ChunkMesh& getMesh();

    [[nodiscard]] uint8_t getSunlight(int x, int y, int z) const;
    void setSunlight(int x, int y, int z, uint8_t level);
    [[nodiscard]] uint8_t getBlockLight(int x, int y, int z) const;
    void setBlockLight(int x, int y, int z, uint8_t level);

    [[nodiscard]] int getHeightMap(int x, int z) const;
    void setHeightMap(int x, int z, int height);
    void recalcHeightMap(int x, int z);

    Chunk* neighbors[4] = {nullptr, nullptr, nullptr, nullptr}; // +X, -X, +Z, -Z

    int m_chunkX;
    int m_chunkZ;

    [[nodiscard]] static std::size_t toIndex(int x, int y, int z);

    // Public for ChunkMesher snapshot capture
    std::array<uint8_t, BLOCK_COUNT> m_lightMap{}; // high nibble = sun, low nibble = block

private:
    [[nodiscard]] static bool isInBounds(int x, int y, int z);

    // Palette + BitPackedArray storage
    Palette m_palette;
    BitPackedArray m_blockData;

    std::array<int, static_cast<std::size_t>(SIZE_X) * SIZE_Z> m_heightMap{}; // highest opaque Y per column

    ChunkMesh m_mesh;

    bool m_dirty = true;
    uint64_t m_meshRevision = 1;
};

#endif // MECRAFT_CHUNK_H
