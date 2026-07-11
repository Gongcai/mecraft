#ifndef MECRAFT_DROPRENDERER_H
#define MECRAFT_DROPRENDERER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../core/Shader.h"
#include "../rhi/RhiHandles.h"
#include "../../world/block/Block.h"
#include "../../item/Item.h"

class Camera;
class IWorldView;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;
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
	void prepareFrame(const IWorldView& worldView, const DropSystem& dropSystem);
	void renderItemsToGBuffer(RhiCommandList& commandList,
	                          const glm::mat4& viewProj,
	                          const glm::mat4& previousViewProj);
	void renderBlocksToGBuffer(RhiCommandList& commandList,
	                           const glm::mat4& viewProj,
	                           const glm::mat4& previousViewProj,
	                           float animationTime);
	void finishGBufferFrame();
	void render(const DropSystem& dropSystem, const Camera& camera, const Window& window);
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
		RhiBufferHandle rhiVertexBuffer;
		RhiDevice* rhiDevice = nullptr;
		uint32_t vertexCount = 0;
	};

	struct PreparedDrop {
		const Mesh* mesh = nullptr;
		glm::mat4 model{1.0f};
		glm::mat4 previousModel{1.0f};
		glm::vec2 light{1.0f, 0.0f};
		bool itemMesh = false;
	};

	Mesh* getOrCreateBlockMesh(BlockID blockId);
	Mesh buildBlockMesh(BlockID blockId) const;
	Mesh* getOrCreateItemMesh(ItemID itemId);
	Mesh buildItemMesh(ItemID itemId) const;
	static void destroyMesh(Mesh& mesh);
	void createItemGBufferRhiResources();
	void destroyItemGBufferRhiResources();
	// Query world light at a block position. Returns (sunlight, blocklight) normalized to [0,1].
	// Falls back to (1.0, 0.0) if chunk is not loaded.
	static glm::vec2 queryWorldLight(const IWorldView& worldView, const glm::vec3& position);

	ResourceMgr* m_resourceMgr = nullptr;
	Shader* m_shader = nullptr;
	Shader* m_itemShader = nullptr;
	Shader* m_deferredShader = nullptr;     // Original deferred block shader (drop_block)
	Shader* m_deferredItemShader = nullptr; // Original deferred item shader (item_model)
	Shader* m_shadowShader = nullptr;      // shadow_depth: block drops → shadow (reused)
	Shader* m_itemShadowShader = nullptr;  // item_shadow: item drops → shadow
	std::unordered_map<BlockID, Mesh> m_blockMeshes;
	std::unordered_map<ItemID, Mesh> m_itemMeshes;
	// Per-object velocity: stores previous-frame model matrix per drop (by drop ID).
	std::unordered_map<std::size_t, glm::mat4> m_previousModelMatrices;
	std::unordered_map<std::size_t, glm::mat4> m_currentModelMatrices;
	std::vector<PreparedDrop> m_preparedDrops;
	RhiDevice* m_rhiDevice = nullptr;
	RhiTextureViewHandle m_itemAtlasView;
	RhiSamplerHandle m_itemSampler;
	RhiShaderHandle m_itemGBufferVertexShader;
	RhiShaderHandle m_itemGBufferFragmentShader;
	RhiBindGroupLayoutHandle m_itemGBufferBindGroupLayout;
	RhiPipelineLayoutHandle m_itemGBufferPipelineLayout;
	RhiPipelineHandle m_itemGBufferPipeline;
	RhiBindGroupHandle m_itemGBufferBindGroup;
	RhiTextureViewHandle m_blockTextureArrayView;
	RhiTextureViewHandle m_grassColormapView;
	RhiTextureViewHandle m_foliageColormapView;
	RhiSamplerHandle m_blockSampler;
	RhiShaderHandle m_blockGBufferVertexShader;
	RhiShaderHandle m_blockGBufferFragmentShader;
	RhiBindGroupLayoutHandle m_blockGBufferBindGroupLayout;
	RhiPipelineLayoutHandle m_blockGBufferPipelineLayout;
	RhiPipelineHandle m_blockGBufferPipeline;
	RhiBindGroupHandle m_blockGBufferBindGroup;
};

#endif // MECRAFT_DROPRENDERER_H
