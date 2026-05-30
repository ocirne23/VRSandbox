module File.MeshData;

import Core;
import File.Assimp;

using namespace Assimp;

MeshData::MeshData()
{
}

MeshData::~MeshData()
{
    destroy();
}

void MeshData::destroy()
{
	if (!m_ownsData)
	{
		m_vertices.release();
		m_normals.release();
		m_tangents.release();
		m_bitangents.release();
		m_texCoords.release();
	}
	m_name.clear();
	m_indices.clear();
	m_materialIdx = UINT32_MAX;
	m_ownsData = false;
}

bool MeshData::initialize(const aiMesh* pMesh)
{
    if (!m_indices.empty())
        destroy();

    m_ownsData = false;
	m_name = pMesh->mName.C_Str();

    m_aabb.min = glm::vec3(pMesh->mAABB.mMin.x, pMesh->mAABB.mMin.y, pMesh->mAABB.mMin.z);
    m_aabb.max = glm::vec3(pMesh->mAABB.mMax.x, pMesh->mAABB.mMax.y, pMesh->mAABB.mMax.z);

    m_indices.resize(pMesh->mNumFaces * 3);
    for (uint32 i = 0; i < pMesh->mNumFaces; i++)
    {
        memcpy(&m_indices[i * 3], pMesh->mFaces[i].mIndices, 3 * sizeof(uint32));
    }

	m_numVertices = pMesh->mNumVertices;
	if (pMesh->mVertices)
	    m_vertices.reset(reinterpret_cast<glm::vec3*>(pMesh->mVertices));
	if (pMesh->HasNormals())
		m_normals.reset(reinterpret_cast<glm::vec3*>(pMesh->mNormals));
    if (pMesh->HasTangentsAndBitangents())
    {
		m_tangents.reset(reinterpret_cast<glm::vec3*>(pMesh->mTangents));
		m_bitangents.reset(reinterpret_cast<glm::vec3*>(pMesh->mBitangents));
    }
	if (pMesh->HasTextureCoords(0))
	{
		m_texCoords.reset(reinterpret_cast<glm::vec3*>(pMesh->mTextureCoords[0]));
	}
	m_materialIdx = pMesh->mMaterialIndex;
    
    return true;
}