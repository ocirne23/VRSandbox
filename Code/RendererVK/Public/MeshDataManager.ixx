export module RendererVK.MeshDataManager;

import Core;

import RendererVK.VK;
import RendererVK.Device;
import RendererVK.StagingManager;

class MeshDataManager final
{
public:

	MeshDataManager();
	~MeshDataManager();
	MeshDataManager(const MeshDataManager&) = delete;

	bool initialize(StagingManager& stagingManager);
};