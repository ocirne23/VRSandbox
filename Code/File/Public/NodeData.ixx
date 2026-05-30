export module File.NodeData;

import Core;
import Core.glm;

export class aiNode;

export class NodeData final
{
public:

    NodeData() {}
    NodeData(const aiNode* pNode) { initialize(pNode); }
    ~NodeData() {}

    bool initialize(const aiNode* pNode);
    operator bool() const { return m_pNode != nullptr; }
	bool operator==(const NodeData& other) const { return m_pNode == other.m_pNode; }
    bool isValid() const { return m_pNode != nullptr; }

    const char* getName() const;
    uint32 getNumChildren() const;
    NodeData getChild(uint32 idx) const;
	uint32 getNumMeshes() const;
	uint32 getMeshIndex(uint32 meshIdx) const;
    void getTransform(glm::vec3& pos, glm::vec3& scale, glm::quat& rot) const;
    const glm::vec3 getPosition() const;
    const glm::vec3 getScale() const;
    const glm::quat getRotation() const;

    uint32 getNumChildrenRecursive() const;

    NodeData findChild(std::initializer_list<const char*> hierarchy) const;
    std::vector<std::string> getChildrenNames() const;

private:

    const aiNode* m_pNode = nullptr;
};