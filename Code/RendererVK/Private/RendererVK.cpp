module RendererVK;

import Core;
import RendererVK.VK;
import RendererVK.glslang;
import RendererVK.Mesh;
import RendererVK.MeshInstance;
import RendererVK.Layout;
import Core.Window;
import Core.Frustum;
import File.SceneData;
import File.MeshData;

const char* vertexShaderText = R"(
#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (std140, binding = 0) uniform buffer
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
};

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;

// Instanced attributes
layout (location = 3) in vec3 inst_pos;
layout (location = 4) in float inst_scale;
layout (location = 5) in vec4 inst_quat;

layout (location = 0) out vec2 out_uv;

vec3 quat_transform( vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main()
{
    out_uv = in_uv;
    vec3 pos = quat_transform(in_pos * inst_scale, inst_quat);
    gl_Position = u_mvp * vec4(pos + inst_pos, 1.0);
}
)";

const char* fragmentShaderText = R"(
#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform sampler2D u_tex;
layout (location = 0) in vec2 in_uv;
layout (location = 0) out vec3 out_color;

void main()
{
    out_color = texture(u_tex, in_uv).xyz;
}
)";

const char* computeShaderText = R"(
#version 450

layout (std140, binding = 0) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
};

struct InMeshInstance
{
    vec4 posScale;
    vec4 quat;
    uint meshIdx;
};
layout (binding = 2, std430) readonly buffer InMeshInstances
{
    InMeshInstance in_instances[];
};

struct InMeshInfo
{
    float radius;
    uint indexCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};
layout (binding = 3, std430) readonly buffer InMeshInfoBuffer
{
    InMeshInfo in_meshInfo[];
};

struct OutMeshRenderLayout
{
    vec4 posScale;
    vec4 quat;
};
layout (binding = 4, std430) writeonly buffer OutRenderData
{
    OutMeshRenderLayout out_renderData[];
};

struct OutInstancedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};
layout (binding = 5, std430) writeonly buffer OutIndirectCommandBufer
{
    OutInstancedIndirectCommand out_indirectCommands[];
};

bool frustumCheck(vec4 pos, float radius)
{
    // Check sphere against frustum planes
    for (int i = 0; i < 6; i++) 
    {
        if (dot(pos, u_frustumPlanes[i]) + radius < 0.0)
        {
            return false;
        }
    }
    return true;
}

void main()
{
    const uint instanceIdx   = gl_GlobalInvocationID.x;
    const uint meshIdx       = in_instances[instanceIdx].meshIdx;
    const uint firstInstance = in_meshInfo[meshIdx].firstInstance;

    out_indirectCommands[meshIdx].indexCount    = in_meshInfo[meshIdx].indexCount;
    out_indirectCommands[meshIdx].firstIndex    = in_meshInfo[meshIdx].firstIndex;
    out_indirectCommands[meshIdx].vertexOffset  = in_meshInfo[meshIdx].vertexOffset;
    out_indirectCommands[meshIdx].firstInstance = in_meshInfo[meshIdx].firstInstance;

    const vec4 instancePos = vec4(in_instances[instanceIdx].posScale.xyz, 1.0);
    const float radius = in_meshInfo[meshIdx].radius * in_instances[instanceIdx].posScale.w;
    if (frustumCheck(instancePos, radius))
    {
        const uint idx = atomicAdd(out_indirectCommands[meshIdx].instanceCount, 1);
        out_renderData[firstInstance + idx].posScale = in_instances[instanceIdx].posScale;
        out_renderData[firstInstance + idx].quat     = in_instances[instanceIdx].quat;
    }
}
)";

constexpr static float CAMERA_FOV_DEG = 45.0f;
constexpr static float CAMERA_NEAR = 0.1f;
constexpr static float CAMERA_FAR = 5000.0f;

// TODO make these dynamic
constexpr static uint32 MAX_INDIRECT_COMMANDS = 100;
constexpr static uint32 MAX_INSTANCE_DATA = 1024 * 1024;

constexpr static size_t VERTEX_DATA_SIZE = 1024 * 1024 * sizeof(RendererVKLayout::MeshVertex);
constexpr static size_t INDEX_DATA_SIZE = 1024 * 1024 * sizeof(RendererVKLayout::MeshIndex);

struct alignas(16) UboData
{
    glm::mat4 mvp;
    Frustum frustum;
};

constexpr std::array<vk::ClearValue, 2> getClearValues()
{
    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{ std::array<float, 4> { 0.2f, 0.0f, 0.0f, 1.0f } };
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
    return clearValues;
}
constexpr static std::array<vk::ClearValue, 2> s_clearValues = getClearValues();

RendererVK::RendererVK() {}
RendererVK::~RendererVK() {}

bool RendererVK::initialize(Window& window, bool enableValidationLayers)
{
    // Disable layers we don't care about for now to eliminate potential issues
    _putenv("DISABLE_LAYER_NV_OPTIMUS_1=True");
    _putenv("DISABLE_VULKAN_OW_OVERLAY_LAYER=True");
    _putenv("DISABLE_VULKAN_OW_OBS_CAPTURE=True");
    _putenv("DISABLE_VULKAN_OBS_CAPTURE=True");

    glslang::InitializeProcess();

    VK::g_inst.initialize(window, enableValidationLayers);
    VK::g_inst.setBreakOnValidationLayerError(enableValidationLayers);
    VK::g_dev.initialize();
    m_surface.initialize(window);
    assert(m_surface.deviceSupportsSurface());

    m_swapChain.initialize(m_surface, NUM_FRAMES_IN_FLIGHT);
    m_stagingManager.initialize(m_swapChain);
    m_meshDataManager.initialize(m_stagingManager, VERTEX_DATA_SIZE, INDEX_DATA_SIZE);

    m_renderPass.initialize(m_swapChain);
    m_framebuffers.initialize(m_renderPass, m_swapChain);

    {
        GraphicsPipelineLayout graphicsPipelineLayout;
        graphicsPipelineLayout.vertexShaderText = vertexShaderText;
        graphicsPipelineLayout.fragmentShaderText = fragmentShaderText;
        graphicsPipelineLayout.vertexLayoutInfo = RendererVKLayout::getVertexLayoutInfo();
        graphicsPipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex
        });
        graphicsPipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment
        });
        m_graphicsPipeline.initialize(m_renderPass, graphicsPipelineLayout);
    }
    {
        ComputePipelineLayout computePipelineLayout;
        computePipelineLayout.computeShaderText = computeShaderText;
        computePipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
        });
        computePipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = 2,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
         });
        computePipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = 3,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
         });
        computePipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = 4,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex
         });
        computePipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = 5,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex
         });
        m_computePipeline.initialize(computePipelineLayout);
    }

    m_sampler.initialize();
    m_texture.initialize(m_stagingManager, "Textures/boat/color.dds");

    //Texture awa;
    //awa.initialize(m_stagingManager, "Textures/boat/color.dds");

    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.indirectDispatchBuffer.initialize(sizeof(vk::DispatchIndirectCommand),
            vk::BufferUsageFlagBits::eIndirectBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedDispatchBuffer = (vk::DispatchIndirectCommand*)perFrame.indirectDispatchBuffer.mapMemory().data();

        perFrame.uniformBuffer.initialize(sizeof(RendererVKLayout::Ubo),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedUniformBuffer = (RendererVKLayout::Ubo*)perFrame.uniformBuffer.mapMemory().data();

        perFrame.computeMeshInfoBuffer.initialize(MAX_INDIRECT_COMMANDS * sizeof(RendererVKLayout::MeshInfo),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedMeshInfo = (RendererVKLayout::MeshInfo*)perFrame.computeMeshInfoBuffer.mapMemory().data();

        perFrame.computeMeshInstanceBuffer.initialize(MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshInstance),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedMeshInstances = (RendererVKLayout::MeshInstance*)perFrame.computeMeshInstanceBuffer.mapMemory().data();

        perFrame.indirectCommandBuffer.initialize(MAX_INDIRECT_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand),
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.instanceDataBuffer.initialize(MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshTransform),
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    }

    return true;
}

void RendererVK::update(double deltaSec, const glm::mat4& viewMatrix, std::span<MeshInstance> instances)
{
    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::mat4x4 projection = glm::perspective(glm::radians(CAMERA_FOV_DEG), (float)extent.width / (float)extent.height, CAMERA_NEAR, CAMERA_FAR);

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    frameData.mappedUniformBuffer[0].mvp = projection * viewMatrix;
    frameData.mappedUniformBuffer[0].frustum.fromMatrix(frameData.mappedUniformBuffer[0].mvp);

    if (!frameData.updated)
    {    
        uint32 instanceCounter = 0;
        for (uint32 i = 0, num = (uint32)m_pMeshSet->size(); i < num; ++i)
        {
            Mesh& mesh = (*m_pMeshSet)[i];
            mesh.m_info.firstInstance = instanceCounter;
            instanceCounter += mesh.m_numInstances;
            assert(instanceCounter <= MAX_INSTANCE_DATA);
            memcpy(&frameData.mappedMeshInfo[i], &mesh.m_info, sizeof(RendererVKLayout::MeshInfo));
        }
        m_instanceCounter = instanceCounter;
        memcpy(frameData.mappedMeshInstances, instances.data(), instances.size() * sizeof(RendererVKLayout::MeshInstance));
        frameData.mappedDispatchBuffer[0] = vk::DispatchIndirectCommand{ .x = instanceCounter, .y = 1, .z = 1 };

        frameData.updated = true;
    }
    m_stagingManager.update();
}

void RendererVK::recordCommandBuffers()
{
    if (!m_pMeshSet)
        return;

    for (uint32 i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
    {
        const vk::Extent2D extent = m_swapChain.getLayout().extent;
        const vk::Viewport viewport{ .x = 0.0f, .y = (float)extent.height, .width = (float)extent.width, .height = -((float)extent.height), .minDepth = 0.0f, .maxDepth = 1.0f };
        const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = extent };

        const vk::RenderPassBeginInfo renderPassBeginInfo{
            .renderPass = m_renderPass.getRenderPass(),
            .framebuffer = m_framebuffers.getFramebuffer(i),
            .renderArea = vk::Rect2D {.offset = vk::Offset2D { 0, 0 }, .extent = extent },
            .clearValueCount = (uint32)s_clearValues.size(),
            .pClearValues = s_clearValues.data(),
        };

        PerFrameData& frameData = m_perFrameData[i];
        std::array<DescriptorSetUpdateInfo, 2> graphicsDescriptorSetUpdateInfos
        {
            DescriptorSetUpdateInfo{
                .binding = 1,
                .type = vk::DescriptorType::eCombinedImageSampler,
                .info = vk::DescriptorImageInfo {
                    .sampler = m_sampler.getSampler(),
                    .imageView = m_texture.getImageView(),
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 0,
                .type = vk::DescriptorType::eUniformBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.uniformBuffer.getBuffer(),
                    .range = sizeof(UboData),
                }
            }
        };
        std::array<DescriptorSetUpdateInfo, 5> computeDescriptorSetUpdateInfos
        {
            DescriptorSetUpdateInfo{
                .binding = 0,
                .type = vk::DescriptorType::eUniformBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.uniformBuffer.getBuffer(),
                    .range = sizeof(UboData),
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 2,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.computeMeshInstanceBuffer.getBuffer(),
                    .range = MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshInstance),
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 3,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.computeMeshInfoBuffer.getBuffer(),
                    .range = MAX_INDIRECT_COMMANDS * sizeof(RendererVKLayout::MeshInfo),
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 4,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.instanceDataBuffer.getBuffer(),
                    .range = MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshTransform),
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 5,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.indirectCommandBuffer.getBuffer(),
                    .range = MAX_INDIRECT_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand),
                }
            }
        };

        CommandBuffer& commandBuffer = m_swapChain.getCommandBuffer(i);
        vk::CommandBuffer vkCommandBuffer = commandBuffer.begin();

        {   // Compute shader frustum cull and indirect command buffer generation
            vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipeline());
            commandBuffer.cmdUpdateDescriptorSets(m_computePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, computeDescriptorSetUpdateInfos);
            vkCommandBuffer.fillBuffer(frameData.instanceDataBuffer.getBuffer(), 0, MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshTransform), 0);
            vkCommandBuffer.fillBuffer(frameData.indirectCommandBuffer.getBuffer(), 0, MAX_INDIRECT_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand), 0);
            vk::MemoryBarrier memoryBarrier{ .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eShaderRead };
            vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags::Flags(0), { memoryBarrier }, {}, {});
            vkCommandBuffer.dispatchIndirect(frameData.indirectDispatchBuffer.getBuffer(), 0);
            vk::MemoryBarrier memoryBarrier2{ .srcAccessMask = vk::AccessFlagBits::eShaderWrite, .dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead };
            vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlags::Flags(0), { memoryBarrier2 }, {}, {});
        }
        {   // Graphics pipeline draw
            vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, graphicsDescriptorSetUpdateInfos);
            vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
            vkCommandBuffer.setViewport(0, { viewport });
            vkCommandBuffer.setScissor(0, { scissor });
            vkCommandBuffer.bindVertexBuffers(0, { m_meshDataManager.getVertexBuffer().getBuffer() }, { 0 });
            vkCommandBuffer.bindVertexBuffers(1, { frameData.instanceDataBuffer.getBuffer() }, { 0 });
            vkCommandBuffer.bindIndexBuffer(m_meshDataManager.getIndexBuffer().getBuffer(), 0, vk::IndexType::eUint32);
            vkCommandBuffer.drawIndexedIndirect(frameData.indirectCommandBuffer.getBuffer(), 0, (uint32)m_pMeshSet->size(), sizeof(vk::DrawIndexedIndirectCommand));
            vkCommandBuffer.endRenderPass();
        }
        commandBuffer.end();
    }
}

void RendererVK::render()
{
    m_swapChain.acquireNextImage();
    m_swapChain.present();
}

void RendererVK::updateMeshSet(std::vector<Mesh>& meshData)
{
    assert(meshData.size() <= MAX_INDIRECT_COMMANDS);
    m_pMeshSet = &meshData;
    recordCommandBuffers();
}

const char* RendererVK::getDebugText()
{
    float vertexDataPercent = (float)(m_meshDataManager.getVertexBufUsed() / (float)m_meshDataManager.getVertexBufSize() * 100.0f);
    float indexDataPercent = (float)(m_meshDataManager.getIndexBufUsed() / (float)m_meshDataManager.getIndexBufSize()) * 100.0f;
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Vertex Capacity: %0.1f%%, Index Capacity: %0.1f%%", vertexDataPercent, indexDataPercent);
    return buffer;
}