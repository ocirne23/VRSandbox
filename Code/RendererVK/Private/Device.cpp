module RendererVK.Device;

import RendererVK.VK;
import RendererVK.Instance;

Device::Device() {}
Device::~Device()
{
	if (m_descriptorPool)
		m_device.destroyDescriptorPool(m_descriptorPool);
	if (m_commandPool)
		m_device.destroyCommandPool(m_commandPool);
	if (m_device)
		m_device.destroy();
}

constexpr static uint32_t DESCRIPTOR_POOL_NUM_UNIFORM_BUFFERS = 3;
constexpr static uint32_t DESCRIPTOR_POOL_NUM_COMBINED_IMAGE_SAMPLERS = 3;

bool Device::initialize(const Instance& inst)
{
	vk::Instance instance = inst.getHandle();
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

	std::vector<const char*> deviceExtensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	if (!supportsExtensions(deviceExtensions))
	{
		assert(false && "Physical device does not support the extension\n");
		return false;
	}

	m_graphicsQueueIndex = -1;
	std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_physicalDevice.getQueueFamilyProperties();
	for (uint32_t i = 0; i < queueFamilyProperties.size(); i++)
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

	const std::vector<const char*>& enabledLayers = inst.getEnabledLayers();
	vk::DeviceCreateInfo deviceCreateInfo{
		.pNext = &deviceFeatures,
		.queueCreateInfoCount = (uint32_t)deviceQueueCreateInfos.size(),
		.pQueueCreateInfos = deviceQueueCreateInfos.data(),
		.enabledLayerCount = (uint32_t)enabledLayers.size(),
		.ppEnabledLayerNames = enabledLayers.data(),
		.enabledExtensionCount = (uint32_t)deviceExtensions.size(),
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

	m_graphicsQueue = m_device.getQueue(m_graphicsQueueIndex, 0);
	if (!m_graphicsQueue)
	{
		assert(false && "Could not get the graphics queue\n");
		return false;
	}

	std::array<vk::DescriptorPoolSize, 2> descriptorPoolSizes
	{
		vk::DescriptorPoolSize {
			.type = vk::DescriptorType::eUniformBuffer,
			.descriptorCount = DESCRIPTOR_POOL_NUM_UNIFORM_BUFFERS
		},
		vk::DescriptorPoolSize {
			.type = vk::DescriptorType::eCombinedImageSampler,
			.descriptorCount = DESCRIPTOR_POOL_NUM_COMBINED_IMAGE_SAMPLERS
		},
	};
	vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo
	{
		.maxSets = 1,
		.poolSizeCount = (uint32_t)descriptorPoolSizes.size(),
		.pPoolSizes = descriptorPoolSizes.data(),
	};
	m_descriptorPool = m_device.createDescriptorPool(descriptorPoolCreateInfo);
	if (!m_descriptorPool)
	{
		assert(false && "Could not create a descriptor pool\n");
		return false;
	}
	return true;
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

uint32_t Device::findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags requirementsMask) const
{
	vk::PhysicalDeviceMemoryProperties memoryProperties = m_physicalDevice.getMemoryProperties();
	uint32_t typeIndex = uint32_t(~0);
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if ((typeBits & 1) && ((memoryProperties.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask))
		{
			typeIndex = i;
			break;
		}
		typeBits >>= 1;
	}
	assert(typeIndex != uint32_t(~0));
	return typeIndex;
}