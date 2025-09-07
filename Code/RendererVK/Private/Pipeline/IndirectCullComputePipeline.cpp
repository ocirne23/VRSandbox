module RendererVK.IndirectCullComputePipeline;

import Core;
import File.FileSystem;
import RendererVK.Device;
import RendererVK.Layout;
import RendererVK.ObjectContainer;

IndirectCullComputePipeline::IndirectCullComputePipeline() {}
IndirectCullComputePipeline::~IndirectCullComputePipeline() {}

bool IndirectCullComputePipeline::initialize()
{
    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.inIndirectCommandBuffer.initialize(sizeof(vk::DispatchIndirectCommand), // x
            vk::BufferUsageFlagBits::eIndirectBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedIndirectCommands = perFrame.inIndirectCommandBuffer.mapMemory<vk::DispatchIndirectCommand>();

        perFrame.outMeshInstancesBuffer.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::OutMeshInstance), // 5
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.outMeshInstanceIndexesBuffer.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(uint32), // 6
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.outIndirectCommandBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand), // 7
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    }

    ComputePipelineLayout computePipelineLayout;
    computePipelineLayout.computeShaderDebugFilePath = "Shaders/instanced_indirect.cs.glsl";
    computePipelineLayout.computeShaderText = FileSystem::readFileStr(computePipelineLayout.computeShaderDebugFilePath);
    auto& descriptorSetBindings = computePipelineLayout.descriptorSetLayoutBindings;
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // UBO
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InRenderNodeTransformsBuffer
        .binding = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInstancesBuffer
        .binding = 2,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInstanceOffsetsBuffer
        .binding = 3,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInfoBuffer
        .binding = 4,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InFirstInstanceBuffer
        .binding = 5,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutMeshInstancesBuffer
        .binding = 6,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutMeshInstanceIndexesBuffer
        .binding = 7,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutIndirectCommandBuffer
        .binding = 8,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex
    });

    m_computePipeline.initialize(computePipelineLayout);

    return true;
}

void IndirectCullComputePipeline::update(uint32 frameIdx, uint32 numMeshInstances)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
   
    frameData.mappedIndirectCommands[0] = vk::DispatchIndirectCommand{ .x = numMeshInstances, .y = 1, .z = 1 };

    auto flushResult = Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.inIndirectCommandBuffer.getMemory(), .offset = 0, .size = vk::WholeSize
    }});
    if (flushResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to flush indirect command buffer memory");
    }
}

void IndirectCullComputePipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& recordParams)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];

    std::array<DescriptorSetUpdateInfo, 9> computeDescriptorSetUpdateInfos
    {
        DescriptorSetUpdateInfo{ // UBO
            .binding = 0,
            .type = vk::DescriptorType::eUniformBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = recordParams.ubo.getBuffer(),
                .range = sizeof(RendererVKLayout::Ubo),
            }
        },
        DescriptorSetUpdateInfo{ // InRenderNodeTransformsBuffer
            .binding = 1,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = recordParams.inRenderNodeTransformsBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_RENDER_NODES * sizeof(RendererVKLayout::RenderNodeTransform),
            }
        },
        DescriptorSetUpdateInfo{ // InMeshInstancesBuffer
            .binding = 2,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = recordParams.inMeshInstancesBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::InMeshInstance),
            }
        },
        DescriptorSetUpdateInfo{ // InMeshInstanceOffsetsBuffer
            .binding = 3,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = recordParams.inMeshInstanceOffsetsBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_INSTANCE_OFFSETS * sizeof(RendererVKLayout::MeshInstanceOffset),
            }
        },
        DescriptorSetUpdateInfo{ // InMeshInfoBuffer
            .binding = 4,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = recordParams.inMeshInfoBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(RendererVKLayout::MeshInfo),
            }
        },
        DescriptorSetUpdateInfo{ // InFirstInstancesBuffer
            .binding = 5,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = recordParams.inFirstInstancesBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(uint32),
            }
        },

        DescriptorSetUpdateInfo{ // OutMeshInstancesBuffer
            .binding = 6,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = frameData.outMeshInstancesBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::OutMeshInstance),
            }
        },
        DescriptorSetUpdateInfo{ // OutMeshInstanceIndexesBuffer
            .binding = 7,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = frameData.outMeshInstanceIndexesBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_INSTANCE_DATA * sizeof(uint32),
            }
        },
        DescriptorSetUpdateInfo{ // OutIndirectCommandBuffer
            .binding = 8,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = frameData.outIndirectCommandBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand),
            }
        }
    };

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    {   // Compute shader frustum cull and indirect command buffer generation
        // There's some synchronization issue here...
        vkCommandBuffer.fillBuffer(frameData.outIndirectCommandBuffer.getBuffer(), 0, numMeshes * sizeof(vk::DrawIndexedIndirectCommand), 0);

        vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags::Flags(0), {
            vk::MemoryBarrier{.srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eShaderRead } }, {}, {});

        vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipeline());
        commandBuffer.cmdUpdateDescriptorSets(m_computePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, computeDescriptorSetUpdateInfos);
        vkCommandBuffer.dispatchIndirect(frameData.inIndirectCommandBuffer.getBuffer(), 0);

        vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlags::Flags(0), {
            vk::MemoryBarrier{.srcAccessMask = vk::AccessFlagBits::eShaderWrite, .dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead } }, {}, {});
    }
}