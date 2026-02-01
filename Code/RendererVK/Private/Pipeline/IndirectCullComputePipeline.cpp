module RendererVK:IndirectCullComputePipeline;

import Core;
import File.FileSystem;
import :Device;
import :Layout;
import :ObjectContainer;

IndirectCullComputePipeline::IndirectCullComputePipeline() {}
IndirectCullComputePipeline::~IndirectCullComputePipeline() {}

void IndirectCullComputePipeline::initialize()
{
    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.inIndirectCommandBuffer.initialize(sizeof(vk::DispatchIndirectCommand), // x
            vk::BufferUsageFlagBits::eIndirectBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedIndirectCommands = perFrame.inIndirectCommandBuffer.mapMemory<vk::DispatchIndirectCommand>();

        perFrame.outMeshInstancesBuffer.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::OutMeshInstance), // 6
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.outMeshInstanceIndexesBuffer.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(uint32), // 7
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.outIndirectCommandBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand), // 8
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        /*
        perFrame.inOutInstanceTable.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(uint32) , // 6
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        */
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
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    /*
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InOutInstanceTable
        .binding = 9,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutInstanceTableValues
        .binding = 10,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });*/

    m_computePipeline.initialize(computePipelineLayout);
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
        DescriptorSetUpdateInfo { // UBO
            .binding = 0,
            .type = vk::DescriptorType::eUniformBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = recordParams.ubo.getBuffer(),
                    .range = recordParams.ubo.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // InRenderNodeTransformsBuffer
            .binding = 1,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = recordParams.inRenderNodeTransformsBuffer.getBuffer(),
                    .range = recordParams.inRenderNodeTransformsBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // InMeshInstancesBuffer
            .binding = 2,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = recordParams.inMeshInstancesBuffer.getBuffer(),
                    .range = recordParams.inMeshInstancesBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // InMeshInstanceOffsetsBuffer
            .binding = 3,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = recordParams.inMeshInstanceOffsetsBuffer.getBuffer(),
                    .range = recordParams.inMeshInstanceOffsetsBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // InMeshInfoBuffer
            .binding = 4,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = recordParams.inMeshInfoBuffer.getBuffer(),
                    .range = recordParams.inMeshInfoBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // InFirstInstancesBuffer
            .binding = 5,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = recordParams.inFirstInstancesBuffer.getBuffer(),
                    .range = recordParams.inFirstInstancesBuffer.getSize(),
                }
            }
        },

        DescriptorSetUpdateInfo { // OutMeshInstancesBuffer
            .binding = 6,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = frameData.outMeshInstancesBuffer.getBuffer(),
                    .range = frameData.outMeshInstancesBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // OutMeshInstanceIndexesBuffer
            .binding = 7,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = frameData.outMeshInstanceIndexesBuffer.getBuffer(),
                    .range = frameData.outMeshInstanceIndexesBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // OutIndirectCommandBuffer
            .binding = 8,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = frameData.outIndirectCommandBuffer.getBuffer(),
                    .range = frameData.outIndirectCommandBuffer.getSize(),
                }
            }
        },
        /*
        DescriptorSetUpdateInfo { // InOutInstanceTable
            .binding = 9,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = frameData.inOutInstanceTable.getBuffer(),
                    .range = frameData.inOutInstanceTable.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo { // OutInstanceTableValues
            .binding = 10,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = frameData.outInstanceTableValues.getBuffer(),
                    .range = frameData.outInstanceTableValues.getSize(),
                }
            }
        }*/
    };

    // Compute shader frustum cull and indirect command buffer generation
    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    {
        vk::DescriptorSet descriptorSet = recordParams.descriptorSet.getDescriptorSet();
        vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipeline());
        commandBuffer.cmdUpdateDescriptorSets(m_computePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, descriptorSet, computeDescriptorSetUpdateInfos);
        vkCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);
        vkCommandBuffer.fillBuffer(frameData.outIndirectCommandBuffer.getBuffer(), 0, numMeshes * sizeof(vk::DrawIndexedIndirectCommand), 0);
        
        {
            vk::MemoryBarrier2 memoryBarrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eClear,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            };
            vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &memoryBarrier });
        }

        vkCommandBuffer.dispatchIndirect(frameData.inIndirectCommandBuffer.getBuffer(), 0);

        {
            vk::MemoryBarrier2 memoryBarrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eVertexShader,
                .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead,
            };
            vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &memoryBarrier });
        }

        {
            vk::MemoryBarrier2 memoryBarrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eClear | vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &memoryBarrier });
        }
    }
}