export module RendererVK.MeshInstance;

import Core;
import RendererVK.Mesh;

export class MeshInstance final
{
public:

	void setInstanceData(InstanceData* pInstanceData) 
	{ 
		m_pMesh->setInstanceData(m_instanceDataIdx, pInstanceData); 
	}

	void remove()
	{
		m_pMesh->removeInstance(this);
	}

private:
	friend class Mesh;
	Mesh* m_pMesh = nullptr;
	uint32 m_instanceDataIdx;
};