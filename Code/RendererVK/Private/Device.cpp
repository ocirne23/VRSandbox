module RendererVK.Device;

import RendererVK.VK;
import RendererVK.Instance;

Device::Device() {}
Device::~Device()
{
	destroy();
}

bool Device::initialize()
{
	vk::Instance instance = VK::g_inst.getInstance();
	// try to find the discrete GPU with the most memory
	for (vk::PhysicalDevice physDevice : instance.enumeratePhysicalDevices())
	{
		if (!m_physicalDevice)
			m_physicalDevice = physDevice;
		else if (physDevice.getProperties().limits.maxMemoryAllocationCount > m_physicalDevice.getProperties().limits.maxMemoryAllocationCount 
			&& physDevice.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu 
			|| m_physicalDevice.getProperties().deviceType != vk::PhysicalDeviceType::eDiscreteGpu)
		{
			m_physicalDevice = physDevice;
		}
	}
	printf("Device: %s\n", m_physicalDevice.getProperties().deviceName.data());

	std::vector<const char*> deviceExtensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
	if (!supportsExtensions(deviceExtensions))
	{
		assert(false && "Physical device does not support the extension\n");
		return false;
	}

	m_graphicsQueueIndex = -1;
	std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_physicalDevice.getQueueFamilyProperties();
	for (uint32 i = 0; i < queueFamilyProperties.size(); i++)
	{
		if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			m_graphicsQueueIndex = i;
			if (!(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eTransfer))
			{
				assert(false && "Separate transfer queue not supported!");
				return false;
			}
			break;
		}
	}
	if (m_graphicsQueueIndex == -1)
	{
		assert(false && "Could not find a graphicsQueue family index\n");
		return false;
	}

	const float queuePriorities[] = { 1.0f };
	std::array<vk::DeviceQueueCreateInfo, 1> deviceQueueCreateInfos {
		vk::DeviceQueueCreateInfo { .queueFamilyIndex = m_graphicsQueueIndex, .queueCount = 1, .pQueuePriorities = queuePriorities }
	};

	vk::PhysicalDeviceSynchronization2Features syncFeatures {
		.synchronization2 = vk::True,
	};
	vk::PhysicalDeviceFeatures2 deviceFeatures {
		.pNext = &syncFeatures,
		.features = vk::PhysicalDeviceFeatures {
			.samplerAnisotropy = vk::True,
		},
	};
	m_physicalDevice.getFeatures2(&deviceFeatures);

	const std::vector<const char*>& enabledLayers = VK::g_inst.getEnabledLayers();
	vk::DeviceCreateInfo deviceCreateInfo{
		.pNext = &deviceFeatures,
		.queueCreateInfoCount = (uint32)deviceQueueCreateInfos.size(),
		.pQueueCreateInfos = deviceQueueCreateInfos.data(),
		.enabledLayerCount = (uint32)enabledLayers.size(),
		.ppEnabledLayerNames = enabledLayers.data(),
		.enabledExtensionCount = (uint32)deviceExtensions.size(),
		.ppEnabledExtensionNames = deviceExtensions.data(),
	};
	m_device = m_physicalDevice.createDevice(deviceCreateInfo);
	if (!m_device)
	{
		assert(false && "Could not create a device\n");
		return false;
	}
	vk::CommandPoolCreateInfo poolCreateInfo{ 
		.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, 
		.queueFamilyIndex = m_graphicsQueueIndex,
	};
	m_commandPool = m_device.createCommandPool(poolCreateInfo);
	if (!m_commandPool)
	{
		assert(false && "Could not create a command pool\n");
		return false;
	}

	pfVkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)m_device.getProcAddr("vkCmdPushDescriptorSetKHR");

	m_graphicsQueue = m_device.getQueue(m_graphicsQueueIndex, 0);
	if (!m_graphicsQueue)
	{
		assert(false && "Could not get the graphics queue\n");
		return false;
	}
	return true;
}

void Device::destroy()
{
	if (m_device)
	{
		m_device.destroyCommandPool(m_commandPool);
		m_device.destroy();
	}
	m_device = nullptr;
}

bool Device::supportsExtensions(std::vector<const char*> extensions)
{
	std::vector<vk::ExtensionProperties> extensionProperties = m_physicalDevice.enumerateDeviceExtensionProperties();
	for (const char* deviceExtension : extensions)
	{
		bool found = false;
		for (vk::ExtensionProperties extensionProperty : extensionProperties)
		{
			if (strcmp(deviceExtension, extensionProperty.extensionName) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			return false;
		}
	}
	return true;
}

uint32 Device::findMemoryType(uint32 typeBits, vk::MemoryPropertyFlags requirementsMask) const
{
	vk::PhysicalDeviceMemoryProperties memoryProperties = m_physicalDevice.getMemoryProperties();
	uint32 typeIndex = uint32(~0);
	for (uint32 i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if ((typeBits & 1) && ((memoryProperties.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask))
		{
			typeIndex = i;
			break;
		}
		typeBits >>= 1;
	}
	assert(typeIndex != uint32(~0));
	return typeIndex;
}