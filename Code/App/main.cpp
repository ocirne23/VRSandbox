#include <glm/glm.hpp>
#include <chrono>

import Core.Window;
import RendererVK.RendererVK;
import File.FileSystem;
import File.SceneData;
import Core.Allocator;
import Input.Input;

int main()
{
	FileSystem::initialize();

	Window window;
	window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(800, 600));

	Input input;
	input.initialize();

	RendererVK renderer;
	renderer.initialize(window, true);
	
	auto startTime = std::chrono::high_resolution_clock::now();
	double timeAccum = 0.0;
	uint32_t frameCount = 0;

	char windowTitleBuf[256];
	while (true)
	{
		const auto now = std::chrono::high_resolution_clock::now();
		const double deltaSec = std::chrono::duration<double>(now - startTime).count();
		startTime = now;

		input.update(deltaSec);
		renderer.update();
		renderer.render();
		frameCount++;

		timeAccum += deltaSec;
		if (timeAccum > 1.0f)
		{
			sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "FPS: %i Used Mem: %f mb", frameCount, (double)g_heapAllocator.getUsedSize() / 1024.0 / 1024.0);
			window.setTitle(windowTitleBuf);
			frameCount = 0;
			timeAccum = 0.0;
		}
	}

	return 0;
}