module File.MeshData;

import Core;
import File.Assimp;

using namespace Assimp;

MeshData::MeshData()
{
}

MeshData::~MeshData()
{
}

bool MeshData::initialize(const aiMesh* pMesh)
{
    m_pMesh = pMesh;
    m_pName = pMesh->mName.C_Str();
    pMesh->mMaterialIndex;

    m_indices.resize(pMesh->mNumFaces * 3);
    for (uint32 i = 0; i < pMesh->mNumFaces; i++)
    {
        memcpy(&m_indices[i * 3], pMesh->mFaces[i].mIndices, 3 * sizeof(uint32));
    }
    return true;
}

glm::vec3* MeshData::getVertices()
{
    return reinterpret_cast<glm::vec3*>(m_pMesh->mVertices);
}

glm::vec3* MeshData::getNormals()
{
    return reinterpret_cast<glm::vec3*>(m_pMesh->mNormals);
}

glm::vec3* MeshData::getTangents()
{
    return reinterpret_cast<glm::vec3*>(m_pMesh->mTangents);
}

glm::vec3* MeshData::getBitangents()
{
    return reinterpret_cast<glm::vec3*>(m_pMesh->mBitangents);
}

glm::vec3* MeshData::getTexCoords()
{
    return reinterpret_cast<glm::vec3*>(m_pMesh->mTextureCoords[0]);
}

uint32 MeshData::getNumVertices()
{
    return m_pMesh->mNumVertices;
}

uint32* MeshData::getIndices()
{
    return m_indices.data();
}

uint32 MeshData::getNumIndices()
{
    return (uint32)m_indices.size();
}

void MeshData::getIndices(std::vector<uint32>& indices)
{
    assert(indices.empty());
    indices.resize(m_pMesh->mNumFaces * 3);
    for (uint32 i = 0; i < m_pMesh->mNumFaces; i++)
    {
        memcpy(&indices[i * 3], m_pMesh->mFaces[i].mIndices, 3 * sizeof(uint32));
    }
}

uint32 MeshData::getMaterialIndex()
{
    return m_pMesh->mMaterialIndex;
}

AABB MeshData::getAABB()
{
    AABB aabb;
    aabb.min = glm::vec3(m_pMesh->mAABB.mMin.x, m_pMesh->mAABB.mMin.y, m_pMesh->mAABB.mMin.z);
    aabb.max = glm::vec3(m_pMesh->mAABB.mMax.x, m_pMesh->mAABB.mMax.y, m_pMesh->mAABB.mMax.z);
    return aabb;
}