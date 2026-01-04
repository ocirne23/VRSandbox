export module File.NodeData;
extern "C++" {

import Core;
import Core.glm;
import File.Assimp;

export class NodeData final
{
public:

    NodeData() {}
    NodeData(const aiNode* pNode) { initialize(pNode); }
    ~NodeData() {}
    NodeData(NodeData&&) = default;

    bool initialize(const aiNode* pNode);
    operator bool() const { return m_pNode != nullptr; }
    bool isValid() const { return m_pNode != nullptr; }

    const char* getName() const;
    uint32 numChildren() const;
    const aiNode* getChild(uint32 idx) const;
    const aiMatrix4x4& getTransformation() const { return m_pNode->mTransformation; }
    const glm::vec3 getPosition() const;
    const glm::vec3 getScale() const;
    const glm::quat getRotation() const;
    const aiNode* getAiNode() const { return m_pNode; }

    uint32 getNumChildrenRecursive() const;

    NodeData findChild(std::initializer_list<const char*> hierarchy) const;
    std::vector<std::string> getChildrenNames() const;

private:

    const aiNode* m_pNode = nullptr;
};
} // extern "C++"