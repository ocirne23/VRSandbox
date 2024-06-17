export module RendererVK.Texture;

import RendererVK.VK;
import RendererVK.Device;

export class CommandBuffer;

export class Texture
{
public:
	Texture();
	~Texture();
	Texture(const Texture&) = delete;

	bool initialize(Device& device, CommandBuffer& commandBuffer, const char* pFilePath);

	vk::ImageView getImageView() const { return m_imageView; }
	vk::Image getImage() const { return m_image; }
	vk::Semaphore getImageReadySemaphore() const { return m_imageReadySemaphore; }

private:

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_numChannels = 0;
	vk::Image m_image;
	vk::DeviceMemory m_imageMemory;
	vk::ImageView m_imageView;
	vk::Semaphore m_imageReadySemaphore;
	vk::Device m_device;
};