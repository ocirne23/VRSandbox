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

const std::string vertexShaderText_PC_C = R"(
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
layout (location = 0) out vec2 out_uv;

void main()
{
	out_uv = in_uv;
	gl_Position = u_mvp * vec4(in_pos, 1.0);
}
)";

const std::string fragmentShaderText_C_C = R"(
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
constexpr static float CAMERA_FAR = 100.0f;

constexpr static uint32 NUM_FRAMES_IN_FLIGHT = 2;
constexpr static uint32 MAX_INDIRECT_COMMANDS = 10;

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

	m_instance.initialize(window, enableValidationLayers);
	m_instance.setBreakOnValidationLayerError(enableValidationLayers);
	m_surface.initialize(m_instance, window);
	m_device.initialize(m_instance);
	assert(m_surface.deviceSupportsSurface(m_device));

	m_swapChain.initialize(m_device, m_surface, NUM_FRAMES_IN_FLIGHT);
	m_renderPass.initialize(m_device, m_swapChain);
	m_framebuffers.initialize(m_device, m_renderPass, m_swapChain);
	
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
	m_pipeline.initialize(m_device, m_renderPass, pipelineLayout);
	m_uniformBuffer.initialize(m_device, sizeof(glm::mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	m_sampler.initialize(m_device);

	m_stagingManager.initialize(m_device);

	SceneData scene;
	scene.initialize("baseshapes.fbx");
	MeshData* pMesh = scene.getMesh("Boat");
	m_renderObject.initialize(m_device, m_stagingManager, *pMesh);

	CommandBuffer& commandBuffer = m_swapChain.getCurrentCommandBuffer();
	commandBuffer.initialize(m_device);
	m_texture.initialize(m_device, commandBuffer, "Textures/grid.png");

	m_indexedIndirectBuffer.initialize(m_device, MAX_INDIRECT_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand),
		vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, 
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	return true;
}

void RendererVK::update(const glm::mat4& viewMatrix)
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
	m_stagingManager.update(m_swapChain);
}

constexpr std::array<vk::ClearValue, 2> getClearValues()
{
	std::array<vk::ClearValue, 2> clearValues{};
	clearValues[0].color = vk::ClearColorValue{ std::array<float, 4> { 0.2f, 0.0f, 0.0f, 1.0f } };
	clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
	return clearValues;
}

void RendererVK::render()
{
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

	const uint32 imageIdx = m_swapChain.acquireNextImage();
	const vk::RenderPassBeginInfo renderPassBeginInfo{
		.renderPass = m_renderPass.getRenderPass(),
		.framebuffer = m_framebuffers.getFramebuffer(imageIdx),
		.renderArea = vk::Rect2D {
			.offset = vk::Offset2D { 0, 0 },
			.extent = extent,
		},
		.clearValueCount = (uint32)clearValues.size(),
		.pClearValues = clearValues.data(),
	};

	std::span<uint8> pUniformData = m_uniformBuffer.mapMemory();
	memcpy(pUniformData.data(), &m_mvpMatrix, sizeof(m_mvpMatrix));
	m_uniformBuffer.unmapMemory();

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
				.buffer = m_uniformBuffer.getBuffer(),
				.range = sizeof(m_mvpMatrix),
			}
		}
	};

	CommandBuffer& commandBuffer = m_swapChain.getCurrentCommandBuffer();
	vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
	vkCommandBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
	vkCommandBuffer.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
	commandBuffer.cmdUpdateDescriptorSets(m_pipeline.getPipelineLayout(), descriptorSetUpdateInfos);
	vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
	vkCommandBuffer.setViewport(0, { viewport });
	vkCommandBuffer.setScissor(0, { scissor });
	vkCommandBuffer.bindVertexBuffers(0, { m_renderObject.m_vertexBuffer.getBuffer() }, { 0 });
	vkCommandBuffer.bindIndexBuffer(m_renderObject.m_indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);
	vkCommandBuffer.drawIndexed(m_renderObject.getNumIndices(), 1, 0, 0, 0);
	vkCommandBuffer.endRenderPass();
	vkCommandBuffer.end();

	m_swapChain.present(imageIdx);
}