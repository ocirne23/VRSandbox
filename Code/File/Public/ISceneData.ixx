export module File.ISceneData;

import Core;
import File.fwd;

export class ISceneData
{
public:

	static std::unique_ptr<ISceneData> createAssimpLoader();
	static std::unique_ptr<ISceneData> createProceduralLoader();

	virtual ~ISceneData() = default;

	virtual bool initialize(const char* filePath, bool mergeNodes, bool preTransformVertices) = 0;
	virtual bool isValid() const = 0;

	virtual const std::string& getFilePath() const = 0;
	virtual const INodeData& getRootNode() const = 0;

	virtual uint32 getNumMeshes() const = 0;
	virtual uint32 getNumMaterials() const = 0;
	virtual uint32 getNumTextures() const = 0;

	virtual const IMeshData* getMesh(const char* pMeshName) const = 0;
	virtual const IMeshData* getMesh(uint32 idx) const = 0;
	virtual const IMaterialData* getMaterial(uint32 idx) const = 0;
	virtual const ITextureData* getTexture(uint32 idx) const = 0;
};