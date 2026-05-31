module File.ISceneData;

import File.SceneData;
import File.ProceduralSceneData;

std::unique_ptr<ISceneData> ISceneData::createAssimpLoader()
{
	return std::make_unique<SceneData>();
}

std::unique_ptr<ISceneData> ISceneData::createProceduralLoader()
{
	return std::make_unique<ProceduralSceneData>();
}
