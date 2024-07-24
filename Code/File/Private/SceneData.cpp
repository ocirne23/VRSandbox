module File.SceneData;

import File.Assimp;
import File.MeshData;

using namespace Assimp;

SceneData::SceneData()
{
}

SceneData::~SceneData()
{
}

bool SceneData::initialize(const char* fileName)
{
	m_pScene = m_importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_MakeLeftHanded);
	for (uint32 i = 0; i < m_pScene->mNumMeshes; i++)
	{
		MeshData& mesh = m_meshes.emplace_back();
		mesh.initialize(m_pScene->mMeshes[i]);
	}
	for (uint32 i = 0; i < m_pScene->mNumTextures; i++)
	{
		TextureData& texture = m_textures.emplace_back();
		texture.initialize(m_pScene->mTextures[i]);
	}
	for (uint32 i = 0; i < m_pScene->mNumMaterials; i++)
	{
		MaterialData& material = m_materials.emplace_back();
		material.initialize(m_pScene->mMaterials[i]);
	}
	return m_pScene != nullptr;
}

MeshData* SceneData::getMesh(const char* pMeshName)
{
	for (MeshData& mesh : m_meshes)
	{
		if (std::string(mesh.getName()) == pMeshName)
		{
			return &mesh;
		}
	}
	return nullptr;
}

MaterialData& SceneData::getMaterial(uint32 materialIdx)
{
	assert(materialIdx < (uint32)m_materials.size());
	return m_materials[materialIdx];
}
