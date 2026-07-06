#ifndef MECRAFT_RHI_PIPELINE_H
#define MECRAFT_RHI_PIPELINE_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <vector>

struct RhiShaderDesc {
    const char* debugName = nullptr;
    RhiShaderStage stage = RhiShaderStage::Vertex;
    const void* bytecode = nullptr;
    uint64_t bytecodeSize = 0;
    const char* source = nullptr;
    uint64_t sourceSize = 0;
    const char* entryPoint = "main";
};

struct RhiVertexAttribute {
    uint32_t location = 0;
    uint32_t binding = 0;
    RhiVertexFormat format = RhiVertexFormat::Float3;
    uint32_t offset = 0;
};

struct RhiVertexBinding {
    uint32_t binding = 0;
    uint32_t stride = 0;
    RhiVertexInputRate inputRate = RhiVertexInputRate::Vertex;
};

struct RhiVertexInputLayout {
    std::vector<RhiVertexBinding> bindings;
    std::vector<RhiVertexAttribute> attributes;
};

struct RhiRasterState {
    RhiCullMode cullMode = RhiCullMode::Back;
    RhiFrontFace frontFace = RhiFrontFace::CounterClockwise;
    bool depthClampEnabled = false;
    bool scissorEnabled = false;
};

struct RhiDepthStencilState {
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    RhiCompareOp depthCompare = RhiCompareOp::Less;
};

struct RhiBlendAttachmentState {
    bool blendEnabled = false;
    RhiBlendFactor srcColor = RhiBlendFactor::One;
    RhiBlendFactor dstColor = RhiBlendFactor::Zero;
    RhiBlendOp colorOp = RhiBlendOp::Add;
    RhiBlendFactor srcAlpha = RhiBlendFactor::One;
    RhiBlendFactor dstAlpha = RhiBlendFactor::Zero;
    RhiBlendOp alphaOp = RhiBlendOp::Add;
};

struct RhiBlendState {
    std::vector<RhiBlendAttachmentState> attachments;
};

struct RhiPipelineLayoutDesc {
    const char* debugName = nullptr;
    std::vector<RhiBindGroupLayoutHandle> bindGroupLayouts;
    uint32_t pushConstantBytes = 0;
    RhiShaderStageFlags pushConstantStages = 0;
};

struct RhiGraphicsPipelineDesc {
    const char* debugName = nullptr;
    RhiShaderHandle vertexShader;
    RhiShaderHandle fragmentShader;
    RhiPipelineLayoutHandle layout;
    RhiVertexInputLayout vertexInput;
    RhiPrimitiveTopology topology = RhiPrimitiveTopology::TriangleList;
    RhiRasterState raster;
    RhiDepthStencilState depthStencil;
    RhiBlendState blend;
    std::vector<RhiTextureFormat> colorFormats;
    RhiTextureFormat depthFormat = RhiTextureFormat::Undefined;
};

struct RhiComputePipelineDesc {
    const char* debugName = nullptr;
    RhiShaderHandle computeShader;
    RhiPipelineLayoutHandle layout;
};

#endif // MECRAFT_RHI_PIPELINE_H
