export module RendererVK.Mesh;

import Core;
import RendererVK.Layout;

export class MeshInstance;
export class MeshData;

export class Mesh final
{
public:

    Mesh();
    ~Mesh();
    Mesh(Mesh&& copy);
    Mesh(const Mesh&) = delete;

    bool initialize(MeshData& meshData);

    float getRadius() const { return m_info.radius; }
    uint32 getNumIndices() const { return m_info.indexCount; }
    uint32 getVertexOffset() const { return m_info.vertexOffset; }
    uint32 getIndexOffset() const { return m_info.firstIndex; }
    uint32 getMeshIdx() const { return m_meshIdx; }

    void addInstance(MeshInstance* pInstance);
    void removeInstance(MeshInstance* pInstance);

    constexpr static uint32 INVALID_MESH_IDX = ~(0u);

private:
    friend class RendererVK;

    RendererVKLayout::MeshInfo m_info;
    uint32 m_meshIdx = 0;//INVALID_MESH_IDX;
    uint32 m_numInstances = 0;
};