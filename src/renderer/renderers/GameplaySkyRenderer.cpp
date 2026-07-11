#include "GameplaySkyRenderer.h"

#include "../gl/GlStateGuard.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <vector>

#include <glad/glad.h>

#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "engine/camera/Camera.h"
#include "../../Paths.h"
#include "../../resource/ResourceMgr.h"
#include "stb/stb_image.h"
#include "../../world/DayNightSystem.h"

#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kHaloSize = 3.25f;
constexpr float kCloudHeight = 128.0f;
constexpr int kCloudMaskSample = 8;
constexpr float kCloudCellSize = 6.0f;
constexpr float kCloudThickness = 4.0f;
constexpr float kCloudDriftSpeed = 0.35f;
constexpr unsigned char kCloudAlphaThreshold = 120;
constexpr int kCloudSolidNumerator = 3;
constexpr int kCloudSolidDenominator = 5;

float smoothstep(float edge0, float edge1, float x) {
    const float denom = std::max(edge1 - edge0, 0.0001f);
    const float t = std::clamp((x - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    if (len <= 0.0001f) {
        return fallback;
    }
    return v / len;
}

struct HaloVertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec4 color;
};

struct CloudVertex {
    glm::vec3 position;
    float shade;
};

bool isCloudPixelSolid(const std::vector<unsigned char>& pixels, const int width, const int height, const int x, const int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    const size_t idx = static_cast<size_t>(y * width + x) * 4;
    return idx + 3 < pixels.size() && pixels[idx + 3] > kCloudAlphaThreshold;
}

bool isMaskSolid(const std::vector<uint8_t>& mask, const int width, const int height, const int x, const int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return mask[static_cast<size_t>(y * width + x)] != 0;
}

bool isExteriorEmpty(const std::vector<uint8_t>& exteriorEmpty, const int width, const int height, const int x, const int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return true;
    }
    return exteriorEmpty[static_cast<size_t>(y * width + x)] != 0;
}

void appendCloudQuad(std::vector<CloudVertex>& vertices,
                     const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     const glm::vec3& d,
                     const float shade) {
    vertices.push_back({a, shade});
    vertices.push_back({b, shade});
    vertices.push_back({c, shade});
    vertices.push_back({a, shade});
    vertices.push_back({c, shade});
    vertices.push_back({d, shade});
}

void appendCloudBoxFaceRect(std::vector<CloudVertex>& vertices,
                            const int x,
                            const int y,
                            const int width,
                            const int height,
                            const float originX,
                            const float originZ,
                            const float cellWorldSize,
                            const float y0,
                            const float y1,
                            const int face,
                            const float shade) {
    const float x0 = originX + static_cast<float>(x) * cellWorldSize;
    const float x1 = originX + static_cast<float>(x + width) * cellWorldSize;
    const float z0 = originZ + static_cast<float>(y) * cellWorldSize;
    const float z1 = originZ + static_cast<float>(y + height) * cellWorldSize;

    switch (face) {
        case 0:
            appendCloudQuad(vertices, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, shade);
            break;
        case 1:
            appendCloudQuad(vertices, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, shade);
            break;
        case 2:
            appendCloudQuad(vertices, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}, shade);
            break;
        case 3:
            appendCloudQuad(vertices, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, shade);
            break;
        case 4:
            appendCloudQuad(vertices, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, shade);
            break;
        case 5:
            appendCloudQuad(vertices, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {x0, y0, z1}, shade);
            break;
        default:
            break;
    }
}

void appendGreedySurface(std::vector<CloudVertex>& vertices,
                         const std::vector<uint8_t>& surface,
                         const int width,
                         const int height,
                         const float originX,
                         const float originZ,
                         const float cellWorldSize,
                         const float y0,
                         const float y1,
                         const int face,
                         const float shade) {
    std::vector<uint8_t> used(surface.size(), 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y * width + x);
            if (surface[idx] == 0 || used[idx] != 0) {
                continue;
            }

            int rectWidth = 1;
            while (x + rectWidth < width) {
                const size_t nextIdx = static_cast<size_t>(y * width + x + rectWidth);
                if (surface[nextIdx] == 0 || used[nextIdx] != 0) {
                    break;
                }
                ++rectWidth;
            }

            int rectHeight = 1;
            bool canExtend = true;
            while (y + rectHeight < height && canExtend) {
                for (int dx = 0; dx < rectWidth; ++dx) {
                    const size_t nextIdx = static_cast<size_t>((y + rectHeight) * width + x + dx);
                    if (surface[nextIdx] == 0 || used[nextIdx] != 0) {
                        canExtend = false;
                        break;
                    }
                }
                if (canExtend) {
                    ++rectHeight;
                }
            }

            for (int dy = 0; dy < rectHeight; ++dy) {
                for (int dx = 0; dx < rectWidth; ++dx) {
                    used[static_cast<size_t>((y + dy) * width + x + dx)] = 1;
                }
            }

            appendCloudBoxFaceRect(vertices, x, y, rectWidth, rectHeight,
                                   originX, originZ, cellWorldSize, y0, y1, face, shade);
        }
    }
}
}

void GameplaySkyRenderer::init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice) {
    m_resourceMgr = &resourceMgr;
    m_rhiDevice = &rhiDevice;
    RhiBufferDesc captureBufferDesc;
    captureBufferDesc.debugName = "GameplaySky.Capture.UniformBuffer";
    captureBufferDesc.size = sizeof(CaptureUniforms);
    captureBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    captureBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_captureUniformBuffer = rhiDevice.createBuffer(captureBufferDesc, nullptr, 0u);
    RhiSamplerDesc captureSamplerDesc;
    captureSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    captureSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    captureSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_captureSampler = rhiDevice.createSampler(captureSamplerDesc);
    RhiBindGroupLayoutDesc captureLayoutDesc;
    captureLayoutDesc.debugName = "GameplaySky.Capture.BindGroupLayout";
    captureLayoutDesc.entries = {
        {0u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Fragment), 1u},
        {1u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u},
        {2u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u}
    };
    m_captureBindGroupLayout = rhiDevice.createBindGroupLayout(captureLayoutDesc);
    RhiPipelineLayoutDesc capturePipelineLayoutDesc;
    capturePipelineLayoutDesc.debugName = "GameplaySky.Capture.PipelineLayout";
    capturePipelineLayoutDesc.bindGroupLayouts.push_back(m_captureBindGroupLayout);
    m_capturePipelineLayout = rhiDevice.createPipelineLayout(capturePipelineLayoutDesc);
    const auto captureVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_capture_rhi.vert");
    const auto captureFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_capture_rhi.frag");
    if (!captureVertexSource || !captureFragmentSource) std::abort();
    RhiShaderDesc captureShaderDesc;
    captureShaderDesc.debugName = "GameplaySky.Capture.Vertex";
    captureShaderDesc.stage = RhiShaderStage::Vertex;
    captureShaderDesc.source = captureVertexSource->c_str();
    captureShaderDesc.sourceSize = captureVertexSource->size();
    m_captureVertexShader = rhiDevice.createShader(captureShaderDesc);
    captureShaderDesc.debugName = "GameplaySky.Capture.Fragment";
    captureShaderDesc.stage = RhiShaderStage::Fragment;
    captureShaderDesc.source = captureFragmentSource->c_str();
    captureShaderDesc.sourceSize = captureFragmentSource->size();
    m_captureFragmentShader = rhiDevice.createShader(captureShaderDesc);
    RhiGraphicsPipelineDesc capturePipelineDesc;
    capturePipelineDesc.debugName = "GameplaySky.Capture.Pipeline";
    capturePipelineDesc.vertexShader = m_captureVertexShader;
    capturePipelineDesc.fragmentShader = m_captureFragmentShader;
    capturePipelineDesc.layout = m_capturePipelineLayout;
    capturePipelineDesc.depthStencil.depthTestEnabled = false;
    capturePipelineDesc.depthStencil.depthWriteEnabled = false;
    capturePipelineDesc.raster.cullMode = RhiCullMode::None;
    capturePipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    capturePipelineDesc.blend.attachments.resize(1u);
    m_capturePipeline = rhiDevice.createGraphicsPipeline(capturePipelineDesc);
    const auto visibleVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_visible_rhi.vert");
    const auto visibleFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_visible_rhi.frag");
    if (!visibleVertexSource || !visibleFragmentSource) std::abort();
    RhiShaderDesc visibleShaderDesc;
    visibleShaderDesc.debugName = "GameplaySky.Visible.Vertex";
    visibleShaderDesc.stage = RhiShaderStage::Vertex;
    visibleShaderDesc.source = visibleVertexSource->c_str();
    visibleShaderDesc.sourceSize = visibleVertexSource->size();
    m_visibleVertexShader = rhiDevice.createShader(visibleShaderDesc);
    visibleShaderDesc.debugName = "GameplaySky.Visible.Fragment";
    visibleShaderDesc.stage = RhiShaderStage::Fragment;
    visibleShaderDesc.source = visibleFragmentSource->c_str();
    visibleShaderDesc.sourceSize = visibleFragmentSource->size();
    m_visibleFragmentShader = rhiDevice.createShader(visibleShaderDesc);
    RhiPipelineLayoutDesc visibleLayoutDesc;
    visibleLayoutDesc.debugName = "GameplaySky.Visible.PipelineLayout";
    visibleLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4) * 6u;
    visibleLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                           rhiFlag(RhiShaderStage::Fragment);
    m_visiblePipelineLayout = rhiDevice.createPipelineLayout(visibleLayoutDesc);
    RhiGraphicsPipelineDesc visiblePipelineDesc;
    visiblePipelineDesc.debugName = "GameplaySky.Visible.Pipeline";
    visiblePipelineDesc.vertexShader = m_visibleVertexShader;
    visiblePipelineDesc.fragmentShader = m_visibleFragmentShader;
    visiblePipelineDesc.layout = m_visiblePipelineLayout;
    visiblePipelineDesc.raster.cullMode = RhiCullMode::None;
    visiblePipelineDesc.depthStencil.depthTestEnabled = false;
    visiblePipelineDesc.depthStencil.depthWriteEnabled = false;
    visiblePipelineDesc.colorFormats = {rhiDevice.swapchainColorFormat()};
    visiblePipelineDesc.depthFormat = rhiDevice.swapchainDepthStencilFormat();
    visiblePipelineDesc.blend.attachments.resize(1u);
    m_visiblePipeline = rhiDevice.createGraphicsPipeline(visiblePipelineDesc);
    const auto haloVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_halo_rhi.vert");
    const auto haloFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_halo_rhi.frag");
    if (!haloVertexSource || !haloFragmentSource) std::abort();
    RhiShaderDesc haloShaderDesc;
    haloShaderDesc.debugName = "GameplaySky.Halo.Vertex";
    haloShaderDesc.stage = RhiShaderStage::Vertex;
    haloShaderDesc.source = haloVertexSource->c_str();
    haloShaderDesc.sourceSize = haloVertexSource->size();
    m_haloVertexShader = rhiDevice.createShader(haloShaderDesc);
    haloShaderDesc.debugName = "GameplaySky.Halo.Fragment";
    haloShaderDesc.stage = RhiShaderStage::Fragment;
    haloShaderDesc.source = haloFragmentSource->c_str();
    haloShaderDesc.sourceSize = haloFragmentSource->size();
    m_haloFragmentShader = rhiDevice.createShader(haloShaderDesc);
    RhiPipelineLayoutDesc haloLayoutDesc;
    haloLayoutDesc.debugName = "GameplaySky.Halo.PipelineLayout";
    haloLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    haloLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                        rhiFlag(RhiShaderStage::Fragment);
    m_haloPipelineLayout = rhiDevice.createPipelineLayout(haloLayoutDesc);
    RhiGraphicsPipelineDesc haloPipelineDesc;
    haloPipelineDesc.debugName = "GameplaySky.Halo.Pipeline";
    haloPipelineDesc.vertexShader = m_haloVertexShader;
    haloPipelineDesc.fragmentShader = m_haloFragmentShader;
    haloPipelineDesc.layout = m_haloPipelineLayout;
    haloPipelineDesc.vertexInput.bindings = {
        {0u, sizeof(HaloVertex), RhiVertexInputRate::Vertex}
    };
    haloPipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(HaloVertex, position)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(HaloVertex, uv)},
        {2u, 0u, RhiVertexFormat::Float4, offsetof(HaloVertex, color)}
    };
    haloPipelineDesc.raster.cullMode = RhiCullMode::None;
    haloPipelineDesc.depthStencil.depthTestEnabled = false;
    haloPipelineDesc.depthStencil.depthWriteEnabled = false;
    haloPipelineDesc.colorFormats = {rhiDevice.swapchainColorFormat()};
    haloPipelineDesc.depthFormat = rhiDevice.swapchainDepthStencilFormat();
    RhiBlendAttachmentState haloBlend;
    haloBlend.blendEnabled = true;
    haloBlend.srcColor = RhiBlendFactor::SrcAlpha;
    haloBlend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    haloBlend.srcAlpha = RhiBlendFactor::One;
    haloBlend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    haloPipelineDesc.blend.attachments.push_back(haloBlend);
    m_haloPipeline = rhiDevice.createGraphicsPipeline(haloPipelineDesc);
    const auto cloudVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_cloud_rhi.vert");
    const auto cloudFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/gameplay_sky_cloud_rhi.frag");
    if (!cloudVertexSource || !cloudFragmentSource) std::abort();
    RhiShaderDesc cloudShaderDesc;
    cloudShaderDesc.debugName = "GameplaySky.Cloud.Vertex";
    cloudShaderDesc.stage = RhiShaderStage::Vertex;
    cloudShaderDesc.source = cloudVertexSource->c_str();
    cloudShaderDesc.sourceSize = cloudVertexSource->size();
    m_cloudVertexShader = rhiDevice.createShader(cloudShaderDesc);
    cloudShaderDesc.debugName = "GameplaySky.Cloud.Fragment";
    cloudShaderDesc.stage = RhiShaderStage::Fragment;
    cloudShaderDesc.source = cloudFragmentSource->c_str();
    cloudShaderDesc.sourceSize = cloudFragmentSource->size();
    m_cloudFragmentShader = rhiDevice.createShader(cloudShaderDesc);
    RhiPipelineLayoutDesc cloudLayoutDesc;
    cloudLayoutDesc.debugName = "GameplaySky.Cloud.PipelineLayout";
    cloudLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    cloudLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                         rhiFlag(RhiShaderStage::Fragment);
    m_cloudPipelineLayout = rhiDevice.createPipelineLayout(cloudLayoutDesc);
    RhiGraphicsPipelineDesc cloudPipelineDesc;
    cloudPipelineDesc.debugName = "GameplaySky.Cloud.Pipeline";
    cloudPipelineDesc.vertexShader = m_cloudVertexShader;
    cloudPipelineDesc.fragmentShader = m_cloudFragmentShader;
    cloudPipelineDesc.layout = m_cloudPipelineLayout;
    cloudPipelineDesc.vertexInput.bindings = {
        {0u, sizeof(CloudVertex), RhiVertexInputRate::Vertex}
    };
    cloudPipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(CloudVertex, position)},
        {1u, 0u, RhiVertexFormat::Float, offsetof(CloudVertex, shade)}
    };
    cloudPipelineDesc.depthStencil.depthTestEnabled = true;
    cloudPipelineDesc.depthStencil.depthWriteEnabled = true;
    cloudPipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    cloudPipelineDesc.colorFormats = {rhiDevice.swapchainColorFormat()};
    cloudPipelineDesc.depthFormat = rhiDevice.swapchainDepthStencilFormat();
    cloudPipelineDesc.blend.attachments.resize(1u);
    m_cloudPipeline = rhiDevice.createGraphicsPipeline(cloudPipelineDesc);
    if (!m_captureUniformBuffer.isValid() || !m_captureSampler.isValid() ||
        !m_captureBindGroupLayout.isValid() || !m_capturePipelineLayout.isValid() ||
        !m_captureVertexShader.isValid() || !m_captureFragmentShader.isValid() ||
        !m_capturePipeline.isValid() || !m_visibleVertexShader.isValid() ||
        !m_visibleFragmentShader.isValid() || !m_visiblePipelineLayout.isValid() ||
        !m_visiblePipeline.isValid() || !m_haloVertexShader.isValid() ||
        !m_haloFragmentShader.isValid() || !m_haloPipelineLayout.isValid() ||
        !m_haloPipeline.isValid() || !m_cloudVertexShader.isValid() ||
        !m_cloudFragmentShader.isValid() || !m_cloudPipelineLayout.isValid() ||
        !m_cloudPipeline.isValid()) std::abort();
    m_deferredShader = resourceMgr.getShader("gameplay_sky");
    m_shader = m_deferredShader;
    initMeshes();
    initCloudMesh();
    ensureDummySkyCaptureTexture();
}

void GameplaySkyRenderer::shutdown() {
    destroyMeshes();
    if (m_captureBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_captureBindGroup);
    if (m_captureNoiseView.isValid()) m_rhiDevice->destroyTextureView(m_captureNoiseView);
    if (m_capturePipeline.isValid()) m_rhiDevice->destroyPipeline(m_capturePipeline);
    if (m_visiblePipeline.isValid()) m_rhiDevice->destroyPipeline(m_visiblePipeline);
    if (m_haloPipeline.isValid()) m_rhiDevice->destroyPipeline(m_haloPipeline);
    if (m_cloudPipeline.isValid()) m_rhiDevice->destroyPipeline(m_cloudPipeline);
    if (m_cloudPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_cloudPipelineLayout);
    if (m_cloudFragmentShader.isValid()) m_rhiDevice->destroyShader(m_cloudFragmentShader);
    if (m_cloudVertexShader.isValid()) m_rhiDevice->destroyShader(m_cloudVertexShader);
    if (m_haloPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_haloPipelineLayout);
    if (m_haloFragmentShader.isValid()) m_rhiDevice->destroyShader(m_haloFragmentShader);
    if (m_haloVertexShader.isValid()) m_rhiDevice->destroyShader(m_haloVertexShader);
    if (m_visiblePipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_visiblePipelineLayout);
    if (m_visibleFragmentShader.isValid()) m_rhiDevice->destroyShader(m_visibleFragmentShader);
    if (m_visibleVertexShader.isValid()) m_rhiDevice->destroyShader(m_visibleVertexShader);
    if (m_captureFragmentShader.isValid()) m_rhiDevice->destroyShader(m_captureFragmentShader);
    if (m_captureVertexShader.isValid()) m_rhiDevice->destroyShader(m_captureVertexShader);
    if (m_captureUniformBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_captureUniformBuffer);
        m_captureUniformBuffer = {};
    }
    if (m_capturePipelineLayout.isValid()) {
        m_rhiDevice->destroyPipelineLayout(m_capturePipelineLayout);
    }
    if (m_captureBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_captureBindGroupLayout);
    if (m_captureSampler.isValid()) m_rhiDevice->destroySampler(m_captureSampler);
    m_captureBindGroup = {};
    m_capturePipeline = {};
    m_visiblePipeline = {};
    m_haloPipeline = {};
    m_cloudPipeline = {};
    m_cloudPipelineLayout = {};
    m_cloudFragmentShader = {};
    m_cloudVertexShader = {};
    m_haloPipelineLayout = {};
    m_haloFragmentShader = {};
    m_haloVertexShader = {};
    m_visiblePipelineLayout = {};
    m_visibleFragmentShader = {};
    m_visibleVertexShader = {};
    m_captureFragmentShader = {};
    m_captureVertexShader = {};
    m_capturePipelineLayout = {};
    m_captureBindGroupLayout = {};
    m_captureSampler = {};
    m_captureAtmosphereLutView = {};
    m_captureNoiseView = {};
    m_captureNoiseTexture = {};
    if (m_dummySkyCaptureTexture != 0) {
        glDeleteTextures(1, &m_dummySkyCaptureTexture);
        m_dummySkyCaptureTexture = 0;
    }
    m_shader = nullptr;
    m_deferredShader = nullptr;
    m_resourceMgr = nullptr;
    m_rhiDevice = nullptr;
}

void GameplaySkyRenderer::synchronizeCaptureResources(
    const RhiTextureViewHandle atmosphereLutView,
    const RhiTextureHandle noiseTexture) {
    if (!atmosphereLutView.isValid() || !noiseTexture.isValid()) {
        std::abort();
    }
    const bool atmosphereUnchanged =
        m_captureAtmosphereLutView.index == atmosphereLutView.index &&
        m_captureAtmosphereLutView.generation == atmosphereLutView.generation;
    const bool noiseUnchanged =
        m_captureNoiseTexture.index == noiseTexture.index &&
        m_captureNoiseTexture.generation == noiseTexture.generation;
    if (atmosphereUnchanged && noiseUnchanged && m_captureBindGroup.isValid()) {
        return;
    }
    if (m_captureBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_captureBindGroup);
        m_captureBindGroup = {};
    }
    if (!noiseUnchanged && m_captureNoiseView.isValid()) {
        m_rhiDevice->destroyTextureView(m_captureNoiseView);
        m_captureNoiseView = {};
    }
    if (!noiseUnchanged) {
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = noiseTexture;
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        m_captureNoiseView = m_rhiDevice->createTextureView(viewDesc);
    }
    if (!m_captureNoiseView.isValid()) {
        std::abort();
    }
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_captureBindGroupLayout;
    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 0u;
    uniformEntry.resource.buffer = {m_captureUniformBuffer, 0u, sizeof(CaptureUniforms)};
    bindGroupDesc.entries.push_back(uniformEntry);
    RhiBindGroupEntry atmosphereEntry;
    atmosphereEntry.binding = 1u;
    atmosphereEntry.resource.combinedTextureSampler = {atmosphereLutView, m_captureSampler};
    bindGroupDesc.entries.push_back(atmosphereEntry);
    RhiBindGroupEntry noiseEntry;
    noiseEntry.binding = 2u;
    noiseEntry.resource.combinedTextureSampler = {m_captureNoiseView, m_captureSampler};
    bindGroupDesc.entries.push_back(noiseEntry);
    m_captureBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_captureBindGroup.isValid()) std::abort();
    m_captureAtmosphereLutView = atmosphereLutView;
    m_captureNoiseTexture = noiseTexture;
}

void GameplaySkyRenderer::setForwardMode(bool forward) {
    if (m_resourceMgr == nullptr) return;
    if (forward) {
        Shader* fwd = m_resourceMgr->getShader("gameplay_sky_forward");
        m_shader = fwd ? fwd : m_deferredShader;
    } else {
        m_shader = m_deferredShader;
    }
}

void GameplaySkyRenderer::render(const Camera& camera, const float aspect,
                                 const DayNightSystem& dayNight,
                                 RhiCommandList& commandList) {
    m_lastColors = computeSkyColors(dayNight);
    struct PushConstants {
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 skyTopHaze;
        glm::vec4 skyHorizonGlare;
        glm::vec4 sunDirectionVisibility;
        glm::vec4 moonDirectionVisibility;
        glm::vec4 sunScatterNight;
        glm::vec4 moonLightPhase;
    };
    const PushConstants constants{
        glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 100.0f),
        buildSkyView(camera),
        {m_lastColors.top, m_lastColors.horizonHaze},
        {m_lastColors.horizon, m_lastColors.sunGlare},
        {m_lastColors.sunDirection, m_lastColors.sunVisibility},
        {m_lastColors.moonDirection, m_lastColors.moonVisibility},
        {m_lastColors.sunScatter, m_lastColors.nightFactor},
        {m_lastColors.moonLightColor, m_lastColors.moonPhaseAngle}
    };
    commandList.setGraphicsPipeline(m_visiblePipeline);
    commandList.pushConstants(&constants, sizeof(constants),
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    renderHalo(camera, aspect, dayNight, m_lastColors, commandList);
    renderClouds(camera, aspect, dayNight, m_lastColors, commandList);
}

void GameplaySkyRenderer::renderCloudySkyCapture(const SkyColors& colors,
                                                  RhiCommandList& commandList,
                                                  const RhiTextureViewHandle targetView,
                                                  const int skyCaptureWidth,
                                                  const int skyCaptureHeight,
                                                  const RhiTextureViewHandle atmosphereLutView,
                                                  const RhiTextureHandle noiseTexture,
                                                  const SkyIlluminanceData& illuminance,
                                                  const CloudySkyCaptureParams& params) {
    if (!targetView.isValid() ||
        !atmosphereLutView.isValid() || skyCaptureWidth <= 0 || skyCaptureHeight <= 258) {
        return;
    }
    synchronizeCaptureResources(atmosphereLutView, noiseTexture);

    const CaptureUniforms captureUniforms{
        {colors.top, colors.horizonHaze},
        {colors.horizon, colors.sunGlare},
        {colors.sunDirection, colors.sunVisibility},
        {colors.moonDirection, colors.moonVisibility},
        {colors.sunScatter, colors.nightFactor},
        {colors.moonLightColor, params.moonPhaseFlux},
        {illuminance.directIlluminance, params.cameraAltitude},
        {illuminance.skyIlluminance, params.shaderTime},
        {illuminance.sunIlluminance, params.cloudTimeScale},
        {illuminance.moonIlluminance, params.cloudCoverage},
        {illuminance.cloudDynamicWeather, params.cloudDensity},
        {params.cloudHeight, params.cloudThickness,
         params.planarCloudCoverage, params.planarCloudDensity},
        {params.planarCloudAltitude, params.precipitation, 0.0f, 0.0f},
        {params.weatherWetness, params.weatherStorm,
         params.skyWetness, params.fogWetness},
        {params.cloudWetness, params.surfaceWetness, params.precipitation, 0.0f},
        {params.cloudCoverage, params.cloudDensity,
         params.cloudTimeScale, params.shaderTime},
        {params.cameraPosition, 0.0f}
    };
    commandList.updateBuffer(m_captureUniformBuffer, 0u,
                             &captureUniforms, sizeof(captureUniforms));

    const renderer::gl::ScopedStateSnapshot stateGuard;

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    // Cloudy sky: rows 258..513 (256 rows). Matches DerivativeMain Deferred0.glsl cloudy sky region.
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SkyCapture.Cloudy";
    renderingInfo.renderArea = {
        0,
        258,
        static_cast<uint32_t>(std::max(1, skyCaptureWidth)),
        256u
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    commandList.beginRendering(renderingInfo);
    commandList.setViewport({0.0f, 258.0f, static_cast<float>(skyCaptureWidth),
                             256.0f, 0.0f, 1.0f});
    commandList.setScissor({0, 258, static_cast<uint32_t>(skyCaptureWidth), 256u});
    commandList.setGraphicsPipeline(m_capturePipeline);
    commandList.setBindGroup(0u, m_captureBindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    // GL state restored by ScopedStateSnapshot destructor
}

GameplaySkyRenderer::SkyColors GameplaySkyRenderer::computeSkyColors(const DayNightSystem& dayNight) const {
    const float skyIntensity = dayNight.getSkyIntensity();
    const float progress = dayNight.getDayProgress01();
    const glm::vec3 sunDirection = directionFromAngle(dayNight.getCelestialAngleRadians());
    const glm::vec3 moonDirection = directionFromAngle(std::fmod(dayNight.getCelestialAngleRadians() + kPi, kTwoPi));
    const int moonPhaseIndex = std::clamp(dayNight.getMoonPhaseIndex(), 0, 7);
    const float moonPhaseAngle = (static_cast<float>(moonPhaseIndex) - 4.0f) * (kTwoPi / 8.0f);
    const float sunHeight = std::clamp(sunDirection.y, -0.25f, 1.0f);
    const float sunriseWindow = 1.0f - std::abs(progress - 0.0f) / 0.07f;
    const float sunriseWrapWindow = 1.0f - std::abs(progress - 1.0f) / 0.07f;
    const float sunsetWindow = 1.0f - std::abs(progress - 0.5f) / 0.08f;
    const float warmWindow = std::clamp(std::max(std::max(sunriseWindow, sunriseWrapWindow), sunsetWindow), 0.0f, 1.0f);
    const float warm = warmWindow * warmWindow * (3.0f - 2.0f * warmWindow);

    const glm::vec3 dayTop(0.38f, 0.66f, 1.0f);
    const glm::vec3 dayHorizon(0.70f, 0.88f, 1.0f);
    const glm::vec3 nightTop(0.010f, 0.020f, 0.060f);
    const glm::vec3 nightHorizon(0.030f, 0.045f, 0.095f);
    const glm::vec3 duskTop(0.24f, 0.34f, 0.58f);
    const glm::vec3 duskHorizon(1.0f, 0.38f, 0.13f);
    const glm::vec3 goldenScatter(1.0f, 0.52f, 0.16f);
    const glm::vec3 noonScatter(0.62f, 0.80f, 1.0f);
    const glm::vec3 noonSunLight(1.0f, 0.98f, 0.92f);
    const glm::vec3 warmSunLight(1.0f, 0.58f, 0.28f);
    const glm::vec3 nightSunLight(0.36f, 0.44f, 0.72f);
    const glm::vec3 dayAmbient(0.68f, 0.82f, 1.0f);
    const glm::vec3 nightAmbient(0.12f, 0.16f, 0.28f);
    const glm::vec3 warmAmbient(0.92f, 0.54f, 0.30f);
    const glm::vec3 dayShadowTint(0.56f, 0.64f, 0.88f);
    const glm::vec3 nightShadowTint(0.10f, 0.13f, 0.24f);
    const glm::vec3 warmHorizonScatter(1.0f, 0.36f, 0.13f);

    SkyColors colors;
    colors.top = lerp(nightTop, dayTop, skyIntensity);
    colors.horizon = lerp(nightHorizon, dayHorizon, skyIntensity);
    colors.top = lerp(colors.top, duskTop, warm * 0.65f);
    colors.horizon = lerp(colors.horizon, duskHorizon, warm);
    colors.fog = lerp(colors.horizon, glm::vec3(0.80f, 0.90f, 1.0f), skyIntensity * (1.0f - warm) * 0.22f);
    colors.sunDirection = sunDirection;
    colors.moonDirection = moonDirection;
    colors.sunScatter = lerp(noonScatter, goldenScatter, warm);
    colors.sunLightColor = lerp(nightSunLight, noonSunLight, skyIntensity);
    colors.sunLightColor = lerp(colors.sunLightColor, warmSunLight, warm * 0.58f);
    colors.moonLightColor = glm::vec3(0.42f, 0.52f, 0.95f);
    colors.skyAmbientColor = lerp(nightAmbient, dayAmbient, skyIntensity);
    colors.skyAmbientColor = lerp(colors.skyAmbientColor, warmAmbient, warm * 0.32f);
    colors.shadowTintColor = lerp(nightShadowTint, dayShadowTint, skyIntensity);
    colors.shadowTintColor = lerp(colors.shadowTintColor, glm::vec3(0.34f, 0.28f, 0.44f), warm * 0.35f);
    colors.horizonScatterColor = lerp(colors.horizon, warmHorizonScatter, warm * 0.85f);
    colors.horizonHaze = std::clamp(0.28f + 0.44f * warm + 0.16f * (1.0f - skyIntensity), 0.0f, 0.85f);
    colors.sunGlare = std::clamp(0.18f * skyIntensity + 0.78f * warm, 0.0f, 1.0f);
    colors.haloStrength = std::clamp(warm + smoothstep(0.05f, 0.35f, sunHeight) * skyIntensity * 0.28f, 0.0f, 1.0f);
    colors.halo = glm::vec4(colors.sunScatter, 0.30f * skyIntensity + 0.68f * warm);
    colors.sunVisibility = smoothstep(-0.08f, 0.18f, sunDirection.y) * skyIntensity;
    colors.moonVisibility = smoothstep(-0.08f, 0.18f, moonDirection.y) * (1.0f - skyIntensity);
    colors.moonPhaseAngle = moonPhaseAngle;
    colors.dayFactor = skyIntensity;
    colors.nightFactor = 1.0f - skyIntensity;
    colors.horizonFactor = std::clamp(1.0f - std::abs(sunDirection.y), 0.0f, 1.0f);
    colors.rainFactor = 0.0f;
    colors.wetnessFactor = 0.0f;
    colors.cloudinessFactor = m_cloudMeshInfo.valid ? 0.35f : 0.0f;

    const glm::vec3 cloudDayColor = lerp(colors.horizon, glm::vec3(1.0f), 0.84f);
    const glm::vec3 cloudNightColor(0.14f, 0.16f, 0.25f);
    const glm::vec3 cloudWarmColor(1.0f, 0.58f, 0.24f);
    colors.cloudColor = lerp(cloudNightColor, cloudDayColor, skyIntensity);
    colors.cloudColor = lerp(colors.cloudColor, cloudWarmColor, warm * 0.42f);
    return colors;
}

GameplaySkyRenderer::SkyIlluminanceData GameplaySkyRenderer::computeSkyIlluminance(const SkyColors& colors,
                                                                                   const float weatherWetness,
                                                                                   const float weatherStorm) const {
    SkyIlluminanceData data;

    // Match DerivativeMain's atmosphere-unit contract:
    //   sunIlluminance/moonIlluminance are solar_irradiance * transmittance,
    //   directIlluminance is their sum, and skyIlluminance is a low HDR
    //   hemisphere term. Do not use real-world lux here; cloud/fog/water shaders
    //   multiply these values by large shaderpack constants.
    constexpr glm::vec3 kSolarIrradiance(1.474000f, 1.850400f, 1.911980f);
    const float sunAltitude = std::clamp(colors.sunDirection.y, 0.0f, 1.0f);
    const float moonAltitude = std::clamp(colors.moonDirection.y, 0.0f, 1.0f);
    const float sunTransmittance = colors.sunVisibility * smoothstep(0.0f, 0.18f, sunAltitude);
    // DerivativeMain MoonFlux includes NIGHT_BRIGHTNESS (0.0005) which scales moon
    // contribution to physically correct levels. Without this, moon is ~2000x too bright.
    // The GPU metadata path (mode 5) uses atmGetSunAndSkyIrradiance with uMoonPhaseFlux
    // which already includes NIGHT_BRIGHTNESS. This CPU fallback should match.
    constexpr float kNightBrightness = 0.0005f;
    const float moonTransmittance = colors.moonVisibility * smoothstep(0.0f, 0.18f, moonAltitude) * kNightBrightness;

    data.sunIlluminance = kSolarIrradiance * colors.sunLightColor * sunTransmittance;
    data.moonIlluminance = kSolarIrradiance * colors.moonLightColor * moonTransmittance;
    data.directIlluminance = data.sunIlluminance + data.moonIlluminance;

    const float skyVisibility = std::clamp(colors.dayFactor + colors.moonVisibility * 0.18f, 0.0f, 1.0f);
    data.skyIlluminance = colors.skyAmbientColor * (0.10f + 0.42f * skyVisibility);

    // Keep CPU fallback aligned with gameplay_sky.fs mode 5 and DerivativeMain:
    // GetSunAndSkyIrradiance() itself is weather-independent. Wetness attenuates
    // direct light later via deferred cloudShadow and tints sky radiance in capture.
    (void)weatherWetness;
    (void)weatherStorm;

    return data;
}

glm::vec3 GameplaySkyRenderer::computeCloudDynamicWeather(const int worldDay, const int worldTime) {
    // Replicates DerivativeMain deferred.vsh cloudDynamicWeather computation.
    // Uses triple32 hash + per-day random weather maps interpolated over the day cycle.
    auto triple32 = [](uint32_t x) -> uint32_t {
        x ^= x >> 17; x *= 0xed5ad4bbu;
        x ^= x >> 11; x *= 0xac4c1b51u;
        x ^= x >> 15; x *= 0x31848babu;
        x ^= x >> 14;
        return x;
    };
    auto hash1 = [](float p) -> float {
        p = std::fmod(p, 1.0f);
        if (p < 0.0f) p += 1.0f;
        p = p * p * (3.0f - 2.0f * p); // fract approximation not needed, just use p
        // Exact GLSL: p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p);
        float v = p * 0.1031f;
        v = v - std::floor(v);
        v *= v + 33.33f;
        v *= v + v;
        return v - std::floor(v);
    };
    auto randWeather = [&](int state) -> glm::vec2 {
        float h = hash1(static_cast<float>(triple32(static_cast<uint32_t>(state))) / static_cast<float>(0xFFFFFFFFu));
        return glm::vec2(h, hash1(h + 0.1f)); // second component from offset hash
    };
    auto curve = [](float x) -> float {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    };
    auto remap = [](float lo, float hi, float x) -> float {
        return std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    };

    // Interpolation factor: fract(worldTime / 24000.0 + vec2(0.65, 0.25))
    const float dayFrac = static_cast<float>(worldTime) / 24000.0f;
    const float tX = dayFrac + 0.65f;
    const float tY = dayFrac + 0.25f;
    const float curveTX = curve(tX - std::floor(tX));
    const float curveTY = curve(tY - std::floor(tY));

    const glm::vec2 w0 = randWeather(worldDay);
    const glm::vec2 w1 = randWeather(worldDay + 1);

    glm::vec2 weatherMap;
    weatherMap.x = w0.x + (w1.x - w0.x) * curveTX;
    weatherMap.y = w0.y + (w1.y - w0.y) * curveTY;

    glm::vec3 result;
    result.x = curve(remap(0.25f, 0.4f, weatherMap.x)) * 0.5f;           // cirrocumulus
    result.y = (1.0f - remap(0.65f, 0.8f, weatherMap.y));
    result.y = result.y * result.y * 0.5f;                                 // cirrus
    result.z = remap(0.4f, 0.55f, weatherMap.x * 2.0f - weatherMap.y);    // storm
    result.z *= 2.0f - result.z;

    return result;
}

glm::vec3 GameplaySkyRenderer::getLastFogColor() const {
    return m_lastColors.fog;
}

std::pair<glm::vec2, glm::vec2> GameplaySkyRenderer::getMoonPhaseUv(const int phaseIndex) {
    const int clamped = std::clamp(phaseIndex, 0, 7);
    const int col = clamped % 4;
    const int row = clamped / 4;
    const glm::vec2 uvMin(static_cast<float>(col) * 0.25f, static_cast<float>(row) * 0.5f);
    return {uvMin, uvMin + glm::vec2(0.25f, 0.5f)};
}

void GameplaySkyRenderer::initMeshes() {
    if (m_skyVao == 0) {
        constexpr std::array<float, 18> skyVertices = {
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
             3.0f, -1.0f, 0.0f, 2.0f, 0.0f, 1.0f,
            -1.0f,  3.0f, 0.0f, 0.0f, 2.0f, 1.0f,
        };

        glGenVertexArrays(1, &m_skyVao);
        glGenBuffers(1, &m_skyVbo);
        glBindVertexArray(m_skyVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_skyVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(skyVertices.size() * sizeof(float)),
                     skyVertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glBindVertexArray(0);
        RhiBufferDesc bufferDesc;
        bufferDesc.debugName = "GameplaySky.Gradient.VertexBuffer";
        bufferDesc.size = skyVertices.size() * sizeof(float);
        bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
        bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        m_skyVertexBuffer = m_rhiDevice->createBuffer(
            bufferDesc, skyVertices.data(), bufferDesc.size);
        if (!m_skyVertexBuffer.isValid()) std::abort();
    }

    if (m_haloVao == 0) {
        constexpr int kSegments = 48;
        std::vector<HaloVertex> haloVertices;
        haloVertices.reserve(static_cast<size_t>(kSegments) * 3);

        const glm::vec4 centerColor(1.0f, 0.42f, 0.06f, 1.0f);
        const glm::vec4 edgeColor(1.0f, 0.42f, 0.06f, 0.0f);
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = static_cast<float>(i) / static_cast<float>(kSegments) * kTwoPi;
            const float a1 = static_cast<float>(i + 1) / static_cast<float>(kSegments) * kTwoPi;
            haloVertices.push_back({glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.5f), centerColor});
            haloVertices.push_back({glm::vec3(std::cos(a0), std::sin(a0), 0.0f), glm::vec2(0.0f), edgeColor});
            haloVertices.push_back({glm::vec3(std::cos(a1), std::sin(a1), 0.0f), glm::vec2(0.0f), edgeColor});
        }

        glGenVertexArrays(1, &m_haloVao);
        glGenBuffers(1, &m_haloVbo);
        glBindVertexArray(m_haloVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_haloVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(haloVertices.size() * sizeof(HaloVertex)),
                     haloVertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HaloVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HaloVertex), reinterpret_cast<void*>(sizeof(glm::vec3)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(HaloVertex), reinterpret_cast<void*>(sizeof(glm::vec3) + sizeof(glm::vec2)));
        glBindVertexArray(0);
        RhiBufferDesc bufferDesc;
        bufferDesc.debugName = "GameplaySky.Halo.VertexBuffer";
        bufferDesc.size = haloVertices.size() * sizeof(HaloVertex);
        bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
        bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        m_haloVertexBuffer = m_rhiDevice->createBuffer(
            bufferDesc, haloVertices.data(), bufferDesc.size);
        if (!m_haloVertexBuffer.isValid()) std::abort();
        m_haloVertexCount = static_cast<int32_t>(haloVertices.size());
    }

}

void GameplaySkyRenderer::destroyMeshes() {
    if (m_rhiDevice != nullptr) {
        if (m_cloudVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_cloudVertexBuffer);
        if (m_haloVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_haloVertexBuffer);
        if (m_skyVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_skyVertexBuffer);
    }
    m_cloudVertexBuffer = {};
    m_haloVertexBuffer = {};
    m_skyVertexBuffer = {};
    auto deleteBuffer = [](uint32_t& vao, uint32_t& vbo) {
        if (vbo != 0) {
            glDeleteBuffers(1, &vbo);
            vbo = 0;
        }
        if (vao != 0) {
            glDeleteVertexArrays(1, &vao);
            vao = 0;
        }
    };

    deleteBuffer(m_skyVao, m_skyVbo);
    deleteBuffer(m_haloVao, m_haloVbo);
    deleteBuffer(m_cloudVao, m_cloudVbo);
    m_haloVertexCount = 0;
    m_cloudVertexCount = 0;
    m_cloudMeshInfo = {};
}

void GameplaySkyRenderer::ensureDummySkyCaptureTexture() {
    if (m_dummySkyCaptureTexture != 0) {
        return;
    }

    constexpr std::array<float, 4> pixel = {0.0f, 0.0f, 0.0f, 1.0f};
    glCreateTextures(GL_TEXTURE_2D, 1, &m_dummySkyCaptureTexture);
    glTextureStorage2D(m_dummySkyCaptureTexture, 1, GL_RGBA16F, 1, 1);
    glTextureSubImage2D(m_dummySkyCaptureTexture, 0, 0, 0, 1, 1,
                        GL_RGBA, GL_FLOAT, pixel.data());
    glTextureParameteri(m_dummySkyCaptureTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_dummySkyCaptureTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_dummySkyCaptureTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_dummySkyCaptureTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void GameplaySkyRenderer::bindDummySkyCaptureTexture(const int32_t unit) {
    ensureDummySkyCaptureTexture();
    m_shader->setInt("uSkyCaptureTex", unit);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_dummySkyCaptureTexture);
}

void GameplaySkyRenderer::initCloudMesh() {
    if (m_cloudVao != 0) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* rawPixels = stbi_load(CLOUD_TEXTURE_PATH, &width, &height, &channels, 4);
    if (rawPixels == nullptr || width <= 0 || height <= 0) {
        if (rawPixels != nullptr) {
            stbi_image_free(rawPixels);
        }
        return;
    }

    std::vector<unsigned char> pixels(rawPixels, rawPixels + static_cast<size_t>(width * height * 4));
    stbi_image_free(rawPixels);

    const int maskWidth = std::max(1, (width + kCloudMaskSample - 1) / kCloudMaskSample);
    const int maskHeight = std::max(1, (height + kCloudMaskSample - 1) / kCloudMaskSample);
    std::vector<uint8_t> mask(static_cast<size_t>(maskWidth * maskHeight), 0);

    for (int my = 0; my < maskHeight; ++my) {
        for (int mx = 0; mx < maskWidth; ++mx) {
            int solidCount = 0;
            int sampleCount = 0;
            for (int sy = 0; sy < kCloudMaskSample; ++sy) {
                for (int sx = 0; sx < kCloudMaskSample; ++sx) {
                    const int px = mx * kCloudMaskSample + sx;
                    const int py = my * kCloudMaskSample + sy;
                    if (px >= width || py >= height) {
                        continue;
                    }
                    ++sampleCount;
                    if (isCloudPixelSolid(pixels, width, height, px, py)) {
                        ++solidCount;
                    }
                }
            }
            if (sampleCount > 0 && solidCount * kCloudSolidDenominator >= sampleCount * kCloudSolidNumerator) {
                mask[static_cast<size_t>(my * maskWidth + mx)] = 1;
            }
        }
    }

    std::vector<uint8_t> exteriorEmpty(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::deque<glm::ivec2> queue;
    auto enqueueEmpty = [&](const int x, const int y) {
        if (x < 0 || y < 0 || x >= maskWidth || y >= maskHeight) {
            return;
        }
        const size_t idx = static_cast<size_t>(y * maskWidth + x);
        if (mask[idx] != 0 || exteriorEmpty[idx] != 0) {
            return;
        }
        exteriorEmpty[idx] = 1;
        queue.emplace_back(x, y);
    };

    for (int x = 0; x < maskWidth; ++x) {
        enqueueEmpty(x, 0);
        enqueueEmpty(x, maskHeight - 1);
    }
    for (int y = 0; y < maskHeight; ++y) {
        enqueueEmpty(0, y);
        enqueueEmpty(maskWidth - 1, y);
    }
    while (!queue.empty()) {
        const glm::ivec2 p = queue.front();
        queue.pop_front();
        enqueueEmpty(p.x - 1, p.y);
        enqueueEmpty(p.x + 1, p.y);
        enqueueEmpty(p.x, p.y - 1);
        enqueueEmpty(p.x, p.y + 1);
    }

    std::vector<CloudVertex> vertices;
    vertices.reserve(static_cast<size_t>(maskWidth * maskHeight * 18));

    const float cellWorldSize = kCloudCellSize * static_cast<float>(kCloudMaskSample);
    const float tileWidth = static_cast<float>(maskWidth) * cellWorldSize;
    const float tileDepth = static_cast<float>(maskHeight) * cellWorldSize;
    const float originX = -tileWidth * 0.5f;
    const float originZ = -tileDepth * 0.5f;
    const float y0 = -kCloudThickness * 0.5f;
    const float y1 = kCloudThickness * 0.5f;

    std::vector<uint8_t> topBottomSurface = mask;
    std::vector<uint8_t> negXSurface(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::vector<uint8_t> posXSurface(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::vector<uint8_t> negZSurface(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::vector<uint8_t> posZSurface(static_cast<size_t>(maskWidth * maskHeight), 0);

    for (int y = 0; y < maskHeight; ++y) {
        for (int x = 0; x < maskWidth; ++x) {
            if (!isMaskSolid(mask, maskWidth, maskHeight, x, y)) {
                continue;
            }
            const size_t idx = static_cast<size_t>(y * maskWidth + x);
            negXSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x - 1, y) ? 1 : 0;
            posXSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x + 1, y) ? 1 : 0;
            negZSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x, y - 1) ? 1 : 0;
            posZSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x, y + 1) ? 1 : 0;
        }
    }

    appendGreedySurface(vertices, topBottomSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 0, 1.00f);
    appendGreedySurface(vertices, topBottomSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 1, 0.70f);
    appendGreedySurface(vertices, negXSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 2, 0.82f);
    appendGreedySurface(vertices, posXSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 3, 0.82f);
    appendGreedySurface(vertices, negZSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 4, 0.76f);
    appendGreedySurface(vertices, posZSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 5, 0.88f);

    if (vertices.empty()) {
        return;
    }

    glGenVertexArrays(1, &m_cloudVao);
    glGenBuffers(1, &m_cloudVbo);
    glBindVertexArray(m_cloudVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cloudVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(CloudVertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CloudVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(CloudVertex), reinterpret_cast<void*>(sizeof(glm::vec3)));
    glBindVertexArray(0);
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "GameplaySky.Cloud.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(CloudVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_cloudVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, vertices.data(), bufferDesc.size);
    if (!m_cloudVertexBuffer.isValid()) std::abort();

    m_cloudVertexCount = static_cast<int32_t>(vertices.size());
    m_cloudMeshInfo.tileWorldSize = tileWidth;
    m_cloudMeshInfo.valid = true;
}

void GameplaySkyRenderer::renderSkyGradient(const Camera& camera, const float aspect, const SkyColors& colors, const uint32_t skyCaptureTexture) {
    m_shader->use();
    m_shader->setInt("uMode", 0);
    m_shader->setMat4("uView", buildSkyView(camera));
    m_shader->setMat4("uProjection", glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 100.0f));
    m_shader->setMat4("uModel", glm::mat4(1.0f));
    m_shader->setVec3("uSkyTopColor", colors.top);
    m_shader->setVec3("uSkyHorizonColor", colors.horizon);
    m_shader->setVec3("uSunDirection", colors.sunDirection);
    m_shader->setVec3("uMoonDirection", colors.moonDirection);
    m_shader->setVec3("uSunScatterColor", colors.sunScatter);
    m_shader->setVec3("uMoonLightColor", colors.moonLightColor);
    m_shader->setFloat("uHorizonHaze", colors.horizonHaze);
    m_shader->setFloat("uSunGlare", colors.sunGlare);
    m_shader->setFloat("uSunVisibility", colors.sunVisibility);
    m_shader->setFloat("uMoonVisibility", colors.moonVisibility);
    m_shader->setFloat("uMoonPhaseAngle", colors.moonPhaseAngle);
    m_shader->setFloat("uNightFactor", colors.nightFactor);
    m_shader->setVec4("uTintColor", glm::vec4(1.0f));
    m_shader->setVec2("uUvMin", glm::vec2(0.0f));
    m_shader->setVec2("uUvMax", glm::vec2(1.0f));
    m_shader->setInt("uSkyCaptureEnabled", skyCaptureTexture != 0 ? 1 : 0);

    // Keep all gameplay_sky sampler2D uniforms on unit 0 so they never collide
    // with uAtmosphereLut, which is a sampler3D fixed to unit 1.
    m_shader->setInt("uSkyCaptureTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skyCaptureTexture);

    glDisable(GL_BLEND);
    glBindVertexArray(m_skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void GameplaySkyRenderer::renderClouds(const Camera& camera,
                                       const float aspect,
                                       const DayNightSystem& dayNight,
                                       const SkyColors& colors,
                                       RhiCommandList& commandList) {
    if (!m_cloudVertexBuffer.isValid() || !m_cloudMeshInfo.valid) {
        return;
    }

    const glm::vec3 cameraPos = camera.getPosition();
    const float cloudY = kCloudHeight;
    if (cameraPos.y >= cloudY - 2.0f) {
        return;
    }

    const float tileSize = std::max(m_cloudMeshInfo.tileWorldSize, 1.0f);
    const float drift = static_cast<float>(dayNight.getTotalGameTime()) * kCloudDriftSpeed;
    const float baseTileX = std::floor((cameraPos.x - drift) / tileSize);
    const float baseTileZ = std::floor(cameraPos.z / tileSize);

    struct PushConstants { glm::mat4 viewProj; glm::mat4 model; glm::vec4 tint; };
    const glm::mat4 viewProj =
        glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 1200.0f) *
        camera.getViewMatrix();
    commandList.setGraphicsPipeline(m_cloudPipeline);
    commandList.setVertexBuffer(0u, m_cloudVertexBuffer, 0u);

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const float tileX = baseTileX + static_cast<float>(dx);
            const float tileZ = baseTileZ + static_cast<float>(dz);
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(tileX * tileSize + drift, cloudY, tileZ * tileSize));
            const PushConstants constants{viewProj, model, glm::vec4(colors.cloudColor, 1.0f)};
            commandList.pushConstants(&constants, sizeof(constants),
                rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
            commandList.draw(static_cast<uint32_t>(m_cloudVertexCount), 1u, 0u, 0u);
        }
    }
}

void GameplaySkyRenderer::renderHalo(const Camera& camera,
                                     const float aspect,
                                     const DayNightSystem& dayNight,
                                     const SkyColors& colors,
                                     RhiCommandList& commandList) {
    if (!m_haloVertexBuffer.isValid() || colors.haloStrength <= 0.001f) {
        return;
    }

    const glm::vec3 direction = directionFromAngle(dayNight.getCelestialAngleRadians());
    const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = safeNormalize(glm::cross(direction, up), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 localUp = safeNormalize(glm::cross(right, direction), up);

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * kHaloSize, 0.0f);
    model[1] = glm::vec4(localUp * kHaloSize, 0.0f);
    model[2] = glm::vec4(direction, 0.0f);
    model[3] = glm::vec4(direction * 12.0f, 1.0f);

    struct PushConstants { glm::mat4 viewProj; glm::mat4 model; glm::vec4 tint; };
    const PushConstants constants{
        glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 100.0f) *
            buildSkyView(camera),
        model,
        colors.halo
    };
    commandList.setGraphicsPipeline(m_haloPipeline);
    commandList.setVertexBuffer(0u, m_haloVertexBuffer, 0u);
    commandList.pushConstants(&constants, sizeof(constants),
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(static_cast<uint32_t>(m_haloVertexCount), 1u, 0u, 0u);
}

glm::mat4 GameplaySkyRenderer::buildSkyView(const Camera& camera) const {
    return glm::mat4(glm::mat3(camera.getViewMatrix()));
}

glm::vec3 GameplaySkyRenderer::directionFromAngle(const float angleRadians) const {
    return safeNormalize(glm::vec3(0.25f, std::sin(angleRadians), -std::cos(angleRadians)),
                         glm::vec3(0.0f, 1.0f, 0.0f));
}
