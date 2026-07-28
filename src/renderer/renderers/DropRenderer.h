#ifndef MECRAFT_DROPRENDERER_H
#define MECRAFT_DROPRENDERER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../rhi/RhiHandles.h"
#include "../contracts/SceneIdentityContract.h"
#include "../../world/block/Block.h"
#include "../../item/Item.h"

class IWorldView;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;
class World;
class DropSystem;

class DropRenderer {
public:
	void init(ResourceMgr& resourceMgr);
	void shutdown();
	[[nodiscard]] bool prepareFrame(const IWorldView& worldView,
	                                const DropSystem& dropSystem);
	void renderItemsToGBuffer(RhiCommandList& commandList,
	                          const glm::mat4& viewProj,
	                          const glm::mat4& previousViewProj);
	[[nodiscard]] bool prepareBlockGBuffer(RhiCommandList& commandList,
	                                       float animationTime);
	void renderBlocksToGBuffer(RhiCommandList& commandList,
	                           const glm::mat4& viewProj,
	                           const glm::mat4& previousViewProj);
	void finishGBufferFrame();
	void renderForward(RhiCommandList& commandList,
	                   const glm::mat4& viewProj,
	                   float skyIntensity,
	                   float animationTime);
	void renderToShadowMap(RhiCommandList& commandList,
	                       const glm::mat4& shadowViewProj,
	                       float animationTime);

private:
	struct Mesh {
		RhiBufferHandle rhiVertexBuffer;
		RhiDevice* rhiDevice = nullptr;
		uint32_t vertexCount = 0;
		renderer::contracts::StableMaterialId materialId;
	};

	struct PreparedDrop {
		const Mesh* mesh = nullptr;
		glm::mat4 model{1.0f};
		glm::mat4 previousModel{1.0f};
		glm::vec2 light{1.0f, 0.0f};
		renderer::contracts::StableObjectId objectId;
		renderer::contracts::StableMaterialId materialId;
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
	std::unordered_map<BlockID, Mesh> m_blockMeshes;
	std::unordered_map<ItemID, Mesh> m_itemMeshes;
	// Per-object velocity: stores previous-frame model matrix per drop (by drop ID).
	std::unordered_map<std::size_t, glm::mat4> m_previousModelMatrices;
	std::unordered_map<std::size_t, glm::mat4> m_currentModelMatrices;
	std::unordered_map<std::size_t, renderer::contracts::StableObjectId>
		m_dropObjectIds;
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
	RhiBufferHandle m_blockAnimationBuffer;
	RhiShaderHandle m_itemShadowVertexShader;
	RhiShaderHandle m_itemShadowFragmentShader;
	RhiPipelineLayoutHandle m_itemShadowPipelineLayout;
	RhiPipelineHandle m_itemShadowPipeline;
	RhiBindGroupHandle m_itemShadowBindGroup;
	RhiShaderHandle m_blockShadowVertexShader;
	RhiShaderHandle m_blockShadowFragmentShader;
	RhiPipelineLayoutHandle m_blockShadowPipelineLayout;
	RhiPipelineHandle m_blockShadowPipeline;
	RhiBindGroupHandle m_blockShadowBindGroup;
	RhiShaderHandle m_itemForwardVertexShader;
	RhiShaderHandle m_itemForwardFragmentShader;
	RhiPipelineLayoutHandle m_itemForwardPipelineLayout;
	RhiPipelineHandle m_itemForwardPipeline;
	RhiShaderHandle m_blockForwardVertexShader;
	RhiShaderHandle m_blockForwardFragmentShader;
	RhiPipelineLayoutHandle m_blockForwardPipelineLayout;
	RhiPipelineHandle m_blockForwardPipeline;
};

#endif // MECRAFT_DROPRENDERER_H
