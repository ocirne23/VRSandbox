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
        perFrame.indirectDispatchBuffer.initialize(sizeof(vk::DispatchIndirectCommand),
            vk::BufferUsageFlagBits::eIndirectBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedDispatchBuffer = (vk::DispatchIndirectCommand*)perFrame.indirectDispatchBuffer.mapMemory().data();

        perFrame.computeMeshInfoBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(RendererVKLayout::MeshInfo),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedMeshInfo = (RendererVKLayout::MeshInfo*)perFrame.computeMeshInfoBuffer.mapMemory().data();

        perFrame.instanceDataBuffer.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshInstance),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedMeshInstances = (RendererVKLayout::MeshInstance*)perFrame.instanceDataBuffer.mapMemory().data();

        perFrame.indirectCommandBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand),
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.instanceIdxBuffer.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(uint32),
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    }

    ComputePipelineLayout computePipelineLayout;
    computePipelineLayout.computeShaderText = std::move(FileSystem::readFileStr("Shaders/instanced_indirect.cs.glsl"));
    auto& descriptorSetBindings = computePipelineLayout.descriptorSetLayoutBindings;
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // UBO
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInstances
        .binding = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInfoBuffer
        .binding = 2,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutMeshInstanceIndexes
        .binding = 3,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutIndirectCommandBufer
        .binding = 4,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex
    });
    m_computePipeline.initialize(computePipelineLayout);

    return true;
}

void IndirectCullComputePipeline::update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers)
{
    uint32 instanceCounter = 0;
    uint32 meshInfoCounter = 0;

    PerFrameData& frameData = m_perFrameData[frameIdx];
    const static vk::DeviceSize atomSize = Globals::device.getNonCoherentAtomSize();

    for (ObjectContainer* pObjectContainer : objectContainers)
    {
        std::vector<RendererVKLayout::MeshInfo>& meshInfos = pObjectContainer->m_meshInfos;
        const uint32 numMeshInfos = (uint32)meshInfos.size();
        meshInfoCounter += numMeshInfos;
        for (uint32 i = 0; i < numMeshInfos; ++i)
        {
            meshInfos[i].firstInstance = instanceCounter;
            assert(instanceCounter <= RendererVKLayout::MAX_INSTANCE_DATA);

            const std::vector<RendererVKLayout::MeshInstance>& meshInstances = pObjectContainer->m_meshInstances[i];
            memcpy(&frameData.mappedMeshInstances[instanceCounter], meshInstances.data(), meshInstances.size() * sizeof(RendererVKLayout::MeshInstance));
            instanceCounter += (uint32)meshInstances.size();
        }
        memcpy(&frameData.mappedMeshInfo[pObjectContainer->m_baseMeshInfoIdx], meshInfos.data(), sizeof(RendererVKLayout::MeshInfo) * numMeshInfos);
    }
    frameData.mappedDispatchBuffer[0] = vk::DispatchIndirectCommand{ .x = instanceCounter, .y = 1, .z = 1 };

    {   // flush instance data buffer
        vk::DeviceSize size = instanceCounter * sizeof(RendererVKLayout::MeshInstance);
        size = (size + atomSize - 1) & ~(atomSize - 1);
        Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
            .memory = frameData.instanceDataBuffer.getMemory(), .offset = 0, .size = size
        } });
    }
    {   // flush meshinfo buffer
        vk::DeviceSize size = meshInfoCounter * sizeof(RendererVKLayout::MeshInfo);
        size = (size + atomSize - 1) & ~(atomSize - 1);
        Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
            .memory = frameData.computeMeshInfoBuffer.getMemory(), .offset = 0, .size = size
        } });
    }
    Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.indirectDispatchBuffer.getMemory(), .offset = 0, .size = vk::WholeSize
    } });

    frameData.instanceCounter = instanceCounter;
    frameData.meshInfoCounter = meshInfoCounter;
}

void IndirectCullComputePipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, Buffer& inUbo)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];

    std::array<DescriptorSetUpdateInfo, 5> computeDescriptorSetUpdateInfos
    {
        DescriptorSetUpdateInfo{ // UBO
            .binding = 0,
            .type = vk::DescriptorType::eUniformBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = inUbo.getBuffer(),
                .range = sizeof(RendererVKLayout::Ubo),
            }
        },
        DescriptorSetUpdateInfo{ // InMeshInstances
            .binding = 1,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = frameData.instanceDataBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshInstance),
            }
        },
        DescriptorSetUpdateInfo{ // InMeshInfoBuffer
            .binding = 2,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = frameData.computeMeshInfoBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(RendererVKLayout::MeshInfo),
            }
        },
        DescriptorSetUpdateInfo{ // OutMeshInstanceIndexes
            .binding = 3,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = frameData.instanceIdxBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_INSTANCE_DATA * sizeof(uint32),
            }
        },
        DescriptorSetUpdateInfo{ // OutIndirectCommandBufer
            .binding = 4,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = frameData.indirectCommandBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand),
            }
        }
    };

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    {   // Compute shader frustum cull and indirect command buffer generation
        // There's some synchronization issue here...
        //vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eDeviceGroup, {
        //    vk::MemoryBarrier{.srcAccessMask = vk::AccessFlagBits::eNone, .dstAccessMask = vk::AccessFlagBits::eTransferRead } }, {}, {});

        vkCommandBuffer.fillBuffer(frameData.indirectCommandBuffer.getBuffer(), 0, frameData.meshInfoCounter * sizeof(vk::DrawIndexedIndirectCommand), 0);

        vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eDeviceGroup, {
            vk::MemoryBarrier{.srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eShaderRead } }, {}, {});

        vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipeline());
        commandBuffer.cmdUpdateDescriptorSets(m_computePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, computeDescriptorSetUpdateInfos);
        vkCommandBuffer.dispatchIndirect(frameData.indirectDispatchBuffer.getBuffer(), 0);

        vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlagBits::eDeviceGroup, {
            vk::MemoryBarrier{.srcAccessMask = vk::AccessFlagBits::eShaderWrite, .dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead } }, {}, {});
    }
}