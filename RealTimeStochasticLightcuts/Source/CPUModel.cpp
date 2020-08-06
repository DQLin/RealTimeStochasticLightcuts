#include "pch.h"
#include "CPUModel.h"
#include "../../include/mikktspace/mikktspace.h"

namespace {
	class MikkTSpaceWrapper
	{
	public:
		static std::vector<glm::vec3> generateBitangents(CPUVertex* pVertices, const uint32_t* pIndices, size_t vertexCount, size_t indexCount)
		{
			SMikkTSpaceInterface mikktspace = {};
			mikktspace.m_getNumFaces = [](const SMikkTSpaceContext* pContext) {return ((MikkTSpaceWrapper*)(pContext->m_pUserData))->getFaceCount(); };
			mikktspace.m_getNumVerticesOfFace = [](const SMikkTSpaceContext * pContext, int32_t face) {return 3; };
			mikktspace.m_getPosition = [](const SMikkTSpaceContext * pContext, float position[], int32_t face, int32_t vert) {((MikkTSpaceWrapper*)(pContext->m_pUserData))->getPosition(position, face, vert); };
			mikktspace.m_getNormal = [](const SMikkTSpaceContext * pContext, float normal[], int32_t face, int32_t vert) {((MikkTSpaceWrapper*)(pContext->m_pUserData))->getNormal(normal, face, vert); };
			mikktspace.m_getTexCoord = [](const SMikkTSpaceContext * pContext, float texCrd[], int32_t face, int32_t vert) {((MikkTSpaceWrapper*)(pContext->m_pUserData))->getTexCrd(texCrd, face, vert); };
			mikktspace.m_setTSpaceBasic = [](const SMikkTSpaceContext * pContext, const float tangent[], float sign, int32_t face, int32_t vert) {((MikkTSpaceWrapper*)(pContext->m_pUserData))->setTangent(tangent, sign, face, vert); };

			MikkTSpaceWrapper wrapper(pVertices, pIndices, vertexCount, indexCount);
			SMikkTSpaceContext context = {};
			context.m_pInterface = &mikktspace;
			context.m_pUserData = &wrapper;

			if (genTangSpaceDefault(&context) == false)
			{
				printf("Failed to generate MikkTSpace tangents");
				return std::vector<glm::vec3>(vertexCount, glm::vec3(0, 0, 0));
			}

			return wrapper.mBitangents;
		}

	private:
		MikkTSpaceWrapper(CPUVertex* pVertices, const uint32_t* pIndices, size_t vertexCount, size_t indexCount) :
			mpVertices(pVertices), mpIndices(pIndices), mFaceCount(indexCount / 3), mBitangents(vertexCount) {}

		CPUVertex* mpVertices;
		const uint32_t* mpIndices;
		size_t mFaceCount;
		std::vector<glm::vec3> mBitangents;
		int32_t getFaceCount() const { return (int32_t)mFaceCount; }
		int32_t getIndex(int32_t face, int32_t vert) { return mpIndices[face * 3 + vert]; }
		void getPosition(float position[], int32_t face, int32_t vert) { *(glm::vec3*)position = mpVertices[getIndex(face, vert)].Position; }
		void getNormal(float normal[], int32_t face, int32_t vert) { *(glm::vec3*)normal = mpVertices[getIndex(face, vert)].Normal; }
		void getTexCrd(float texCrd[], int32_t face, int32_t vert) { *(glm::vec2*)texCrd = mpVertices[getIndex(face, vert)].TexCoords; }

		void setTangent(const float tangent[], float sign, int32_t face, int32_t vert)
		{
			int32_t index = getIndex(face, vert);
			glm::vec3 T(*(glm::vec3*)tangent), N;
			getNormal(&N[0], face, vert);
			mpVertices[index].Tangent = T;
			mpVertices[index].Bitangent = sign * cross(N, T);
		}
	};
}


CPUMesh CPUModel::processMesh(aiMesh *mesh, const aiScene *scene, int geomId)
{
	// data to fill
	std::vector<CPUVertex> vertices;
	std::vector<unsigned int> indices;
	std::vector<CPUTexture> textures;

	// Walk through each of the mesh's vertices
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		CPUVertex vertex;
		glm::vec3 vector; 
		vector.x = mesh->mVertices[i].x;
		vector.y = mesh->mVertices[i].y;
		vector.z = mesh->mVertices[i].z;
		vertex.Position = vector;
		// normals
		vector.x = mesh->mNormals[i].x;
		vector.y = mesh->mNormals[i].y;
		vector.z = mesh->mNormals[i].z;
		vertex.Normal = vector;
		// texture coordinates
		if (mesh->mTextureCoords[0]) 
		{
			glm::vec2 vec;
			vec.x = mesh->mTextureCoords[0][i].x;
			vec.y = mesh->mTextureCoords[0][i].y;
			vertex.TexCoords = vec;
		}
		else
			vertex.TexCoords = glm::vec2(0.0f, 0.0f);
		// tangent
		if (mesh->mTangents)
		{
			vector.x = mesh->mTangents[i].x;
			vector.y = mesh->mTangents[i].y;
			vector.z = mesh->mTangents[i].z;
			vertex.Tangent = vector;
			// bitangent
			vector.x = mesh->mBitangents[i].x;
			vector.y = mesh->mBitangents[i].y;
			vector.z = mesh->mBitangents[i].z;
			vertex.Bitangent = vector;
		}
		else
		{
			vertex.Tangent = glm::vec3(0, 0, 0);
			vertex.Bitangent = glm::vec3(0, 0, 0);
		}
		vertices.push_back(vertex);
	}

	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		// retrieve all indices of the face and store them in the indices vector
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	// generate tangent space
	// confirm that mesh has normal, position, texcoord, and indices defined
	
	if (!mesh->mTangents)
		if (mesh->mNormals && mesh->mTextureCoords[0])
			MikkTSpaceWrapper::generateBitangents(vertices.data(), indices.data(), mesh->mNumVertices, 3 * mesh->mNumFaces);

	// process materials
	int matId = matIdOffset + mesh->mMaterialIndex;

	int emitMatId = -1;
	for (CPUTexture& tex : materials[matId].textures)
	{
		if (tex.type == "texture_emissive")
		{
			emitMatId = matId;
			break;
		}
	}

	int meshlightId = -1;

	if (!materials[matId].matEmissionColor.isZero() || emitMatId >= 0)
	{
		if (materials[matId].matEmissionColor.isZero()) materials[matId].matEmissionColor = 1;
		meshlightId = meshLights.size();
		meshLights.push_back(CPUMeshLight(geomId, mesh->mNumFaces, materials[matId].matEmissionColor, emitMatId));
		if (!mesh->mTextureCoords[0]) meshLights.back().texCoordDefined = false;
	}

	return CPUMesh(vertices, indices, matId, meshlightId);
}
