#ifndef MECRAFT_DROPRENDERER_H
#define MECRAFT_DROPRENDERER_H

#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include "../core/Shader.h"
#include "../../world/block/Block.h"
#include "../../item/Item.h"

class Camera;
class IWorldView;
class ResourceMgr;
class Window;
class World;
class DropSystem;

class DropRenderer {
public:
	void init(ResourceMgr& resourceMgr);
	void shutdown();
	/// Switch to forward vanilla shaders (no CSM shadow / held_item_shadow contract).
	/// Must be called after init(). Reverts to deferred shaders if false.
	void setForwardMode(bool forward);
	void render(const DropSystem& dropSystem, const Camera& camera, const Window& window);
	// GBuffer path: renders drops into the deferred GBuffer (5 MRT).
	// Caller must have already bound the GBuffer FBO with terrain+entity depth.
	void renderToGBuffer(const IWorldView& worldView, const DropSystem& dropSystem,
	                     const glm::mat4& jitteredViewProj,
	                     const glm::mat4& previousViewProj,
	                     float animationTime);
	// Shadow path: renders drops into the CSM shadow map.
	// Caller must have already bound the shadow FBO layer.
	void renderToShadowMap(const IWorldView& worldView, const DropSystem& dropSystem,
	                        const glm::mat4& shadowViewProj,
	                        const glm::mat4& shadowView, const glm::mat4& shadowProjection,
	                        float animationTime, float shaderTime);

private:
	struct Mesh {
		uint32_t vao = 0;
		uint32_t vbo = 0;
		uint32_t vertexCount = 0;
	};

	Mesh* getOrCreateBlockMesh(BlockID blockId);
	Mesh buildBlockMesh(BlockID blockId) const;
	Mesh* getOrCreateItemMesh(ItemID itemId);
	Mesh buildItemMesh(ItemID itemId) const;
	static void destroyMesh(Mesh& mesh);
	// Query world light at a block position. Returns (sunlight, blocklight) normalized to [0,1].
	// Falls back to (1.0, 0.0) if chunk is not loaded.
	static glm::vec2 queryWorldLight(const IWorldView& worldView, const glm::vec3& position);

	ResourceMgr* m_resourceMgr = nullptr;
	Shader* m_shader = nullptr;
	Shader* m_itemShader = nullptr;
	Shader* m_deferredShader = nullptr;     // Original deferred block shader (drop_block)
	Shader* m_deferredItemShader = nullptr; // Original deferred item shader (item_model)
	Shader* m_gbufferShader = nullptr;     // drop_gbuffer: block drops → GBuffer
	Shader* m_itemGBufferShader = nullptr; // item_gbuffer: item drops → GBuffer
	Shader* m_shadowShader = nullptr;      // shadow_depth: block drops → shadow (reused)
	Shader* m_itemShadowShader = nullptr;  // item_shadow: item drops → shadow
	std::unordered_map<BlockID, Mesh> m_blockMeshes;
	std::unordered_map<ItemID, Mesh> m_itemMeshes;
	// Per-object velocity: stores previous-frame model matrix per drop (by drop ID).
	std::unordered_map<std::size_t, glm::mat4> m_previousModelMatrices;
};

#endif // MECRAFT_DROPRENDERER_H
