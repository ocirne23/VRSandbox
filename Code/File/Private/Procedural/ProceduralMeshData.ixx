export module File.ProceduralMeshData;

import Core;
import Core.glm;
import Core.AABB;
import File.IMeshData;

export enum class EProceduralShape
{
	Cube,
	Plane,
	Sphere,
};

export class ProceduralMeshData final : public IMeshData
{
public:
	ProceduralMeshData();
	~ProceduralMeshData();
	ProceduralMeshData(const ProceduralMeshData&) = delete;
	ProceduralMeshData(ProceduralMeshData&&) = default;

	bool initialize(EProceduralShape shape, const char* name = nullptr);

	const glm::vec3* getVertices() const override;
	const glm::vec3* getNormals() const override;
	const glm::vec3* getTangents() const override;
	const glm::vec3* getBitangents() const override;
	const glm::vec3* getTexCoords() const override;
	const uint32* getIndices() const override;
	uint32 getNumVertices() const override;
	uint32 getNumIndices() const override;
	uint32 getMaterialIndex() const override;
	AABB getAABB() const override;
	const char* getName() const override;

private:
	void buildCube();
	void buildPlane();
	void buildSphere(uint32 stacks = 16, uint32 slices = 32);

	std::string              m_name;
	std::vector<glm::vec3>   m_vertices;
	std::vector<glm::vec3>   m_normals;
	std::vector<glm::vec3>   m_tangents;
	std::vector<glm::vec3>   m_bitangents;
	std::vector<glm::vec3>   m_texCoords; // xy = UV, z = 0
	std::vector<uint32>      m_indices;
	AABB                     m_aabb;
};
