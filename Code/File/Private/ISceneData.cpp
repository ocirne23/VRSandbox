module File.ISceneData;

import File.SceneData;

std::unique_ptr<ISceneData> ISceneData::createAssimpLoader()
{
	return std::make_unique<SceneData>();
}