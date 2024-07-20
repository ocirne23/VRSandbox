module RendererVK.RenderPass;

import RendererVK.VK;
import RendererVK.Device;
import RendererVK.SwapChain;

RenderPass::RenderPass() {}
RenderPass::~RenderPass() 
{
    if (m_renderPass)
        VK::g_dev.getDevice().destroyRenderPass(m_renderPass);
}

bool RenderPass::initialize(const SwapChain& swapChain)
{
    std::array<vk::AttachmentDescription, 2> attachments;
    attachments[0] = {
        .flags = vk::AttachmentDescriptionFlags(),
        .format = swapChain.getLayout().format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::ePresentSrcKHR,
    };
    attachments[1] = {
		.flags = vk::AttachmentDescriptionFlags(),
		.format = vk::Format::eD32Sfloat,
        .samples = vk::SampleCountFlagBits::e1,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eDontCare,
		.initialLayout = vk::ImageLayout::eUndefined,
		.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
	};
    vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference depthRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::SubpassDescription subpass {
		.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorRef,
		.pDepthStencilAttachment = &depthRef,
	};
    vk::PipelineStageFlags depthStages = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
    std::array<vk::SubpassDependency, 2> dependencies;
    dependencies[0] = vk::SubpassDependency{
		.srcSubpass = vk::SubpassExternal,
		.dstSubpass = 0,
		.srcStageMask = depthStages,
		.dstStageMask = depthStages,
		.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
	};
    dependencies[1] = vk::SubpassDependency {
        .srcSubpass = vk::SubpassExternal,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,// | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,// | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .srcAccessMask = vk::AccessFlags(),
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
    };
    vk::RenderPassCreateInfo renderPassInfo {
		.attachmentCount = static_cast<uint32>(attachments.size()),
		.pAttachments = attachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = (uint32)dependencies.size(),
		.pDependencies = dependencies.data(),
	};

    m_renderPass = VK::g_dev.getDevice().createRenderPass(renderPassInfo);
    if (!m_renderPass)
    {
        assert(false && "Failed to create renderpass\n");
        return false;
    }

	return true;
}