module RendererVK;

import Core;
import RendererVK.VK;
import RendererVK.glslang;
import Core.Window;
import RendererVK.RenderObject;
import RendererVK.Texture;
import RendererVK.Pipeline;
import File.SceneData;
import File.MeshData;

const char* vertexShaderText_PC_C = R"(
#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (std140, binding = 0) uniform buffer
{
	mat4 u_mvp;
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

const char* fragmentShaderText_C_C = R"(
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

constexpr static float CAMERA_FOV_DEG = 45.0f;
constexpr static float CAMERA_NEAR = 0.1f;
constexpr static float CAMERA_FAR = 1000.0f;

constexpr static uint32 NUM_FRAMES_IN_FLIGHT = 2;
constexpr static uint32 MAX_INDIRECT_COMMANDS = 10;

constexpr static size_t VERTEX_DATA_SIZE = 3 * 1024 * sizeof(RenderObject::VertexLayout);
constexpr static size_t INDEX_DATA_SIZE = 5 * 1024 * sizeof(RenderObject::IndexLayout);

constexpr static double VERTEX_DATA_SIZE_KB = VERTEX_DATA_SIZE / 1024.0;
constexpr static double INDEX_DATA_SIZE_KB = INDEX_DATA_SIZE / 1024.0;

constexpr static double VERTEX_DATA_SIZE_MB = VERTEX_DATA_SIZE / 1024.0 / 1024.0;
constexpr static double INDEX_DATA_SIZE_MB = INDEX_DATA_SIZE / 1024.0 / 1024.0;

constexpr bool DEVICE_LOCAL_PER_FRAME = false;

RendererVK::RendererVK() {}
RendererVK::~RendererVK() 
{
	VK::g_dev.getDevice().waitIdle();
}

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
	m_surface.initialize(window);
	VK::g_dev.initialize();
	assert(m_surface.deviceSupportsSurface());

	m_swapChain.initialize(m_surface, NUM_FRAMES_IN_FLIGHT);
	m_stagingManager.initialize(m_swapChain);
	m_meshDataManager.initialize(m_stagingManager, VERTEX_DATA_SIZE, INDEX_DATA_SIZE);

	m_renderPass.initialize(m_swapChain);
	m_framebuffers.initialize(m_renderPass, m_swapChain);
	
	PipelineLayout pipelineLayout;
	pipelineLayout.numUniformBuffers = 1;
	pipelineLayout.vertexShaderText = vertexShaderText_PC_C;
	pipelineLayout.fragmentShaderText = fragmentShaderText_C_C;
	pipelineLayout.vertexLayoutInfo = RenderObject::getVertexLayoutInfo();
	pipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
		.binding = 0,
		.descriptorType = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = 1,
		.stageFlags = vk::ShaderStageFlagBits::eVertex
	});
	pipelineLayout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
		.binding = 1,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 1,
		.stageFlags = vk::ShaderStageFlagBits::eFragment
	});
	m_pipeline.initialize(m_renderPass, pipelineLayout);
	m_sampler.initialize();

	SceneData scene;
	scene.initialize("baseshapes.fbx");
	m_renderObject.initialize(m_meshDataManager, *scene.getMesh("Boat"));
	m_renderObject2.initialize(m_meshDataManager, *scene.getMesh("BoatCollider"));
	m_texture.initialize(m_stagingManager, "Textures/grid.png");

	for (uint32 i = 0; i < m_swapChain.getLayout().numImages; ++i)
	{
		if constexpr (DEVICE_LOCAL_PER_FRAME)
		{
			m_uniformBuffers.emplace_back().initialize(sizeof(glm::mat4),
				vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
				vk::MemoryPropertyFlagBits::eDeviceLocal);

			m_indirectCommandBuffers.emplace_back().initialize(MAX_INDIRECT_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand),
				vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst,
				vk::MemoryPropertyFlagBits::eDeviceLocal);

			m_instanceDataBuffers.emplace_back().initialize(MAX_INDIRECT_COMMANDS * sizeof(RenderObject::InstanceData),
				vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
				vk::MemoryPropertyFlagBits::eDeviceLocal);
		}
		else
		{
			m_uniformBuffers.emplace_back().initialize(sizeof(glm::mat4),
				vk::BufferUsageFlagBits::eUniformBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

			m_indirectCommandBuffers.emplace_back().initialize(MAX_INDIRECT_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand),
				vk::BufferUsageFlagBits::eIndirectBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

			m_instanceDataBuffers.emplace_back().initialize(MAX_INDIRECT_COMMANDS * sizeof(RenderObject::InstanceData),
				vk::BufferUsageFlagBits::eVertexBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		}

	}

	recordCommandBuffers();

	return true;
}

void RendererVK::update(double deltaSec, const glm::mat4& viewMatrix)
{
	const vk::Extent2D extent = m_swapChain.getLayout().extent;
	const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	const glm::mat4x4 projection = glm::perspective(glm::radians(CAMERA_FOV_DEG), aspect, CAMERA_NEAR, CAMERA_FAR);
	// vulkan clip space has inverted y and half z !
	const static glm::mat4x4 clip = glm::mat4x4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f);

	m_mvpMatrix = clip * projection * viewMatrix;

	std::vector<vk::DrawIndexedIndirectCommand> indirectCommands;
	std::vector<RenderObject::InstanceData> instanceData;
	indirectCommands.push_back(vk::DrawIndexedIndirectCommand{
		.indexCount = m_renderObject.getNumIndices(),
		.instanceCount = 5,
		.firstIndex = m_renderObject.getIndexOffset(),
		.vertexOffset = (int32)m_renderObject.getVertexOffset(),
		.firstInstance = 0
	});
	indirectCommands.push_back(vk::DrawIndexedIndirectCommand{
		.indexCount = m_renderObject2.getNumIndices(),
		.instanceCount = 5,
		.firstIndex = m_renderObject2.getIndexOffset(),
		.vertexOffset = (int32)m_renderObject2.getVertexOffset(),
		.firstInstance = 5
	});
	for (uint32 i = 0; i < MAX_INDIRECT_COMMANDS; ++i)
	{
		RenderObject::InstanceData data{
			.pos = glm::vec3((float)i * 10, 0.0f, 0.0f),
			.scale = 1.0f + 0.2f * i,
			.rot = glm::quat()
		};
		instanceData.push_back(data);
	}

	m_uniformBuffers[m_swapChain.getCurrentFrameIndex()].mapMemory(&m_mvpMatrix, sizeof(m_mvpMatrix));
	m_indirectCommandBuffers[m_swapChain.getCurrentFrameIndex()].mapMemory(indirectCommands.data(), indirectCommands.size() * sizeof(indirectCommands[0]));
	m_instanceDataBuffers[m_swapChain.getCurrentFrameIndex()].mapMemory(instanceData.data(), instanceData.size() * sizeof(instanceData[0]));

	m_stagingManager.update();
}

constexpr std::array<vk::ClearValue, 2> getClearValues()
{
	std::array<vk::ClearValue, 2> clearValues{};
	clearValues[0].color = vk::ClearColorValue{ std::array<float, 4> { 0.2f, 0.0f, 0.0f, 1.0f } };
	clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
	return clearValues;
}

void RendererVK::recordCommandBuffers()
{
	for (uint32 i = 0; i < m_swapChain.getLayout().numImages; ++i)
	{
		CommandBuffer& commandBuffer = m_swapChain.getCommandBuffer(i);
		constexpr static std::array<vk::ClearValue, 2> clearValues = getClearValues();

		const vk::Extent2D extent = m_swapChain.getLayout().extent;
		const vk::Viewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = (float)extent.width,
			.height = (float)extent.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};
		const vk::Rect2D scissor{
			.offset = vk::Offset2D{ 0, 0 },
			.extent = extent
		};

		std::array<DescriptorSetUpdateInfo, 2> descriptorSetUpdateInfos
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
					.buffer = m_uniformBuffers[i].getBuffer(),
					.range = sizeof(m_mvpMatrix),
				}
			}
		};

		const vk::RenderPassBeginInfo renderPassBeginInfo{
			.renderPass = m_renderPass.getRenderPass(),
			.framebuffer = m_framebuffers.getFramebuffer(i),
			.renderArea = vk::Rect2D {
				.offset = vk::Offset2D { 0, 0 },
				.extent = extent,
			},
			.clearValueCount = (uint32)clearValues.size(),
			.pClearValues = clearValues.data(),
		};

		vk::CommandBuffer vkCommandBuffer = commandBuffer.begin();

		vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
		commandBuffer.cmdUpdateDescriptorSets(m_pipeline.getPipelineLayout(), descriptorSetUpdateInfos);
		vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
		vkCommandBuffer.setViewport(0, { viewport });
		vkCommandBuffer.setScissor(0, { scissor });
		vkCommandBuffer.bindVertexBuffers(0, { m_meshDataManager.getVertexBuffer().getBuffer() }, { 0 });
		vkCommandBuffer.bindVertexBuffers(1, { m_instanceDataBuffers[i].getBuffer() }, { 0 });
		vkCommandBuffer.bindIndexBuffer(m_meshDataManager.getIndexBuffer().getBuffer(), 0, vk::IndexType::eUint32);
		vkCommandBuffer.drawIndexedIndirect(m_indirectCommandBuffers[i].getBuffer(), 0, 2, sizeof(vk::DrawIndexedIndirectCommand));
		vkCommandBuffer.endRenderPass();

		commandBuffer.end();
	}
}

void RendererVK::render()
{
	m_swapChain.acquireNextImage();
	m_swapChain.present();
}

const char* RendererVK::getDebugText()
{
	float vertexDataPercent = (float)(m_meshDataManager.getVertexBufUsed() / (float)m_meshDataManager.getVertexBufSize() * 100.0f);
	float indexDataPercent  = (float)(m_meshDataManager.getIndexBufUsed() / (float)m_meshDataManager.getIndexBufSize()) * 100.0f;
	static char buffer[256];
	snprintf(buffer, sizeof(buffer), "Vertex Capacity: %0.1f%%, Index Capacity: %0.1f%%", vertexDataPercent, indexDataPercent);
	return buffer;
}