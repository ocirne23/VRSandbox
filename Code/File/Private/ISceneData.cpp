module File;

import Core;

import :SceneData;
import :ProceduralSceneData;

std::unique_ptr<ISceneData> ISceneData::createAssimpLoader()
{
	return std::make_unique<SceneData>();
}

std::unique_ptr<ISceneData> ISceneData::createProceduralLoader()
{
	return std::make_unique<ProceduralSceneData>();
}

std::unique_ptr<ISceneData> ISceneData::createMeshScene(const MeshGeometryDesc& geometry,
	const uint8* colorRGBA, uint32 colorWidth, uint32 colorHeight)
{
	auto scene = std::make_unique<ProceduralSceneData>();
	if (!scene->initializeFromMesh(geometry, colorRGBA, colorWidth, colorHeight))
		return nullptr;
	return scene;
}
