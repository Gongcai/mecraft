#ifndef MECRAFT_DROPRENDERER_H
#define MECRAFT_DROPRENDERER_H

#include <cstdint>
#include <unordered_map>

#include <glad/glad.h>

#include "Shader.h"
#include "../world/Block.h"

class Camera;
class ResourceMgr;
class Window;
class DropSystem;

class DropRenderer {
public:
	void init(ResourceMgr& resourceMgr);
	void shutdown();
	void render(const DropSystem& dropSystem, const Camera& camera, const Window& window);

private:
	struct Mesh {
		GLuint vao = 0;
		GLuint vbo = 0;
		uint32_t vertexCount = 0;
	};

	Mesh* getOrCreateBlockMesh(BlockID blockId);
	Mesh buildBlockMesh(BlockID blockId) const;
	static void destroyMesh(Mesh& mesh);

	ResourceMgr* m_resourceMgr = nullptr;
	Shader* m_shader = nullptr;
	std::unordered_map<BlockID, Mesh> m_blockMeshes;
};

#endif // MECRAFT_DROPRENDERER_H

