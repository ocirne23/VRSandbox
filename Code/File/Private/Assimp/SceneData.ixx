export module File:SceneData;

import Core;

import File.fwd;
import :ISceneData;
import :Assimp;
import :MeshData;
import :TextureData;
import :MaterialData;
import :NodeData;

export std::unique_ptr<ISceneData> createAssimpLoader();

export class SceneData final : public ISceneData
{
public:
	SceneData();
	~SceneData();
	SceneData(const SceneData&) = delete;
	SceneData(SceneData&&) = default;

	bool initialize(const char* filePath, bool mergeNodes, bool preTransformVertices) override;
	bool isValid() const override { return m_pScene != nullptr; }

	const std::string& getFilePath() const override { return m_filePath; }
	const INodeData& getRootNode() const override { return m_rootNode; }

	const IMeshData* getMesh(const char* pMeshName) const override;

	virtual uint32 getNumMeshes() const override { return static_cast<uint32>(m_meshes.size()); }
	virtual uint32 getNumMaterials() const override { return static_cast<uint32>(m_materials.size()); }
	virtual uint32 getNumTextures() const override { return static_cast<uint32>(m_textures.size()); }

	virtual const IMeshData* getMesh(uint32 idx) const override { assert(idx < m_meshes.size()); return &m_meshes[idx]; }
	virtual const IMaterialData* getMaterial(uint32 idx) const override { assert(idx < m_materials.size()); return &m_materials[idx]; }
	virtual const ITextureData* getTexture(uint32 idx) const override { assert(idx < m_textures.size()); return &m_textures[idx]; }

private:

	std::string m_filePath;
	Assimp::Importer m_importer;
	const aiScene* m_pScene = nullptr;
	std::vector<MeshData> m_meshes;
	std::vector<TextureData> m_textures;
	std::vector<MaterialData> m_materials;
	NodeData m_rootNode;
};