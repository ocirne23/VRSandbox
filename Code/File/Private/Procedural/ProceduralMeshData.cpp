module File.ProceduralMeshData;

import Core;
import Core.glm;

ProceduralMeshData::ProceduralMeshData()
{
}

ProceduralMeshData::~ProceduralMeshData()
{
}

bool ProceduralMeshData::initialize(EProceduralShape shape, const char* name)
{
	switch (shape)
	{
	case EProceduralShape::Cube:
		m_name = name ? name : "Cube";
		buildCube();
		break;
	case EProceduralShape::Plane:
		m_name = name ? name : "Plane";
		buildPlane();
		break;
	case EProceduralShape::Sphere:
		m_name = name ? name : "Sphere";
		buildSphere();
		break;
	default:
		return false;
	}

	m_aabb.min = glm::vec3(std::numeric_limits<float>::max());
	m_aabb.max = glm::vec3(std::numeric_limits<float>::lowest());
	for (const glm::vec3& v : m_vertices)
	{
		m_aabb.min = glm::min(m_aabb.min, v);
		m_aabb.max = glm::max(m_aabb.max, v);
	}
	return true;
}

const glm::vec3* ProceduralMeshData::getVertices()    const { return m_vertices.data(); }
const glm::vec3* ProceduralMeshData::getNormals()     const { return m_normals.data(); }
const glm::vec3* ProceduralMeshData::getTangents()    const { return m_tangents.data(); }
const glm::vec3* ProceduralMeshData::getBitangents()  const { return m_bitangents.data(); }
const glm::vec3* ProceduralMeshData::getTexCoords()   const { return m_texCoords.data(); }
const uint32*    ProceduralMeshData::getIndices()      const { return m_indices.data(); }
uint32           ProceduralMeshData::getNumVertices()  const { return (uint32)m_vertices.size(); }
uint32           ProceduralMeshData::getNumIndices()   const { return (uint32)m_indices.size(); }
uint32           ProceduralMeshData::getMaterialIndex() const { return 0; }
AABB             ProceduralMeshData::getAABB()          const { return m_aabb; }
const char*      ProceduralMeshData::getName()          const { return m_name.c_str(); }

// ---------------------------------------------------------------------------
// Cube: 6 faces * 4 verts = 24 vertices, 6 faces * 6 indices = 36 indices
// All vertices in CCW winding order when viewed from outside.
// ---------------------------------------------------------------------------
void ProceduralMeshData::buildCube()
{
	struct FaceDesc
	{
		glm::vec3 normal;
		glm::vec3 tangent;
		glm::vec3 bitangent;
		glm::vec3 verts[4]; // BL, BR, TR, TL (CCW from outside)
	};

	constexpr float h = 0.5f;
	const FaceDesc faces[6] =
	{
		// +Z front
		{ {0,0,1},  {1,0,0},  {0,1,0},  { {-h,-h,h}, { h,-h,h}, { h, h,h}, {-h, h,h} } },
		// -Z back
		{ {0,0,-1}, {-1,0,0}, {0,1,0},  { { h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h} } },
		// +X right
		{ {1,0,0},  {0,0,-1}, {0,1,0},  { { h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h} } },
		// -X left
		{ {-1,0,0}, {0,0,1},  {0,1,0},  { {-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h} } },
		// +Y top  (verts go front-left→front-right→back-right→back-left so cross gives +Y)
		{ {0,1,0},  {1,0,0},  {0,0,-1}, { {-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h} } },
		// -Y bottom (verts go back-left→back-right→front-right→front-left so cross gives -Y)
		{ {0,-1,0}, {1,0,0},  {0,0,1},  { {-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h} } },
	};

	const glm::vec3 uvs[4] = { {0,0,0},{1,0,0},{1,1,0},{0,1,0} };

	for (const FaceDesc& face : faces)
	{
		const uint32 base = (uint32)m_vertices.size();
		for (int i = 0; i < 4; i++)
		{
			m_vertices.push_back(face.verts[i]);
			m_normals.push_back(face.normal);
			m_tangents.push_back(face.tangent);
			m_bitangents.push_back(face.bitangent);
			m_texCoords.push_back(uvs[i]);
		}
		// Two CCW triangles from the quad
		m_indices.push_back(base + 0); m_indices.push_back(base + 1); m_indices.push_back(base + 2);
		m_indices.push_back(base + 0); m_indices.push_back(base + 2); m_indices.push_back(base + 3);
	}
}

// ---------------------------------------------------------------------------
// Plane: unit quad in the XZ plane, normal pointing +Y
// ---------------------------------------------------------------------------
void ProceduralMeshData::buildPlane()
{
	constexpr float h = 0.5f;
	// Vertices ordered front-left→front-right→back-right→back-left so cross gives +Y
	m_vertices   = { {-h,0, h}, { h,0, h}, { h,0,-h}, {-h,0,-h} };
	m_normals    = { {0,1,0}, {0,1,0}, {0,1,0}, {0,1,0} };
	m_tangents   = { {1,0,0}, {1,0,0}, {1,0,0}, {1,0,0} };
	// V direction goes from z=+h to z=-h, so bitangent = (0,0,-1)
	m_bitangents = { {0,0,-1}, {0,0,-1}, {0,0,-1}, {0,0,-1} };
	m_texCoords  = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} };
	m_indices    = { 0,1,2, 0,2,3 };
}

// ---------------------------------------------------------------------------
// UV Sphere: unit sphere (diameter 1), stacks = latitude bands, slices = longitude bands
// ---------------------------------------------------------------------------
void ProceduralMeshData::buildSphere(uint32 stacks, uint32 slices)
{
	const float pi = glm::pi<float>();

	for (uint32 stack = 0; stack <= stacks; stack++)
	{
		const float phi    = pi * (float)stack / (float)stacks; // 0 (top) to pi (bottom)
		const float sinPhi = std::sin(phi);
		const float cosPhi = std::cos(phi);

		for (uint32 slice = 0; slice <= slices; slice++)
		{
			const float theta    = 2.0f * pi * (float)slice / (float)slices;
			const float sinTheta = std::sin(theta);
			const float cosTheta = std::cos(theta);

			// Unit sphere position; scale to 0.5 for unit diameter
			const glm::vec3 normal(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
			const glm::vec3 tangent  = glm::normalize(glm::vec3(-sinTheta, 0.0f, cosTheta));
			const glm::vec3 bitangent = glm::cross(normal, tangent);
			const glm::vec3 uv((float)slice / (float)slices, (float)stack / (float)stacks, 0.0f);

			m_vertices.push_back(normal * 0.5f);
			m_normals.push_back(normal);
			m_tangents.push_back(tangent);
			m_bitangents.push_back(bitangent);
			m_texCoords.push_back(uv);
		}
	}

	for (uint32 stack = 0; stack < stacks; stack++)
	{
		for (uint32 slice = 0; slice < slices; slice++)
		{
			const uint32 a = stack * (slices + 1) + slice;
			const uint32 b = a + 1;
			const uint32 c = a + (slices + 1);
			const uint32 d = c + 1;

			m_indices.push_back(a); m_indices.push_back(b); m_indices.push_back(c);
			m_indices.push_back(b); m_indices.push_back(d); m_indices.push_back(c);
		}
	}
}
