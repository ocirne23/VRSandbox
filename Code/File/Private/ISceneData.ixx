export module File:ISceneData;

import File.fwd;

import Core;
import Animation;

export class ISceneData
{
public:

	static std::unique_ptr<ISceneData> createAssimpLoader();
	static std::unique_ptr<ISceneData> createProceduralLoader();

	// Loads animation clips from a separate file (rig in one file, animations in others) and appends them
	// to outSet, resolving each channel against targetSkeleton BY BONE NAME (retargeting across files that
	// share a skeleton, e.g. Mixamo exports). Any animation whose name contains skipName is ignored (null/
	// "" keeps all) — handy for stripping a "TPose" track exported into every file. clipNameOverride names
	// the kept clip(s) (suffixed _N if the file keeps several); when null the file-name stem is used.
	// Returns false if the file has no kept animations.
	static bool loadAnimations(const char* filePath, const Skeleton& targetSkeleton, AnimationSet& outSet, const char* skipName = nullptr, const char* clipNameOverride = nullptr);

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

	// Skeletal animation. getSkeleton() returns nullptr when the scene has no bones. Animation clips are
	// resolved against that skeleton (channel bone indices already mapped). getAnimations() returns the
	// full named set (for AnimationPlayer::setClipLibrary / play-by-name). Default: none.
	virtual const Skeleton* getSkeleton() const { return nullptr; }
	virtual const AnimationSet* getAnimations() const { return nullptr; }
	virtual uint32 getNumAnimations() const { return 0; }
	virtual const AnimationClip* getAnimation(uint32 idx) const { return nullptr; }
};