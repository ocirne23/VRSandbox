module File.NodeData;

bool NodeData::initialize(const aiNode* pNode)
{
    m_pNode = pNode;
    return true;
}

const char* NodeData::getName() const
{
    return m_pNode->mName.C_Str();
}

uint32 NodeData::numChildren() const
{
    return m_pNode->mNumChildren;
}
const aiNode* NodeData::getChild(uint32 idx) const
{
    return m_pNode->mChildren[idx];
}

const glm::vec3 NodeData::getPosition() const
{
    aiVector3f pos;
    aiVector3f scale;
    aiQuaternion rot;
    m_pNode->mTransformation.Decompose(scale, rot, pos);
    return glm::vec3(pos.x, pos.y, pos.z);
}

const glm::quat NodeData::getRotation() const
{
    aiVector3f pos;
    aiVector3f scale;
    aiQuaternion rot;
    m_pNode->mTransformation.Decompose(scale, rot, pos);
    return glm::quat (rot.w, rot.x, rot.y, rot.z);
}

const glm::vec3 NodeData::getScale() const
{
    aiVector3f pos;
    aiVector3f scale;
    aiQuaternion rot;
    m_pNode->mTransformation.Decompose(scale, rot, pos);
    return glm::vec3(scale.x, scale.y, scale.z);
}

uint32 NodeData::getNumChildrenRecursive() const
{
    uint32 numChildren = m_pNode->mNumChildren;
    for (uint32 i = 0; i < m_pNode->mNumChildren; i++)
    {
        numChildren += NodeData(m_pNode->mChildren[i]).getNumChildrenRecursive();
    }
    return numChildren;
}
