module File.ProceduralSceneData;

import Core;

std::unique_ptr<ISceneData> createProceduralLoader()
{
	return std::make_unique<ProceduralSceneData>();
}

ProceduralSceneData::ProceduralSceneData()
{
}

ProceduralSceneData::~ProceduralSceneData()
{
}

bool ProceduralSceneData::initialize(const char* shapeName, bool /*mergeNodes*/, bool /*preTransformVertices*/)
{
	m_shapeName = shapeName ? shapeName : "";

	EProceduralShape shape;
	bool isTerrain = false;
	if (m_shapeName == "cube")
		shape = EProceduralShape::Cube;
	else if (m_shapeName == "plane")
		shape = EProceduralShape::Plane;
	else if (m_shapeName == "sphere")
		shape = EProceduralShape::Sphere;
	else if (m_shapeName == "terrain")
	{
		shape     = EProceduralShape::Terrain;
		isTerrain = true;
	}
	else
	{
		assert(false && "ProceduralSceneData: unknown shape name. Use \"cube\", \"plane\", \"sphere\" or \"terrain\".");
		return false;
	}

	// Mesh
	ProceduralMeshData& mesh = m_meshes.emplace_back();
	bool meshOk;
	if (isTerrain)
		meshOk = mesh.initializeTerrain(TerrainParams{}, shapeName);
	else
		meshOk = mesh.initialize(shape, shapeName);
	assert(meshOk && "Failed to initialize ProceduralMeshData");

	// Textures: index 0 = checkerboard (diffuse), index 1 = flat normal map
	ProceduralTextureData& diffuseTex = m_textures.emplace_back();
	[[maybe_unused]] bool diffuseOk = diffuseTex.initialize(EProceduralTextureType::Checkerboard, isTerrain ? 1024 : 64, isTerrain ? 1024 : 64);
	assert(diffuseOk && "Failed to initialize diffuse ProceduralTextureData");

	ProceduralTextureData& normalTex = m_textures.emplace_back();
	[[maybe_unused]] bool normalOk = normalTex.initialize(EProceduralTextureType::FlatNormal, 8, 8);
	assert(normalOk && "Failed to initialize normal ProceduralTextureData");

	// Material
	ProceduralMaterialData& material = m_materials.emplace_back();
	[[maybe_unused]] bool matOk = material.initialize("ProceduralMaterial");
	assert(matOk && "Failed to initialize ProceduralMaterialData");
	material.setDiffuseTexIdx(0);
	material.setNormalTexIdx(1);

	// Root node references mesh 0
	m_rootNode.initialize(shapeName, { 0u });

	m_valid = true;
	return true;
}

const IMeshData* ProceduralSceneData::getMesh(const char* pMeshName) const
{
	for (const ProceduralMeshData& mesh : m_meshes)
	{
		if (std::string(mesh.getName()) == pMeshName)
			return &mesh;
	}
	return nullptr;
}

const IMeshData* ProceduralSceneData::getMesh(uint32 idx) const
{
	assert(idx < m_meshes.size());
	return &m_meshes[idx];
}

const IMaterialData* ProceduralSceneData::getMaterial(uint32 idx) const
{
	assert(idx < m_materials.size());
	return &m_materials[idx];
}

const ITextureData* ProceduralSceneData::getTexture(uint32 idx) const
{
	assert(idx < m_textures.size());
	return &m_textures[idx];
}
