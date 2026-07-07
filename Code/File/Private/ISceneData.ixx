export module File:ISceneData;

import File.fwd;

import Core;
import Animation;

// Cook options for the scene cache (see loadCached / SceneCooker.cpp). All fields participate in the
// cache's options hash, so changing any of them re-cooks affected scenes on the next load.
export struct SceneCookOptions
{
	bool enableCache = true;      // false = always import directly (no read, no write)
	bool convertTextures = true;  // convert non-DDS model textures (loose + embedded) to BC .dds for mip streaming
	bool generateLods = true;     // bake meshopt LOD chains for static meshes without authored LodN_ chains
	int  lodLevels = 4;           // max generated levels beyond LOD0
	float lodReduction = 0.25f;   // index-count factor per generated level
	int  lodMinIndices = 32;      // don't generate for meshes below this index count
};

export class ISceneData
{
public:

	static std::unique_ptr<ISceneData> createAssimpLoader();
	static std::unique_ptr<ISceneData> createProceduralLoader();

	// Cache-aware load: serves a cooked binary snapshot from Assets/Local/Cooked when it matches the
	// source file (mtime + size) and options, otherwise imports via Assimp, cooks, and writes it for
	// next time. Skinned/animated scenes always import directly (never cooked). Returns an initialized
	// scene, or null on import failure.
	static std::unique_ptr<ISceneData> loadCached(const char* filePath, bool mergeNodes, bool preTransformVertices, const SceneCookOptions& options);

	// Loads animation clips from a separate file (rig in one file, animations in others) and appends them
	// to outSet, resolving each channel against targetSkeleton BY BONE NAME (retargeting across files that
	// share a skeleton, e.g. Mixamo exports). Any animation whose name contains skipName is ignored (null/
	// "" keeps all) — handy for stripping a "TPose" track exported into every file. trackName selects a
	// single track from a multi-track file (kept only if its name contains trackName; null/"" keeps all).
	// clipNameOverride names the kept clip(s) (suffixed _N if several are kept); when null the file-name
	// stem is used. Returns false if the file has no kept animations.
	static bool loadAnimations(const char* filePath, const Skeleton& targetSkeleton, AnimationSet& outSet, const char* skipName = nullptr, const char* clipNameOverride = nullptr, const char* trackName = nullptr);

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