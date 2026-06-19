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
