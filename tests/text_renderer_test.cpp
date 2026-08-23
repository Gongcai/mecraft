#include "../src/Paths.h"
#include "../src/renderer/rhi/RhiCommandList.h"
#include "../src/renderer/rhi/RhiDevice.h"
#include "../src/ui/core/UIRenderContext.h"
#include "../src/ui/font/GlyphAtlas.h"
#include "../src/ui/font/TextRenderer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[text_renderer_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool nearlyEqual(const float left, const float right) {
    return std::fabs(left - right) <= 1.0e-6f;
}

void appendUtf8(std::string& output, const uint32_t codepoint) {
    if (codepoint <= 0x7fu) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
}

class RecordingCommandList final : public RhiCommandList {
public:
    bool begin(const RhiCommandListDesc&) override {
        commandState = RhiCommandListState::Recording;
        return true;
    }
    bool end() override {
        commandState = RhiCommandListState::Executable;
        return true;
    }
    [[nodiscard]] RhiCommandListState state() const override { return commandState; }
    enum class EventType {
        TextureBarrier,
        CopyBufferToTexture,
        SetScissor,
        SetGraphicsPipeline,
        SetBindGroup,
        SetVertexBuffer,
        PushConstants,
        Draw,
        UpdateBuffer
    };

    struct Event {
        EventType type;
        RhiRect2D scissor{};
        uint32_t vertexCount = 0u;
    };

    void beginDebugLabel(const char*, const glm::vec4&) override {}
    void endDebugLabel() override {}
    void insertDebugMarker(const char*, const glm::vec4&) override {}

    void textureBarrier(const RhiTextureBarrier& barrier) override {
        textureBarriers.push_back(barrier);
        events.push_back({EventType::TextureBarrier});
    }
    void bufferBarrier(const RhiBufferBarrier&) override {}
    bool accelerationStructureBarrier(const RhiAccelerationStructureBarrier&) override { return false; }

    void beginRendering(const RhiRenderingInfo&) override {}
    void endRendering() override {}
    void clearDepthAttachment(float, const RhiRect2D&) override {}

    void setViewport(const RhiViewport&) override {}
    void setScissor(const RhiRect2D& rect) override {
        scissors.push_back(rect);
        Event event{EventType::SetScissor};
        event.scissor = rect;
        events.push_back(event);
    }
    void setGraphicsPipeline(RhiPipelineHandle) override { events.push_back({EventType::SetGraphicsPipeline}); }
    void setComputePipeline(RhiPipelineHandle) override {}
    void setBindGroup(uint32_t, RhiBindGroupHandle) override { events.push_back({EventType::SetBindGroup}); }
    void setVertexBuffer(uint32_t, RhiBufferHandle, uint64_t) override {
        events.push_back({EventType::SetVertexBuffer});
    }
    void setIndexBuffer(RhiBufferHandle, RhiIndexFormat, uint64_t) override {}
    void pushConstants(const void*, size_t, RhiShaderStageFlags) override {
        events.push_back({EventType::PushConstants});
    }

    void draw(const uint32_t vertexCount, uint32_t, uint32_t, uint32_t) override {
        Event event{EventType::Draw};
        event.vertexCount = vertexCount;
        events.push_back(event);
        drawVertexCounts.push_back(vertexCount);
    }
    void drawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
    void drawIndirect(RhiBufferHandle, uint64_t, uint32_t, uint32_t) override {}
    void drawIndexedIndirectCount(RhiBufferHandle, uint64_t, uint32_t, uint32_t, RhiBufferHandle, uint64_t) override {}
    void dispatch(uint32_t, uint32_t, uint32_t) override {}

    void updateBuffer(RhiBufferHandle, uint64_t, const void*, const size_t size) override {
        updatedByteCounts.push_back(size);
        events.push_back({EventType::UpdateBuffer});
    }
    void copyBuffer(const RhiBufferCopy&) override {}
    void copyBufferToTexture(const RhiBufferTextureCopy& copy) override {
        bufferTextureCopies.push_back(copy);
        events.push_back({EventType::CopyBufferToTexture});
    }
    void copyTextureToBuffer(const RhiTextureBufferCopy&) override {}
    void copyTexture(const RhiTextureCopy&) override {}
    void blitTexture(const RhiTextureBlit&) override {}
    void generateMipmaps(RhiTextureHandle) override {}

    void resetQueryPool(RhiQueryPoolHandle, uint32_t, uint32_t) override {}
    void writeTimestamp(RhiQueryPoolHandle, uint32_t) override {}
    bool buildAccelerationStructures(const RhiAccelerationStructureBuildDesc*, uint32_t) override { return false; }
    bool buildMicromaps(const RhiMicromapBuildDesc*, uint32_t) override { return false; }
    bool copyAccelerationStructure(const RhiAccelerationStructureCopyDesc&) override { return false; }
    bool writeAccelerationStructureProperties(const RhiAccelerationStructurePropertyQueryDesc&) override {
        return false;
    }

    void clearDrawRecording() {
        events.clear();
        scissors.clear();
        drawVertexCounts.clear();
        updatedByteCounts.clear();
    }

    std::vector<Event> events;
    std::vector<RhiTextureBarrier> textureBarriers;
    std::vector<RhiBufferTextureCopy> bufferTextureCopies;
    std::vector<RhiRect2D> scissors;
    std::vector<uint32_t> drawVertexCounts;
    std::vector<size_t> updatedByteCounts;
    RhiCommandListState commandState = RhiCommandListState::Initial;
};

class RecordingCommandListPool final : public RhiCommandListPool {
public:
    explicit RecordingCommandListPool(RecordingCommandList& commandList) : m_commandList(commandList) {}

    [[nodiscard]] RhiCommandList* acquire(RhiCommandListType type) override {
        if (type != RhiCommandListType::Graphics || m_commandList.state() == RhiCommandListState::Recording) {
            return nullptr;
        }
        return &m_commandList;
    }

    bool reset() override {
        if (m_commandList.state() == RhiCommandListState::Recording) {
            return false;
        }
        m_commandList.commandState = RhiCommandListState::Initial;
        return true;
    }

private:
    RecordingCommandList& m_commandList;
};

class RecordingDevice final : public RhiDevice {
public:
    bool prepareWindowCreation() override { return true; }
    bool init(const RhiDeviceDesc&) override { return true; }
    void shutdown() override {}

    [[nodiscard]] RhiBackend backend() const override { return RhiBackend::OpenGL; }
    [[nodiscard]] const RhiCapabilities& capabilities() const override { return m_capabilities; }
    [[nodiscard]] RhiMemoryStats memoryStats() const override { return {}; }

    RhiBufferHandle createBuffer(const RhiBufferDesc& desc, const void*, const size_t initialDataSize) override {
        bufferDescs.push_back(desc);
        bufferInitialDataSizes.push_back(initialDataSize);
        return {m_nextBuffer++, 1u};
    }
    RhiTextureHandle createTexture(const RhiTextureDesc& desc, const RhiTextureInitialData*) override {
        textureDescs.push_back(desc);
        return {m_nextTexture++, 1u};
    }
    [[nodiscard]] bool getBufferDesc(const RhiBufferHandle buffer, RhiBufferDesc& desc) const override {
        if (!buffer.isValid() || buffer.generation != 1u || buffer.index > bufferDescs.size())
            return false;
        desc = bufferDescs[buffer.index - 1u];
        return true;
    }
    [[nodiscard]] bool getTextureDesc(const RhiTextureHandle texture, RhiTextureDesc& desc) const override {
        if (!texture.isValid() || texture.generation != 1u || texture.index > textureDescs.size())
            return false;
        desc = textureDescs[texture.index - 1u];
        return true;
    }
    [[nodiscard]] bool getTextureViewDesc(const RhiTextureViewHandle textureView,
                                          RhiTextureViewDesc& desc) const override {
        if (!textureView.isValid() || textureView.generation != 1u || textureView.index > textureViewDescs.size())
            return false;
        desc = textureViewDescs[textureView.index - 1u];
        return true;
    }
    [[nodiscard]] bool getSamplerDesc(const RhiSamplerHandle sampler, RhiSamplerDesc& desc) const override {
        if (!sampler.isValid() || sampler.generation != 1u || sampler.index > samplerDescs.size())
            return false;
        desc = samplerDescs[sampler.index - 1u];
        return true;
    }
    RhiTextureViewHandle createTextureView(const RhiTextureViewDesc& desc) override {
        textureViewDescs.push_back(desc);
        return {m_nextTextureView++, 1u};
    }
    RhiSamplerHandle createSampler(const RhiSamplerDesc& desc) override {
        samplerDescs.push_back(desc);
        return {m_nextSampler++, 1u};
    }
    RhiShaderHandle createShader(const RhiShaderDesc& desc) override {
        shaderStages.push_back(desc.stage);
        return {m_nextShader++, 1u};
    }
    RhiBindGroupLayoutHandle createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) override {
        bindGroupLayoutDescs.push_back(desc);
        return {m_nextBindGroupLayout++, 1u};
    }
    RhiPipelineLayoutHandle createPipelineLayout(const RhiPipelineLayoutDesc& desc) override {
        pipelineLayoutDescs.push_back(desc);
        return {m_nextPipelineLayout++, 1u};
    }
    RhiPipelineHandle createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override {
        graphicsPipelineDescs.push_back(desc);
        return {m_nextPipeline++, 1u};
    }
    RhiPipelineHandle createComputePipeline(const RhiComputePipelineDesc&) override { return {m_nextPipeline++, 1u}; }
    RhiBindGroupHandle createBindGroup(const RhiBindGroupDesc& desc) override {
        bindGroupDescs.push_back(desc);
        return {m_nextBindGroup++, 1u};
    }
    RhiAccelerationStructureHandle createAccelerationStructure(const RhiAccelerationStructureDesc&) override {
        return {};
    }
    RhiMicromapHandle createMicromap(const RhiMicromapDesc&) override { return {}; }
    [[nodiscard]] bool getMicromapDesc(RhiMicromapHandle, RhiMicromapDesc&) const override { return false; }
    [[nodiscard]] bool queryMicromapBuildSizes(const RhiMicromapBuildInput&, RhiMicromapBuildSizes&) const override {
        return false;
    }
    [[nodiscard]] bool getAccelerationStructureDesc(RhiAccelerationStructureHandle,
                                                    RhiAccelerationStructureDesc&) const override {
        return false;
    }
    [[nodiscard]] bool queryAccelerationStructureBuildSizes(const RhiAccelerationStructureBuildInput&,
                                                            RhiAccelerationStructureBuildSizes&) const override {
        return false;
    }
    [[nodiscard]] uint64_t bufferDeviceAddress(RhiBufferHandle) const override { return 0u; }
    [[nodiscard]] uint64_t accelerationStructureDeviceAddress(RhiAccelerationStructureHandle) const override {
        return 0u;
    }
    bool updateBindGroups(const RhiBindGroupUpdate*, uint32_t) override { return true; }
    RhiQueryPoolHandle createQueryPool(const RhiQueryPoolDesc&) override { return {m_nextQueryPool++, 1u}; }
    bool resetQueryPool(RhiQueryPoolHandle, uint32_t, uint32_t) override { return true; }
    void* mapBuffer(RhiBufferHandle, uint64_t, uint64_t) override { return nullptr; }
    void unmapBuffer(RhiBufferHandle) override {}
    [[nodiscard]] bool areQueryResultsAvailable(RhiQueryPoolHandle, uint32_t, uint32_t) const override { return false; }
    bool getQueryResults(RhiQueryPoolHandle, uint32_t, uint32_t, uint64_t*) const override { return false; }

    [[nodiscard]] RhiTextureViewHandle currentSwapchainColorView() const override { return {}; }
    [[nodiscard]] RhiTextureViewHandle currentSwapchainDepthStencilView() const override { return {}; }
    [[nodiscard]] RhiTextureHandle currentSwapchainColorTexture() const override { return {}; }
    [[nodiscard]] RhiTextureFormat swapchainColorFormat() const override { return RhiTextureFormat::Rgba8Unorm; }
    [[nodiscard]] RhiTextureFormat swapchainDepthStencilFormat() const override { return RhiTextureFormat::Depth24; }
    [[nodiscard]] bool vsyncEnabled() const override { return false; }
    bool setVsyncEnabled(bool) override { return true; }
    bool resizeSwapchain(uint32_t, uint32_t) override { return true; }
    [[nodiscard]] RhiFrameAcquireResult acquireFrame() override {
        return {RhiFrameStatus::Success, 0u, 0u, 1u, 1u, {}, {}, {}};
    }
    RhiFrameStatus presentFrame(const RhiPresentInfo&) override { return RhiFrameStatus::Success; }
    bool cancelFrame(const RhiPresentInfo&) override { return true; }

    void destroyBuffer(const RhiBufferHandle handle) override { destroyedBuffers.push_back(handle); }
    void destroyTexture(const RhiTextureHandle handle) override { destroyedTextures.push_back(handle); }
    void destroyTextureView(RhiTextureViewHandle) override {}
    void destroySampler(RhiSamplerHandle) override {}
    void destroyShader(RhiShaderHandle) override {}
    void destroyBindGroupLayout(RhiBindGroupLayoutHandle) override {}
    void destroyPipelineLayout(RhiPipelineLayoutHandle) override {}
    void destroyPipeline(RhiPipelineHandle) override {}
    void destroyBindGroup(RhiBindGroupHandle) override {}
    void destroyQueryPool(RhiQueryPoolHandle) override {}
    void destroyAccelerationStructure(RhiAccelerationStructureHandle) override {}
    void destroyMicromap(RhiMicromapHandle) override {}

    [[nodiscard]] std::unique_ptr<RhiCommandListPool> createCommandListPool(const RhiCommandListPoolDesc&) override {
        return std::make_unique<RecordingCommandListPool>(commandList);
    }
    bool submit(const RhiSubmitInfo& info, RhiSubmissionToken* completionToken = nullptr) override {
        if (completionToken != nullptr) {
            *completionToken = {1u, 1u};
        }
        return info.commandListCount == 1u && info.commandLists != nullptr && info.commandLists[0] == &commandList &&
               commandList.state() == RhiCommandListState::Executable;
    }
    [[nodiscard]] bool isSubmissionComplete(RhiSubmissionToken token, bool& complete) override {
        complete = token.deviceId == 1u && token.sequence == 1u;
        return complete;
    }
    bool waitForSubmission(RhiSubmissionToken token) override { return token.deviceId == 1u && token.sequence == 1u; }
    void waitIdle() override {}

    RecordingCommandList commandList;
    std::vector<RhiBufferDesc> bufferDescs;
    std::vector<size_t> bufferInitialDataSizes;
    std::vector<RhiTextureDesc> textureDescs;
    std::vector<RhiTextureViewDesc> textureViewDescs;
    std::vector<RhiSamplerDesc> samplerDescs;
    std::vector<RhiShaderStage> shaderStages;
    std::vector<RhiBindGroupLayoutDesc> bindGroupLayoutDescs;
    std::vector<RhiPipelineLayoutDesc> pipelineLayoutDescs;
    std::vector<RhiGraphicsPipelineDesc> graphicsPipelineDescs;
    std::vector<RhiBindGroupDesc> bindGroupDescs;
    std::vector<RhiBufferHandle> destroyedBuffers;
    std::vector<RhiTextureHandle> destroyedTextures;

private:
    RhiCommandListState commandState = RhiCommandListState::Initial;
    RhiCapabilities m_capabilities{};
    uint32_t m_nextBuffer = 1u;
    uint32_t m_nextTexture = 1u;
    uint32_t m_nextTextureView = 1u;
    uint32_t m_nextSampler = 1u;
    uint32_t m_nextShader = 1u;
    uint32_t m_nextBindGroupLayout = 1u;
    uint32_t m_nextPipelineLayout = 1u;
    uint32_t m_nextPipeline = 1u;
    uint32_t m_nextBindGroup = 1u;
    uint32_t m_nextQueryPool = 1u;
};

bool testUnicodeRasterizationAndStrictUtf8() {
    RecordingDevice device;
    GlyphAtlas atlas;
    if (!requireTrue(atlas.init(device, DEFAULT_FONT_PATH, 48),
                     "glyph atlas must initialize with the project Unicode font")) {
        return false;
    }

    constexpr std::string_view kUnicodeText = u8"中文界面";
    if (!requireTrue(atlas.ensureGlyphs(kUnicodeText), "valid multi-byte UTF-8 text must rasterize")) {
        return false;
    }
    const GlyphInfo* chineseGlyph = atlas.findGlyph(0x4e2du);
    if (!requireTrue(chineseGlyph != nullptr && chineseGlyph->bitmapWidth > 0 && chineseGlyph->bitmapHeight > 0 &&
                         chineseGlyph->advanceX > 0,
                     "a CJK codepoint must produce visible glyph metrics")) {
        return false;
    }
    if (!requireTrue(nearlyEqual(chineseGlyph->uvMinX,
                                 static_cast<float>(chineseGlyph->atlasX) / static_cast<float>(atlas.atlasWidth())) &&
                         nearlyEqual(chineseGlyph->uvMaxY,
                                     static_cast<float>(chineseGlyph->atlasY + chineseGlyph->bitmapHeight) /
                                         static_cast<float>(atlas.atlasHeight())),
                     "Unicode glyph UVs must derive from their atlas pixel rectangle")) {
        return false;
    }

    const uint64_t revisionBeforeMalformedInput = atlas.revision();
    const std::array<char, 2> malformed = {static_cast<char>(0xe4), static_cast<char>(0xb8)};
    if (!requireTrue(!atlas.ensureGlyphs(std::string_view(malformed.data(), malformed.size())),
                     "truncated UTF-8 must be rejected")) {
        return false;
    }
    if (!requireTrue(atlas.revision() == revisionBeforeMalformedInput, "rejected UTF-8 must not mutate the atlas")) {
        return false;
    }

    atlas.shutdown();
    return true;
}

bool testAtlasGrowthRefreshesExistingGlyphUvs() {
    RecordingDevice device;
    GlyphAtlas atlas;
    if (!requireTrue(atlas.init(device, DEFAULT_FONT_PATH, 64), "large glyph atlas test must initialize")) {
        return false;
    }
    if (!requireTrue(atlas.ensureGlyphs(u8"中"), "seed glyph must rasterize")) {
        return false;
    }
    const GlyphInfo seedBeforeGrowth = *atlas.findGlyph(0x4e2du);
    const int widthBeforeGrowth = atlas.atlasWidth();
    const int heightBeforeGrowth = atlas.atlasHeight();

    std::string manyGlyphs;
    for (uint32_t codepoint = 0x4e00u; codepoint < 0x4f80u; ++codepoint) {
        appendUtf8(manyGlyphs, codepoint);
    }
    if (!requireTrue(atlas.ensureGlyphs(manyGlyphs), "a large Unicode set must rasterize and grow the atlas")) {
        return false;
    }
    if (!requireTrue(atlas.atlasWidth() > widthBeforeGrowth || atlas.atlasHeight() > heightBeforeGrowth,
                     "the large Unicode set must exceed initial atlas capacity")) {
        return false;
    }

    const GlyphInfo* seedAfterGrowth = atlas.findGlyph(0x4e2du);
    if (!requireTrue(seedAfterGrowth != nullptr && seedAfterGrowth->atlasX == seedBeforeGrowth.atlasX &&
                         seedAfterGrowth->atlasY == seedBeforeGrowth.atlasY &&
                         seedAfterGrowth->bitmapWidth == seedBeforeGrowth.bitmapWidth &&
                         seedAfterGrowth->bitmapHeight == seedBeforeGrowth.bitmapHeight,
                     "atlas growth must preserve the seed glyph pixel rectangle")) {
        return false;
    }
    const float expectedMinX = static_cast<float>(seedAfterGrowth->atlasX) / static_cast<float>(atlas.atlasWidth());
    const float expectedMinY = static_cast<float>(seedAfterGrowth->atlasY) / static_cast<float>(atlas.atlasHeight());
    const float expectedMaxX = static_cast<float>(seedAfterGrowth->atlasX + seedAfterGrowth->bitmapWidth) /
                               static_cast<float>(atlas.atlasWidth());
    const float expectedMaxY = static_cast<float>(seedAfterGrowth->atlasY + seedAfterGrowth->bitmapHeight) /
                               static_cast<float>(atlas.atlasHeight());
    if (!requireTrue(nearlyEqual(seedAfterGrowth->uvMinX, expectedMinX) &&
                         nearlyEqual(seedAfterGrowth->uvMinY, expectedMinY) &&
                         nearlyEqual(seedAfterGrowth->uvMaxX, expectedMaxX) &&
                         nearlyEqual(seedAfterGrowth->uvMaxY, expectedMaxY),
                     "atlas growth must recompute existing glyph UVs using the new dimensions")) {
        return false;
    }

    atlas.shutdown();
    return true;
}

bool testAtlasUploadIsExplicitAndIdempotent() {
    RecordingDevice device;
    GlyphAtlas atlas;
    if (!requireTrue(atlas.init(device, DEFAULT_FONT_PATH, 48) && atlas.ensureGlyphs(u8"显式上传"),
                     "glyphs must be collected before explicit upload")) {
        return false;
    }
    if (!requireTrue(!atlas.textureHandle().isValid(), "CPU glyph collection must not create a GPU texture")) {
        return false;
    }
    const uint64_t revisionBeforeUpload = atlas.revision();
    if (!requireTrue(atlas.prepareUpload(device.commandList), "explicit atlas preparation must succeed")) {
        return false;
    }
    if (!requireTrue(atlas.textureHandle().isValid() && device.textureDescs.size() == 1u,
                     "explicit preparation must create exactly one atlas texture")) {
        return false;
    }
    const RhiTextureDesc& textureDesc = device.textureDescs.front();
    const RhiTextureUsageFlags expectedUsage =
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    if (!requireTrue(textureDesc.format == RhiTextureFormat::R8Unorm &&
                         textureDesc.width == static_cast<uint32_t>(atlas.atlasWidth()) &&
                         textureDesc.height == static_cast<uint32_t>(atlas.atlasHeight()) &&
                         textureDesc.usage == expectedUsage,
                     "atlas texture must expose sampled and transfer-destination usage")) {
        return false;
    }
    if (!requireTrue(device.commandList.textureBarriers.size() == 2u &&
                         device.commandList.textureBarriers[0].oldState == RhiResourceState::Undefined &&
                         device.commandList.textureBarriers[0].newState == RhiResourceState::TransferDst &&
                         device.commandList.textureBarriers[1].oldState == RhiResourceState::TransferDst &&
                         device.commandList.textureBarriers[1].newState == RhiResourceState::ShaderRead,
                     "atlas upload must record explicit transfer state transitions")) {
        return false;
    }
    if (!requireTrue(
            device.commandList.bufferTextureCopies.size() == 1u &&
                device.commandList.bufferTextureCopies.front().width == static_cast<uint32_t>(atlas.atlasWidth()) &&
                device.commandList.bufferTextureCopies.front().height == static_cast<uint32_t>(atlas.atlasHeight()),
            "atlas upload must copy the complete CPU atlas into the RHI texture")) {
        return false;
    }
    if (!requireTrue(atlas.revision() == revisionBeforeUpload, "GPU upload must not mutate the CPU glyph revision")) {
        return false;
    }

    const size_t eventCountAfterUpload = device.commandList.events.size();
    if (!requireTrue(atlas.prepareUpload(device.commandList) &&
                         device.commandList.events.size() == eventCountAfterUpload && device.textureDescs.size() == 1u,
                     "preparing a clean atlas must record no resource commands")) {
        return false;
    }

    atlas.shutdown();
    return true;
}

bool sameRect(const RhiRect2D& left, const RhiRect2D& right) {
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

bool testTextRendererCollectPrepareRecordSequence() {
    RecordingDevice device;
    TextRenderer renderer;
    if (!requireTrue(renderer.init(device, DEFAULT_FONT_PATH), "text renderer must initialize through RHI resources")) {
        return false;
    }
    if (!requireTrue(device.shaderStages.size() == 2u && device.graphicsPipelineDescs.size() == 1u &&
                         device.graphicsPipelineDescs.front().raster.scissorEnabled,
                     "text renderer must create a scissor-enabled RHI pipeline")) {
        return false;
    }

    constexpr float kScreenWidth = 800.0f;
    constexpr float kScreenHeight = 600.0f;
    constexpr std::array<float, 4> kFirstColor = {1.0f, 0.5f, 0.25f, 1.0f};
    constexpr std::array<float, 4> kSecondColor = {0.25f, 0.5f, 1.0f, 0.75f};
    constexpr RhiRect2D kExplicitScissor = {11, 13, 101u, 37u};

    UIRenderContext firstContext;
    firstContext.phase = UIRenderPhase::CollectText;
    firstContext.screenWidth = static_cast<int>(kScreenWidth);
    firstContext.screenHeight = static_cast<int>(kScreenHeight);
    firstContext.scaleConfig.effectiveScale = 1.0f;
    firstContext.hasScissor = true;
    firstContext.scissor = kExplicitScissor;

    UIRenderContext secondContext = firstContext;
    secondContext.hasScissor = false;
    secondContext.scissor = {};

    renderer.beginFrameCollection(kScreenWidth, kScreenHeight);
    renderer.draw(firstContext, u8"中", 20.0f, 30.0f, 1.0f, kFirstColor);
    renderer.draw(secondContext, "AB", 80.0f, 90.0f, 1.5f, kSecondColor);
    if (!requireTrue(renderer.atlas().revision() == 0u, "collection must queue text without rasterizing glyphs")) {
        return false;
    }
    if (!requireTrue(renderer.prepareFrame(device.commandList),
                     "frame preparation must rasterize and upload all queued text")) {
        return false;
    }
    if (!requireTrue(renderer.atlas().revision() == 3u,
                     "frame preparation must rasterize each unique requested codepoint once")) {
        return false;
    }
    if (!requireTrue(device.commandList.updatedByteCounts.size() == 1u &&
                         device.commandList.updatedByteCounts.front() == static_cast<size_t>(18u * 8u * sizeof(float)),
                     "frame preparation must upload one immutable vertex stream for all requests")) {
        return false;
    }

    const uint64_t preparedRevision = renderer.atlas().revision();
    device.commandList.clearDrawRecording();
    firstContext.phase = UIRenderPhase::Record;
    firstContext.commandList = &device.commandList;
    secondContext.phase = UIRenderPhase::Record;
    secondContext.commandList = &device.commandList;
    renderer.beginFrameRecording();
    renderer.draw(firstContext, u8"中", 20.0f, 30.0f, 1.0f, kFirstColor);
    renderer.draw(secondContext, "AB", 80.0f, 90.0f, 1.5f, kSecondColor);
    if (!requireTrue(renderer.endFrameRecording(), "recording must consume every collected request in order")) {
        return false;
    }
    if (!requireTrue(renderer.atlas().revision() == preparedRevision,
                     "recording prepared requests must never rasterize or mutate the atlas")) {
        return false;
    }

    constexpr RhiRect2D kFullScreenScissor = {0, 0, 800u, 600u};
    if (!requireTrue(device.commandList.scissors.size() == 2u &&
                         sameRect(device.commandList.scissors[0], kExplicitScissor) &&
                         sameRect(device.commandList.scissors[1], kFullScreenScissor),
                     "every text request must record its explicit or full-screen scissor")) {
        return false;
    }
    if (!requireTrue(device.commandList.drawVertexCounts == std::vector<uint32_t>({6u, 12u}),
                     "recording must preserve request order and vertex ranges")) {
        return false;
    }

    constexpr std::array<RecordingCommandList::EventType, 6> kRequestSequence = {
        RecordingCommandList::EventType::SetScissor,    RecordingCommandList::EventType::SetGraphicsPipeline,
        RecordingCommandList::EventType::SetBindGroup,  RecordingCommandList::EventType::SetVertexBuffer,
        RecordingCommandList::EventType::PushConstants, RecordingCommandList::EventType::Draw};
    if (!requireTrue(device.commandList.events.size() == kRequestSequence.size() * 2u,
                     "each prepared request must emit one complete draw command sequence")) {
        return false;
    }
    for (size_t requestIndex = 0u; requestIndex < 2u; ++requestIndex) {
        for (size_t eventIndex = 0u; eventIndex < kRequestSequence.size(); ++eventIndex) {
            const size_t index = requestIndex * kRequestSequence.size() + eventIndex;
            if (!requireTrue(device.commandList.events[index].type == kRequestSequence[eventIndex],
                             "text request command order must start with scissor and end with draw")) {
                return false;
            }
        }
    }

    renderer.shutdown();
    return true;
}

} // namespace

int main() {
    if (!testUnicodeRasterizationAndStrictUtf8() || !testAtlasGrowthRefreshesExistingGlyphUvs() ||
        !testAtlasUploadIsExplicitAndIdempotent() || !testTextRendererCollectPrepareRecordSequence()) {
        return EXIT_FAILURE;
    }

    std::cout << "[text_renderer_test] PASS\n";
    return EXIT_SUCCESS;
}
