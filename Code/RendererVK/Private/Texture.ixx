export module RendererVK.Texture;

import Core;
import RendererVK.VK;
import RendererVK.Device;

export class StagingManager;

export class Texture final
{
public:
    Texture();
    ~Texture();
    Texture(const Texture&) = delete;

    bool initialize(StagingManager& stagingManager, const char* pFilePath);

    vk::ImageView getImageView() const { return m_imageView; }
    vk::Image getImage() const { return m_image; }
    vk::Semaphore getImageReadySemaphore() const { return m_imageReadySemaphore; }

private:

    uint32 m_width = 0;
    uint32 m_height = 0;
    uint32 m_numChannels = 0;
    vk::Image m_image;
    vk::DeviceMemory m_imageMemory;
    vk::ImageView m_imageView;
    vk::Semaphore m_imageReadySemaphore;
};