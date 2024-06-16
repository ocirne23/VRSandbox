module;

#include <assimp/Importer.hpp>
#include <vector>

export module File.SceneData;

import File.MeshData;
import File.TextureData;
import File.MaterialData;

export struct aiScene;

export class SceneData
{
public:
	SceneData();
	~SceneData();
	SceneData(const SceneData&) = delete;
	SceneData(SceneData&&) = default;

	bool initialize(const char* fileName);
	std::vector<MeshData>& getMeshes() { return m_meshes; }
	MeshData* getMesh(const char* pMeshName);
	MaterialData& getMaterial(uint32_t materialIdx);

private:

	Assimp::Importer m_importer;
	const aiScene* m_pScene = nullptr;
	std::vector<MeshData> m_meshes;
	std::vector<TextureData> m_textures;
	std::vector<MaterialData> m_materials;
};