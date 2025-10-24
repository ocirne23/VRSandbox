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
    vk::Instance instance = Globals::instance.getInstance();
    // try to find the discrete GPU with the most memory
    auto enumResult = instance.enumeratePhysicalDevices();
    if (enumResult.result != vk::Result::eSuccess || enumResult.value.size() == 0)
    {
        assert(false && "Could not find any physical devices\n");
        return false;
    }
    for (vk::PhysicalDevice physDevice : enumResult.value)
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
    m_nonCoherentAtomSize = m_physicalDevice.getProperties().limits.nonCoherentAtomSize;

    std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    if (!supportsExtensions(deviceExtensions))
    {
        assert(false && "Physical device does not support the extension\n");
        return false;
    }

    m_graphicsQueueIndex = UINT32_MAX;
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
            if (!(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute))
            {
                assert(false && "Separate compute queue not supported!");
                return false;
            }
            break;
        }
    }
    if (m_graphicsQueueIndex == UINT32_MAX)
    {
        assert(false && "Could not find a graphicsQueue family index\n");
        return false;
    }

    const float queuePriorities[] = { 1.0f };
    std::array<vk::DeviceQueueCreateInfo, 1> deviceQueueCreateInfos{
        vk::DeviceQueueCreateInfo {.queueFamilyIndex = m_graphicsQueueIndex, .queueCount = 1, .pQueuePriorities = queuePriorities }
    };
    const std::vector<const char*>& enabledLayers = Globals::instance.getEnabledLayers();

    vk::PhysicalDeviceFeatures2 deviceFeatures;
    m_physicalDevice.getFeatures2(&deviceFeatures);
    deviceFeatures.features.samplerAnisotropy = vk::True;
    vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{
        .pNext = &deviceFeatures,
        //.shaderInputAttachmentArrayDynamicIndexing = vk::True,
        //.shaderUniformTexelBufferArrayDynamicIndexing = vk::True,
        //.shaderStorageTexelBufferArrayDynamicIndexing = vk::True,
        //.shaderUniformBufferArrayNonUniformIndexing = vk::True,
        .shaderSampledImageArrayNonUniformIndexing = vk::True,
        //.shaderStorageBufferArrayNonUniformIndexing = vk::True,
        //.shaderStorageImageArrayNonUniformIndexing = vk::True,
        //.shaderInputAttachmentArrayNonUniformIndexing = vk::True,
        //.shaderUniformTexelBufferArrayNonUniformIndexing = vk::True,
        //.shaderStorageTexelBufferArrayNonUniformIndexing = vk::True,
        //.descriptorBindingUniformBufferUpdateAfterBind = vk::True,
        //.descriptorBindingSampledImageUpdateAfterBind = vk::True,
        //.descriptorBindingStorageImageUpdateAfterBind = vk::True,
        //.descriptorBindingStorageBufferUpdateAfterBind = vk::True,
        //.descriptorBindingUniformTexelBufferUpdateAfterBind = vk::True,
        //.descriptorBindingStorageTexelBufferUpdateAfterBind = vk::True,
        //.descriptorBindingUpdateUnusedWhilePending = vk::True,
        //.descriptorBindingPartiallyBound = vk::True,
        .descriptorBindingVariableDescriptorCount = vk::True,
        .runtimeDescriptorArray = vk::True,
    };
    vk::PhysicalDeviceSynchronization2Features syncFeatures{
        .pNext = &descriptorIndexingFeatures,
        .synchronization2 = vk::True,
    };
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &syncFeatures,
        .queueCreateInfoCount = (uint32)deviceQueueCreateInfos.size(),
        .pQueueCreateInfos = deviceQueueCreateInfos.data(),
        .enabledLayerCount = (uint32)enabledLayers.size(),
        .ppEnabledLayerNames = enabledLayers.data(),
        .enabledExtensionCount = (uint32)deviceExtensions.size(),
        .ppEnabledExtensionNames = deviceExtensions.data(),
    };
    auto createResult = m_physicalDevice.createDevice(deviceCreateInfo);
    if (createResult.result != vk::Result::eSuccess)
    {
        assert(false && "Could not create a device\n");
        return false;
    }
    m_device = createResult.value;

    vk::CommandPoolCreateInfo poolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = m_graphicsQueueIndex,
    };
    auto commandPoolResult = m_device.createCommandPool(poolCreateInfo);
    if (commandPoolResult.result != vk::Result::eSuccess)
    {
        assert(false && "Could not create a command pool\n");
        return false;
    }
    m_commandPool = commandPoolResult.value;

    const uint32 poolSize = 1000;
    vk::DescriptorPoolSize poolSizes[] =
    {
        { vk::DescriptorType::eSampler, poolSize },
        { vk::DescriptorType::eCombinedImageSampler, poolSize },
        { vk::DescriptorType::eSampledImage, poolSize },
        { vk::DescriptorType::eStorageImage, poolSize },
        { vk::DescriptorType::eUniformTexelBuffer, poolSize },
        { vk::DescriptorType::eStorageTexelBuffer, poolSize },
        { vk::DescriptorType::eUniformBuffer, poolSize },
        { vk::DescriptorType::eStorageBuffer, poolSize },
        { vk::DescriptorType::eUniformBufferDynamic, poolSize },
        { vk::DescriptorType::eStorageBufferDynamic, poolSize },
        { vk::DescriptorType::eInputAttachment, poolSize }
    };
    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = poolSize * (uint32)(sizeof(poolSizes) / sizeof(poolSizes[0])),
        .poolSizeCount = (uint32)(sizeof(poolSizes) / sizeof(poolSizes[0])),
        .pPoolSizes = poolSizes,
    };
    auto descriptorPoolResult = m_device.createDescriptorPool(descriptorPoolCreateInfo);
    if (descriptorPoolResult.result != vk::Result::eSuccess)
    {
        assert(false && "Could not create a descriptor pool\n");
        return false;
    }
    m_descriptorPool = descriptorPoolResult.value;

    pfVkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)m_device.getProcAddr("vkCmdPushDescriptorSetKHR");

    m_graphicsQueue = m_device.getQueue(m_graphicsQueueIndex, 0);
    if (!m_graphicsQueue)
    {
        assert(false && "Could not get the graphics queue\n");
        return false;
    }

    std::vector<vk::Format> candidates = {
         vk::Format::eUndefined                                ,
         vk::Format::eR4G4UnormPack8                           ,
         vk::Format::eR4G4B4A4UnormPack16                      ,
         vk::Format::eB4G4R4A4UnormPack16                      ,
         vk::Format::eR5G6B5UnormPack16                        ,
         vk::Format::eB5G6R5UnormPack16                        ,
         vk::Format::eR5G5B5A1UnormPack16                      ,
         vk::Format::eB5G5R5A1UnormPack16                      ,
         vk::Format::eA1R5G5B5UnormPack16                      ,
         vk::Format::eR8Unorm                                  ,
         vk::Format::eR8Snorm                                  ,
         vk::Format::eR8Uscaled                                ,
         vk::Format::eR8Sscaled                                ,
         vk::Format::eR8Uint                                   ,
         vk::Format::eR8Sint                                   ,
         vk::Format::eR8Srgb                                   ,
         vk::Format::eR8G8Unorm                                ,
         vk::Format::eR8G8Snorm                                ,
         vk::Format::eR8G8Uscaled                              ,
         vk::Format::eR8G8Sscaled                              ,
         vk::Format::eR8G8Uint                                 ,
         vk::Format::eR8G8Sint                                 ,
         vk::Format::eR8G8Srgb                                 ,
         vk::Format::eR8G8B8Unorm                              ,
         vk::Format::eR8G8B8Snorm                              ,
         vk::Format::eR8G8B8Uscaled                            ,
         vk::Format::eR8G8B8Sscaled                            ,
         vk::Format::eR8G8B8Uint                               ,
         vk::Format::eR8G8B8Sint                               ,
         vk::Format::eR8G8B8Srgb                               ,
         vk::Format::eB8G8R8Unorm                              ,
         vk::Format::eB8G8R8Snorm                              ,
         vk::Format::eB8G8R8Uscaled                            ,
         vk::Format::eB8G8R8Sscaled                            ,
         vk::Format::eB8G8R8Uint                               ,
         vk::Format::eB8G8R8Sint                               ,
         vk::Format::eB8G8R8Srgb                               ,
         vk::Format::eR8G8B8A8Unorm                            ,
         vk::Format::eR8G8B8A8Snorm                            ,
         vk::Format::eR8G8B8A8Uscaled                          ,
         vk::Format::eR8G8B8A8Sscaled                          ,
         vk::Format::eR8G8B8A8Uint                             ,
         vk::Format::eR8G8B8A8Sint                             ,
         vk::Format::eR8G8B8A8Srgb                             ,
         vk::Format::eB8G8R8A8Unorm                            ,
         vk::Format::eB8G8R8A8Snorm                            ,
         vk::Format::eB8G8R8A8Uscaled                          ,
         vk::Format::eB8G8R8A8Sscaled                          ,
         vk::Format::eB8G8R8A8Uint                             ,
         vk::Format::eB8G8R8A8Sint                             ,
         vk::Format::eB8G8R8A8Srgb                             ,
         vk::Format::eA8B8G8R8UnormPack32                      ,
         vk::Format::eA8B8G8R8SnormPack32                      ,
         vk::Format::eA8B8G8R8UscaledPack32                    ,
         vk::Format::eA8B8G8R8SscaledPack32                    ,
         vk::Format::eA8B8G8R8UintPack32                       ,
         vk::Format::eA8B8G8R8SintPack32                       ,
         vk::Format::eA8B8G8R8SrgbPack32                       ,
         vk::Format::eA2R10G10B10UnormPack32                   ,
         vk::Format::eA2R10G10B10SnormPack32                   ,
         vk::Format::eA2R10G10B10UscaledPack32                 ,
         vk::Format::eA2R10G10B10SscaledPack32                 ,
         vk::Format::eA2R10G10B10UintPack32                    ,
         vk::Format::eA2R10G10B10SintPack32                    ,
         vk::Format::eA2B10G10R10UnormPack32                   ,
         vk::Format::eA2B10G10R10SnormPack32                   ,
         vk::Format::eA2B10G10R10UscaledPack32                 ,
         vk::Format::eA2B10G10R10SscaledPack32                 ,
         vk::Format::eA2B10G10R10UintPack32                    ,
         vk::Format::eA2B10G10R10SintPack32                    ,
         vk::Format::eR16Unorm                                 ,
         vk::Format::eR16Snorm                                 ,
         vk::Format::eR16Uscaled                               ,
         vk::Format::eR16Sscaled                               ,
         vk::Format::eR16Uint                                  ,
         vk::Format::eR16Sint                                  ,
         vk::Format::eR16Sfloat                                ,
         vk::Format::eR16G16Unorm                              ,
         vk::Format::eR16G16Snorm                              ,
         vk::Format::eR16G16Uscaled                            ,
         vk::Format::eR16G16Sscaled                            ,
         vk::Format::eR16G16Uint                               ,
         vk::Format::eR16G16Sint                               ,
         vk::Format::eR16G16Sfloat                             ,
         vk::Format::eR16G16B16Unorm                           ,
         vk::Format::eR16G16B16Snorm                           ,
         vk::Format::eR16G16B16Uscaled                         ,
         vk::Format::eR16G16B16Sscaled                         ,
         vk::Format::eR16G16B16Uint                            ,
         vk::Format::eR16G16B16Sint                            ,
         vk::Format::eR16G16B16Sfloat                          ,
         vk::Format::eR16G16B16A16Unorm                        ,
         vk::Format::eR16G16B16A16Snorm                        ,
         vk::Format::eR16G16B16A16Uscaled                      ,
         vk::Format::eR16G16B16A16Sscaled                      ,
         vk::Format::eR16G16B16A16Uint                         ,
         vk::Format::eR16G16B16A16Sint                         ,
         vk::Format::eR16G16B16A16Sfloat                       ,
         vk::Format::eR32Uint                                  ,
         vk::Format::eR32Sint                                  ,
         vk::Format::eR32Sfloat                                ,
         vk::Format::eR32G32Uint                               ,
         vk::Format::eR32G32Sint                               ,
         vk::Format::eR32G32Sfloat                             ,
         vk::Format::eR32G32B32Uint                            ,
         vk::Format::eR32G32B32Sint                            ,
         vk::Format::eR32G32B32Sfloat                          ,
         vk::Format::eR32G32B32A32Uint                         ,
         vk::Format::eR32G32B32A32Sint                         ,
         vk::Format::eR32G32B32A32Sfloat                       ,
         vk::Format::eR64Uint                                  ,
         vk::Format::eR64Sint                                  ,
         vk::Format::eR64Sfloat                                ,
         vk::Format::eR64G64Uint                               ,
         vk::Format::eR64G64Sint                               ,
         vk::Format::eR64G64Sfloat                             ,
         vk::Format::eR64G64B64Uint                            ,
         vk::Format::eR64G64B64Sint                            ,
         vk::Format::eR64G64B64Sfloat                          ,
         vk::Format::eR64G64B64A64Uint                         ,
         vk::Format::eR64G64B64A64Sint                         ,
         vk::Format::eR64G64B64A64Sfloat                       ,
         vk::Format::eB10G11R11UfloatPack32                    ,
         vk::Format::eE5B9G9R9UfloatPack32                     ,
         vk::Format::eD16Unorm                                 ,
         vk::Format::eX8D24UnormPack32                         ,
         vk::Format::eD32Sfloat                                ,
         vk::Format::eS8Uint                                   ,
         vk::Format::eD16UnormS8Uint                           ,
         vk::Format::eD24UnormS8Uint                           ,
         vk::Format::eD32SfloatS8Uint                          ,
         vk::Format::eBc1RgbUnormBlock                         ,
         vk::Format::eBc1RgbSrgbBlock                          ,
         vk::Format::eBc1RgbaUnormBlock                        ,
         vk::Format::eBc1RgbaSrgbBlock                         ,
         vk::Format::eBc2UnormBlock                            ,
         vk::Format::eBc2SrgbBlock                             ,
         vk::Format::eBc3UnormBlock                            ,
         vk::Format::eBc3SrgbBlock                             ,
         vk::Format::eBc4UnormBlock                            ,
         vk::Format::eBc4SnormBlock                            ,
         vk::Format::eBc5UnormBlock                            ,
         vk::Format::eBc5SnormBlock                            ,
         vk::Format::eBc6HUfloatBlock                          ,
         vk::Format::eBc6HSfloatBlock                          ,
         vk::Format::eBc7UnormBlock                            ,
         vk::Format::eBc7SrgbBlock                             ,
         vk::Format::eEtc2R8G8B8UnormBlock                     ,
         vk::Format::eEtc2R8G8B8SrgbBlock                      ,
         vk::Format::eEtc2R8G8B8A1UnormBlock                   ,
         vk::Format::eEtc2R8G8B8A1SrgbBlock                    ,
         vk::Format::eEtc2R8G8B8A8UnormBlock                   ,
         vk::Format::eEtc2R8G8B8A8SrgbBlock                    ,
         vk::Format::eEacR11UnormBlock                         ,
         vk::Format::eEacR11SnormBlock                         ,
         vk::Format::eEacR11G11UnormBlock                      ,
         vk::Format::eEacR11G11SnormBlock                      ,
         vk::Format::eAstc4x4UnormBlock                        ,
         vk::Format::eAstc4x4SrgbBlock                         ,
         vk::Format::eAstc5x4UnormBlock                        ,
         vk::Format::eAstc5x4SrgbBlock                         ,
         vk::Format::eAstc5x5UnormBlock                        ,
         vk::Format::eAstc5x5SrgbBlock                         ,
         vk::Format::eAstc6x5UnormBlock                        ,
         vk::Format::eAstc6x5SrgbBlock                         ,
         vk::Format::eAstc6x6UnormBlock                        ,
         vk::Format::eAstc6x6SrgbBlock                         ,
         vk::Format::eAstc8x5UnormBlock                        ,
         vk::Format::eAstc8x5SrgbBlock                         ,
         vk::Format::eAstc8x6UnormBlock                        ,
         vk::Format::eAstc8x6SrgbBlock                         ,
         vk::Format::eAstc8x8UnormBlock                        ,
         vk::Format::eAstc8x8SrgbBlock                         ,
         vk::Format::eAstc10x5UnormBlock                       ,
         vk::Format::eAstc10x5SrgbBlock                        ,
         vk::Format::eAstc10x6UnormBlock                       ,
         vk::Format::eAstc10x6SrgbBlock                        ,
         vk::Format::eAstc10x8UnormBlock                       ,
         vk::Format::eAstc10x8SrgbBlock                        ,
         vk::Format::eAstc10x10UnormBlock                      ,
         vk::Format::eAstc10x10SrgbBlock                       ,
         vk::Format::eAstc12x10UnormBlock                      ,
         vk::Format::eAstc12x10SrgbBlock                       ,
         vk::Format::eAstc12x12UnormBlock                      ,
         vk::Format::eAstc12x12SrgbBlock
    };

    for (vk::Format candidate : candidates)
    {
        auto imageFormatPropertiesresult = m_physicalDevice.getImageFormatProperties(candidate, vk::ImageType::e2D, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        if (imageFormatPropertiesresult.result == vk::Result::eSuccess)
        {
            m_supported2DOptimalFormats.push_back(candidate);
        }
    }

    return true;
}

void Device::destroy()
{
    if (m_device)
    {
        auto waitResult = m_device.waitIdle();
        if (waitResult != vk::Result::eSuccess)
        {
            assert(false && "Failed to wait for device idle");
        }
        m_device.destroyCommandPool(m_commandPool);
        m_device.destroy();
    }
    m_device = nullptr;
}

bool Device::supportsExtensions(std::vector<const char*> extensions)
{
    auto enumResult = m_physicalDevice.enumerateDeviceExtensionProperties();
    if (enumResult.result != vk::Result::eSuccess)
    {
        assert(false && "Could not enumerate device extension properties\n");
        return false;
    }
    std::vector<vk::ExtensionProperties> extensionProperties = enumResult.value;
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