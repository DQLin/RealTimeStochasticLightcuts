// NOTE: Some part of this file is modified from model.h in the LearnOpenGL.com code repository (Author: Joey de Vries)
// which is licensed under the CC BY-NC 4.0 license

#pragma once
#include <glm/glm.hpp>
#include "CPUColor.h"
#include "ImageIO.h"
#include "CPUaabb.h"
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include "Animation.h"


struct CPUVertex {
	glm::vec3 Position;
	glm::vec2 TexCoords;
	glm::vec3 Normal;
	glm::vec3 Tangent;
	glm::vec3 Bitangent;

	CPUVertex() {};
	CPUVertex(glm::vec3 position, glm::vec2 texcoords, glm::vec3 normal, glm::vec3 tangent, glm::vec3 bitangent)
		: Position(position), TexCoords(texcoords), Normal(normal), Tangent(tangent), Bitangent(bitangent) {};

	static void ConvertFromHalfFloatVertexChunk(std::vector<CPUVertex>& output, unsigned char* m_pVertexData, int numVerts)
	{
		unsigned char* head = m_pVertexData;
		for (int j = 0; j < numVerts; j++)
		{
			CPUVertex cur;

			memcpy(&cur.Position, head, 4 * 3);
			head += 4 * 3;

			memcpy(&cur.TexCoords, head, 4 * 2);
			head += 4 * 2;

			memcpy(&cur.Normal, head, 4 * 3); //NORMAL
			head += 4 * 3;

			memcpy(&cur.Tangent, head, 4 * 3); //TANGENT
			head += 4 * 3;

			memcpy(&cur.Bitangent, head, 4 * 3); //BITANGENT
			head += 4 * 3;

			output[j] = cur;
		}	
	}
};

struct CPUFace {
	CPUFace(CPUVertex v0, CPUVertex v1, CPUVertex v2) : v0(v0), v1(v1), v2(v2) {};
	CPUVertex v0, v1, v2;

	glm::vec2 getuv(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.TexCoords + u * v1.TexCoords + v * v2.TexCoords;
	}

	glm::vec3 getNormal(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Normal + u * v1.Normal + v * v2.Normal;
	}

	glm::vec3 getBitangent(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Bitangent + u * v1.Bitangent + v * v2.Bitangent;
	}

	glm::vec3 getTangent(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Tangent + u * v1.Tangent + v * v2.Tangent;
	}

	glm::vec3 getPosition(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Position + u * v1.Position + v * v2.Position;
	}
};

class CPUTexture {
public:
	int width, height, nrComponents;
	std::string path;
	std::string type;
	int id;

	CPUTexture() {};
	CPUTexture(unsigned char* data, int width, int height, int nrComponents) : data(data), width(width), height(height), nrComponents(nrComponents) {};

	CPUTexture(std::string filename, const std::string &directory, bool ignoreGamma = false, const std::string& type = "") : type(type)
	{
		path = filename;
		filename = directory + "/" + filename;
		if (filename.substr(filename.find_last_of('.') + 1) == "dds") data = nullptr; //don't load dds
		else ImageIO::ReadImageFile(filename.c_str(), &data, &width, &height, &nrComponents, ignoreGamma, true /*type == "texture_diffuse"*/);
	}
	glm::vec2 TileClamp(const glm::vec2 &uv)
	{
		glm::vec2 u;
		u.x = uv.x - (int)uv.x;
		u.y = uv.y - (int)uv.y;
		if (u.x < 0) u.x += 1;
		if (u.y < 0) u.y += 1;
		u.y = 1 - u.y;
		return u;
	}

	CPUColor4 toColor(int offset)
	{
		offset = nrComponents * offset;
		return CPUColor4(data[offset] / 255.0, data[offset + 1] / 255.0, data[offset + 2] / 255.0, data[offset + 3] / 255.0);
	}

	CPUColor toColor3(int offset)
	{
		offset = nrComponents * offset;
		return CPUColor(data[offset] / 255.0, data[offset + 1] / 255.0, data[offset + 2] / 255.0);
	}

	CPUColor4 Sample(const glm::vec2 &uv)
	{
		if (width + height == 0) return CPUColor4(0, 0, 0, 0);

		glm::vec2 u = TileClamp(uv);
		float x = width * u.x;
		float y = height * u.y;
		int ix = (int)x;
		int iy = (int)y;
		float fx = x - ix;
		float fy = y - iy;

		if (ix < 0) ix -= (ix / width - 1)*width;
		if (ix >= width) ix -= (ix / width)*width;
		int ixp = ix + 1;
		if (ixp >= width) ixp -= width;

		if (iy < 0) iy -= (iy / height - 1)*height;
		if (iy >= height) iy -= (iy / height)*height;
		int iyp = iy + 1;
		if (iyp >= height) iyp -= height;

		//magnification filtering
		return	toColor(iy *width + ix) * ((1 - fx)*(1 - fy)) +
			toColor(iy *width + ixp) * (fx *(1 - fy)) +
			toColor(iyp*width + ix) * ((1 - fx)*   fy) +
			toColor(iyp*width + ixp) * (fx *   fy);
	}

	CPUColor SampleColor3(const glm::vec2 &uv)
	{
		if (width + height == 0) return CPUColor(0, 0, 0);

		glm::vec2 u = TileClamp(uv);
		float x = width * u.x;
		float y = height * u.y;
		int ix = (int)x;
		int iy = (int)y;
		float fx = x - ix;
		float fy = y - iy;

		if (ix < 0) ix -= (ix / width - 1)*width;
		if (ix >= width) ix -= (ix / width)*width;
		int ixp = ix + 1;
		if (ixp >= width) ixp -= width;

		if (iy < 0) iy -= (iy / height - 1)*height;
		if (iy >= height) iy -= (iy / height)*height;
		int iyp = iy + 1;
		if (iyp >= height) iyp -= height;

		//magnification filtering
		return	toColor3(iy *width + ix) * ((1 - fx)*(1 - fy)) +
			toColor3(iy *width + ixp) * (fx *(1 - fy)) +
			toColor3(iyp*width + ix) * ((1 - fx)*   fy) +
			toColor3(iyp*width + ixp) * (fx *   fy);
	}

	unsigned char* data;
};

class CPUMaterial
{
public:
	CPUColor matDiffuseColor;
	CPUColor matSpecularColor;
	CPUColor matEmissionColor;
	std::vector<CPUTexture> textures;
	bool isCutOut;
	bool isTransparent;

	CPUMaterial() {};

	CPUMaterial(std::vector<CPUTexture> textures, CPUColor matDiffuseColor, CPUColor matSpecularColor, CPUColor matEmissionColor, bool isCutOut, bool isTransparent)
	{
		this->textures = textures;
		this->matDiffuseColor = matDiffuseColor;
		this->matSpecularColor = matSpecularColor;
		this->matEmissionColor = matEmissionColor;
		this->isCutOut = isCutOut;
		this->isTransparent = isTransparent;
	}
};

class CPUMesh {
public:
	std::vector<CPUVertex> vertices;
	std::vector<unsigned int> indices;
	std::vector<int> instances; // instance ids (actually global matrix ids)
	aabb boundingBox;
	int matId;
	int meshlightId;
	CPUMesh() {}

	CPUMesh(std::vector<CPUVertex> vertices, std::vector<unsigned int> indices, int matId, int meshlightId = -1)
	{
		this->vertices = vertices;
		this->indices = indices;
		this->matId = matId;
		this->meshlightId = meshlightId;
	}

	CPUFace getFace(int primID)
	{
		return CPUFace(vertices[indices[3 * primID]], vertices[indices[3 * primID + 1]], vertices[indices[3 * primID + 2]]);
	}
};

struct CPUMeshInstance
{
	unsigned int meshID;
	unsigned int globalMatrixID;
	CPUMeshInstance() {};
	CPUMeshInstance(unsigned int meshID, unsigned int globalMatrixID) : meshID(meshID), globalMatrixID(globalMatrixID) {};
};

struct CPUSceneNode
{
	unsigned int parentNodeID;
	glm::mat4 modelMatrix;
	CPUSceneNode() {};
	CPUSceneNode(unsigned int parentNodeID, const glm::mat4& modelMatrix) : parentNodeID(parentNodeID), modelMatrix(modelMatrix) {};
};

class CPUMeshLight
{
public:
	int indexOffset; //index array offset
	int vertexOffset;
	int indexCount;
	int geomId;
	int numTriangles;
	int emitMatId;
	int instanceOffset;
	int instanceCount;
	bool texCoordDefined;
	CPUColor emission;

	CPUMeshLight() {};
	CPUMeshLight(int geomId, int numTriangles, const CPUColor& emission, int emitMatId) : 
		geomId(geomId), numTriangles(numTriangles), emission(emission), emitMatId(emitMatId) {
		indexOffset = 0; vertexOffset = 0; indexCount = 0; texCoordDefined = true;
	};
};

// mesh light instance
class CPUMeshLightInstance
{
	unsigned meshlightID;
	unsigned globalMatrixID;
};


class CPUModel
{
public:

	CPUModel() {};

	std::vector<CPUTexture> textures_loaded;
	std::vector<CPUMesh> meshes;
	std::vector<CPUMeshLight> meshLights;
	std::vector<CPUMaterial> materials;
	std::vector<CPUMeshInstance> meshInstances;
	std::vector<CPUMeshInstance> meshlightInstances; // store meshlightID in meshID
	std::vector<CPUSceneNode> sceneNodes;
	std::vector<std::shared_ptr<Animation>> animations;
	glm::mat4 initialCameraMatrix;
	int CameraNodeID = -1;

	std::map<const aiNode*, uint32_t> mAiToSceneNodeID;
	std::map<const std::string, std::vector<const aiNode*>> mAiNodes;

	std::string directory;
	int primIdOffset;
	int matIdOffset;
	bool gammaCorrection;
	glm::vec3 scene_sphere_pos;
	float scene_sphere_radius;
	int numEmissiveTextures;

	CPUModel(const std::vector<std::string> &paths, bool gamma = false): gammaCorrection(gamma)
	{
		if (paths[0].substr(paths[0].find_last_of(".")) == ".bin")
		{
			LoadDump(paths[0]);
		}
		else
		{
			primIdOffset = 0;
			matIdOffset = 0;
			for (auto& path : paths)
			{
				loadModel(path);
				primIdOffset += meshes.size();
				matIdOffset += materials.size();
			}
			computeRitterBoundingSphere();
		}
	}

	void SaveDump(std::string filename)
	{
		std::ofstream f(filename, std::ios_base::binary);

		int N = textures_loaded.size();
		f.write((char*)&N, 4);
		for (int i = 0; i < N; i++)
		{
			CPUTexture& tex = textures_loaded[i];
			f.write((char*)&tex.width, 4);
			f.write((char*)&tex.height, 4);
			f.write((char*)&tex.nrComponents, 4);
			int strsize = tex.path.size() + 1;
			f.write((char*)&strsize, 4);
			f.write(tex.path.c_str(), strsize);
			strsize = tex.type.size() + 1;
			f.write((char*)&strsize, 4);
			f.write(tex.type.c_str(), strsize);
			f.write((char*)&tex.id, 4);
			f.write((char*)tex.data, tex.width*tex.height*tex.nrComponents);
		}

		N = meshes.size();
		f.write((char*)&N, 4);

		for (int i = 0; i < N; i++)
		{
			CPUMesh& mesh = meshes[i];
			int N = mesh.vertices.size();
			f.write((char*)&N, 4);
			for (int i = 0; i < N; i++)
			{
				CPUVertex& vert = mesh.vertices[i];
				f.write((char*)&vert.Position.x, 4);
				f.write((char*)&vert.Position.y, 4);
				f.write((char*)&vert.Position.z, 4);
				f.write((char*)&vert.TexCoords.x, 4);
				f.write((char*)&vert.TexCoords.y, 4);
				f.write((char*)&vert.Normal.x, 4);
				f.write((char*)&vert.Normal.y, 4);
				f.write((char*)&vert.Normal.z, 4);
				f.write((char*)&vert.Tangent.x, 4);
				f.write((char*)&vert.Tangent.y, 4);
				f.write((char*)&vert.Tangent.z, 4);
				f.write((char*)&vert.Bitangent.x, 4);
				f.write((char*)&vert.Bitangent.y, 4);
				f.write((char*)&vert.Bitangent.z, 4);
			}

			N = mesh.indices.size();
			f.write((char*)&N, 4);
			for (int i = 0; i < N; i++)
			{
				f.write((char*)&mesh.indices[i], 4);
			}

			f.write((char*)&mesh.boundingBox.pos.x, 4);
			f.write((char*)&mesh.boundingBox.pos.y, 4);
			f.write((char*)&mesh.boundingBox.pos.z, 4);
			f.write((char*)&mesh.boundingBox.end.x, 4);
			f.write((char*)&mesh.boundingBox.end.y, 4);
			f.write((char*)&mesh.boundingBox.end.z, 4);

			f.write((char*)&mesh.matId, 4);
		}


		N = materials.size();
		f.write((char*)&N, 4);

		for (int i = 0; i < N; i++)
		{
			CPUMaterial& mat = materials[i];

			f.write((char*)&mat.matDiffuseColor.r, 4);
			f.write((char*)&mat.matDiffuseColor.g, 4);
			f.write((char*)&mat.matDiffuseColor.b, 4);

			f.write((char*)&mat.matSpecularColor.r, 4);
			f.write((char*)&mat.matSpecularColor.g, 4);
			f.write((char*)&mat.matSpecularColor.b, 4);

			f.write((char*)&mat.matEmissionColor.r, 4);
			f.write((char*)&mat.matEmissionColor.g, 4);
			f.write((char*)&mat.matEmissionColor.b, 4);

			int numTexs = mat.textures.size();
			f.write((char*)&numTexs, 4);
			for (int i = 0; i < numTexs; i++)
			{
				f.write((char*)&mat.textures[i].id, 4);
				int strsize = mat.textures[i].type.size() + 1;
				f.write((char*)&strsize, 4);
				f.write(mat.textures[i].type.c_str(), strsize);
			}

			int isCutOut = mat.isCutOut;
			f.write((char*)&isCutOut, 4);

			int isTransparent = mat.isTransparent;
			f.write((char*)&isTransparent, 4);
		}

		N = meshLights.size();
		f.write((char*)&N, 4);
		for (int i = 0; i < N; i++)
		{
			CPUMeshLight& ml = meshLights[i];
			f.write((char*)&ml.indexOffset, 4);
			f.write((char*)&ml.indexCount, 4);
			f.write((char*)&ml.vertexOffset, 4);
			f.write((char*)&ml.emitMatId, 4);
			f.write((char*)&ml.geomId, 4);
			f.write((char*)&ml.numTriangles, 4);
			f.write((char*)&ml.emission.r, 4);
			f.write((char*)&ml.emission.g, 4);
			f.write((char*)&ml.emission.b, 4);
		}

		int strsize = directory.size() + 1;
		f.write((char*)&strsize, 4);
		f.write(directory.c_str(), strsize);

		int gcrt = gammaCorrection;
		f.write((char*)&gcrt, 4);

		f.write((char*)&scene_sphere_pos.x, 4);
		f.write((char*)&scene_sphere_pos.y, 4);
		f.write((char*)&scene_sphere_pos.z, 4);

		f.write((char*)&scene_sphere_radius, 4);

		f.close();
	}

	void LoadDump(std::string filename)
	{
		std::cout << "Loading dump file \"" << filename << "\"...";
		FILE *fp;
		fp = fopen(filename.c_str(), "rb");

		int N;
		fread((char*)&N, 1, 4, fp);
		textures_loaded.resize(N);
		char buffer[1024];
		for (int i = 0; i < N; i++)
		{
			CPUTexture& tex = textures_loaded[i];
			fread((char*)&tex.width, 1, 4, fp);
			fread((char*)&tex.height, 1, 4, fp);
			fread((char*)&tex.nrComponents, 1, 4, fp);
			int strsize;
			fread((char*)&strsize, 1, 4, fp);
			fread(buffer, 1, strsize, fp);
			tex.path = std::string(buffer);
			fread((char*)&strsize, 1, 4, fp);
			fread(buffer, 1, strsize, fp);
			tex.type = std::string(buffer);
			fread((char*)&tex.id, 1, 4, fp);
			tex.data = new unsigned char[tex.width*tex.height*tex.nrComponents];
			fread((char*)tex.data, 1, tex.width*tex.height*tex.nrComponents, fp);
		}

		fread((char*)&N, 1, 4, fp);
		meshes.resize(N);

		for (int i = 0; i < N; i++)
		{
			CPUMesh& mesh = meshes[i];
			int N;
			fread((char*)&N, 1, 4, fp);
			mesh.vertices.resize(N);
			for (int i = 0; i < N; i++)
			{
				CPUVertex& vert = mesh.vertices[i];
				fread((char*)&vert.Position.x, 1, 4, fp);
				fread((char*)&vert.Position.y, 1, 4, fp);
				fread((char*)&vert.Position.z, 1, 4, fp);
				fread((char*)&vert.TexCoords.x, 1, 4, fp);
				fread((char*)&vert.TexCoords.y, 1, 4, fp);
				fread((char*)&vert.Normal.x, 1, 4, fp);
				fread((char*)&vert.Normal.y, 1, 4, fp);
				fread((char*)&vert.Normal.z, 1, 4, fp);
				fread((char*)&vert.Tangent.x, 1, 4, fp);
				fread((char*)&vert.Tangent.y, 1, 4, fp);
				fread((char*)&vert.Tangent.z, 1, 4, fp);
				fread((char*)&vert.Bitangent.x, 1, 4, fp);
				fread((char*)&vert.Bitangent.y, 1, 4, fp);
				fread((char*)&vert.Bitangent.z, 1, 4, fp);
			}

			fread((char*)&N, 1, 4, fp);
			mesh.indices.resize(N);
			for (int i = 0; i < N; i++)
			{
				fread((char*)&mesh.indices[i], 1, 4, fp);
			}

			fread((char*)&mesh.boundingBox.pos.x, 1, 4, fp);
			fread((char*)&mesh.boundingBox.pos.y, 1, 4, fp);
			fread((char*)&mesh.boundingBox.pos.z, 1, 4, fp);
			fread((char*)&mesh.boundingBox.end.x, 1, 4, fp);
			fread((char*)&mesh.boundingBox.end.y, 1, 4, fp);
			fread((char*)&mesh.boundingBox.end.z, 1, 4, fp);

			fread((char*)&mesh.matId, 1, 4, fp);
		}

		// load materials
		fread((char*)&N, 1, 4, fp);
		materials.resize(N);

		for (int i = 0; i < N; i++)
		{
			CPUMaterial& mat = materials[i];

			fread((char*)&mat.matDiffuseColor.r, 1, 4, fp);
			fread((char*)&mat.matDiffuseColor.g, 1, 4, fp);
			fread((char*)&mat.matDiffuseColor.b, 1, 4, fp);

			fread((char*)&mat.matSpecularColor.r, 1, 4, fp);
			fread((char*)&mat.matSpecularColor.g, 1, 4, fp);
			fread((char*)&mat.matSpecularColor.b, 1, 4, fp);

			fread((char*)&mat.matEmissionColor.r, 1, 4, fp);
			fread((char*)&mat.matEmissionColor.g, 1, 4, fp);
			fread((char*)&mat.matEmissionColor.b, 1, 4, fp);

			int numTexs;
			fread((char*)&numTexs, 1, 4, fp);
			mat.textures.resize(numTexs);
			for (int i = 0; i < numTexs; i++)
			{
				fread((char*)&mat.textures[i].id, 1, 4, fp);
				mat.textures[i] = textures_loaded[mat.textures[i].id];

				int strsize;
				fread((char*)&strsize, 1, 4, fp);
				fread(buffer, 1, strsize, fp);
				mat.textures[i].type = std::string(buffer);
			}

			int isCutOut;
			fread((char*)&isCutOut, 1, 4, fp);
			mat.isCutOut = isCutOut;

			int isTransparent;
			fread((char*)&isTransparent, 1, 4, fp);
			mat.isTransparent = isTransparent;
		}

		fread((char*)&N, 1, 4, fp);
		meshLights.resize(N);
		for (int i = 0; i < N; i++)
		{
			CPUMeshLight& ml = meshLights[i];
			fread((char*)&ml.indexOffset, 1, 4, fp);
			fread((char*)&ml.indexCount, 1, 4, fp);
			fread((char*)&ml.vertexOffset, 1, 4, fp);
			fread((char*)&ml.emitMatId, 1, 4, fp);
			fread((char*)&ml.geomId, 1, 4, fp);
			fread((char*)&ml.numTriangles, 1, 4, fp);
			fread((char*)&ml.emission.r, 1, 4, fp);
			fread((char*)&ml.emission.g, 1, 4, fp);
			fread((char*)&ml.emission.b, 1, 4, fp);
		}

		int strsize;
		fread((char*)&strsize, 1, 4, fp);
		fread(buffer, 1, strsize, fp);
		directory = std::string(buffer);

		int gcrt;
		fread((char*)&gcrt, 1, 4, fp);
		gammaCorrection = gcrt;

		fread((char*)&scene_sphere_pos.x, 1, 4, fp);
		fread((char*)&scene_sphere_pos.y, 1, 4, fp);
		fread((char*)&scene_sphere_pos.z, 1, 4, fp);

		fread((char*)&scene_sphere_radius, 1, 4, fp);
		fclose(fp);
	}

	void computeRitterBoundingSphere()
	{
		glm::vec3 x = meshes[0].vertices[0].Position;
		float maxDist = 0, yi = 0, yj = 0, zi = 0, zj = 0;
		for (int i = 0; i < meshes.size(); i++)
		{
			aabb meshBounds;
			for (int j = 0; j < meshes[i].vertices.size(); j++)
			{
				meshBounds.Union(meshes[i].vertices[j].Position);
				float dist = length(meshes[i].vertices[j].Position - x);
				if (dist > maxDist)
				{
					maxDist = dist;
					yi = i;
					yj = j;
				}
			}
			meshes[i].boundingBox = meshBounds;
		}
		maxDist = 0;
		glm::vec3 y = meshes[yi].vertices[yj].Position;
		for (int i = 0; i < meshes.size(); i++)
		{
			for (int j = 0; j < meshes[i].vertices.size(); j++)
			{
				float dist = length(meshes[i].vertices[j].Position - y);
				if (dist > maxDist)
				{
					maxDist = dist;
					zi = i;
					zj = j;
				}
			}
		}

		glm::vec3 z = meshes[zi].vertices[zj].Position;
		glm::vec3 center(0.5f*(y + z));
		float radius = 0.5f * length(y - z);
		for (int i = 0; i < meshes.size(); i++)
		{
			for (int j = 0; j < meshes[i].vertices.size(); j++)
			{
				float dist = length(meshes[i].vertices[j].Position - center);
				if (dist > radius)
				{
					glm::vec3 extra = meshes[i].vertices[j].Position;
					center = center + 0.5f*(dist - radius)*normalize(extra - center);
					radius = 0.5*(dist + radius);
				}
			}
		}
		scene_sphere_pos = center;
		scene_sphere_radius = radius;
	}
	/*  Functions   */
	// loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
	void loadModel(std::string const &path)
	{
		std::cout << "Loading model \"" << path << "\"...\n";
		// read file via ASSIMP
		Assimp::Importer importer;
		uint32_t assimpFlags = aiProcessPreset_TargetRealtime_MaxQuality | 0;
		// these flags are used in assimp importer in Falcor 4.0
		assimpFlags &= ~(aiProcess_CalcTangentSpace); // Never use Assimp's tangent gen code
		assimpFlags &= ~(aiProcess_FindDegenerates); // Avoid converting degenerated triangles to lines
		assimpFlags &= ~(aiProcess_OptimizeGraph); // Never use as it doesn't handle transforms with negative determinants
		assimpFlags &= ~(aiProcess_RemoveRedundantMaterials); // Avoid merging materials

		const aiScene* scene = importer.ReadFile(path, assimpFlags);
		// check for errors
		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
		{
			std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
			return;
		}
		// retrieve the directory path of the filepath
		directory = path.substr(0, path.find_last_of('/'));

		// load material
		processMaterial(scene);

		int geomId = 0;

		// read meshes
		for (unsigned int i = 0; i < scene->mNumMeshes; i++)
		{
			// the node object only contains indices to index the actual objects in the scene. 
			// the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
			aiMesh* mesh = scene->mMeshes[i];
			meshes.push_back(processMesh(mesh, scene, geomId));
			geomId++;
		}

		// read scene graph
		processNode(scene->mRootNode, scene, -1);

		// read animation
		for (uint32_t i = 0; i < scene->mNumAnimations; i++)
		{
			std::shared_ptr<Animation> pAnimation = processAnimation(scene->mAnimations[i]);
			animations.push_back(pAnimation);
		}

		if (scene->HasCameras())
		{
			aiMatrix4x4 aiMat;
			aiCamera* pAiCamera = scene->mCameras[0];
			pAiCamera->GetCameraMatrix(aiMat);
			//initialCameraMatrix = aiCast(aiMat);

			glm::vec3 position = aiCast(pAiCamera->mPosition);
			glm::vec3 up = aiCast(pAiCamera->mUp);
			glm::vec3 lookAt = aiCast(pAiCamera->mLookAt) + position;
			initialCameraMatrix = glm::lookAt(position, lookAt, up);

			aiString camName = scene->mCameras[0]->mName;
			CameraNodeID = mAiToSceneNodeID[mAiNodes.at(camName.C_Str())[0]];
		}
	}

	void processMaterial(const aiScene *scene)
	{
		for (int matId = 0; matId < scene->mNumMaterials; matId++)
		{
			aiMaterial* material = scene->mMaterials[matId];

			if (matId + matIdOffset >= materials.size())
			{
				std::vector<CPUTexture> textures;
				//materials
				// 1. diffuse maps
				std::vector<CPUTexture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
				textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
				// 2. specular maps
				std::vector<CPUTexture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
				textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
				// 3. normal maps
				std::vector<CPUTexture> normalMaps = loadMaterialTextures(material, aiTextureType_NORMALS, "texture_normals");
				textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
				// 4. emission maps
				std::vector<CPUTexture> emitMaps = loadMaterialTextures(material, aiTextureType_EMISSIVE, "texture_emissive");
				textures.insert(textures.end(), emitMaps.begin(), emitMaps.end());

				aiMaterial* mat = scene->mMaterials[matId];
				aiColor3D diffuse(0.f, 0.f, 0.f), specular(0.f, 0.f, 0.f);
				mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
				mat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
				// return a mesh object created from the extracted mesh data

				aiColor3D emission(0.f);
				mat->Get(AI_MATKEY_COLOR_EMISSIVE, emission);

				bool isCutOut = mat->GetTextureCount(aiTextureType_OPACITY) > 0;

				aiColor3D trans(0.f);
				mat->Get(AI_MATKEY_COLOR_TRANSPARENT, trans);

				materials.push_back(CPUMaterial(textures, diffuse, specular, emission, isCutOut, trans.r < 1.0 || trans.g < 1.0 || trans.b < 1.0));
			}
		}
	}

	glm::mat4 aiCast(const aiMatrix4x4& aiMat)
	{
		glm::mat4 ret;

		for (int i = 0; i < 4; i++)
		{
			ret[i][0] = aiMat[0][i];
			ret[i][1] = aiMat[1][i];
			ret[i][2] = aiMat[2][i];
			ret[i][3] = aiMat[3][i];
		}

		return ret;
	}

	glm::vec3 aiCast(const aiColor3D& ai)
	{
		return glm::vec3(ai.r, ai.g, ai.b);
	}

	glm::vec3 aiCast(const aiVector3D& val)
	{
		return glm::vec3(val.x, val.y, val.z);
	}

	glm::quat aiCast(const aiQuaternion& q)
	{
		return glm::quat(q.w, q.x, q.y, q.z);
	}

	void addAiNode(const aiNode* pNode, uint32_t sceneNodeID)
	{
		assert(mAiToSceneNodeID.find(pNode) == mAiToSceneNodeID.end());
		mAiToSceneNodeID[pNode] = sceneNodeID;
		if (mAiNodes.find(pNode->mName.C_Str()) == mAiNodes.end())
		{
			mAiNodes[pNode->mName.C_Str()] = {};
		}
		mAiNodes[pNode->mName.C_Str()].push_back(pNode);
	}

	uint32_t getNodeInstanceCount(const std::string& nodeName) const
	{
		return (uint32_t)mAiNodes.at(nodeName).size();
	}

	template<typename AiType, typename EngineType>
	bool parseAnimationChannel(const AiType* pKeys, uint32_t count, double time, uint32_t& currentIndex, EngineType& engineVal)
	{
		if (currentIndex >= count) return true;

		if (pKeys[currentIndex].mTime == time)
		{
			engineVal = aiCast(pKeys[currentIndex].mValue);
			currentIndex++;
		}

		return currentIndex >= count;
	}

	// processes a node in a recursive fashion. (depth first order)
	void processNode(aiNode *node, const aiScene *scene, int parentNodeId)
	{
		int nodeId = sceneNodes.size();
		sceneNodes.push_back(CPUSceneNode());
		sceneNodes[nodeId].parentNodeID = parentNodeId;
		sceneNodes[nodeId].modelMatrix = aiCast(node->mTransformation);

		for (unsigned int i = 0; i < node->mNumMeshes; i++)
		{
			// this creates mesh instances (with current nodeId being the global matrix ID)
			meshInstances.push_back(CPUMeshInstance(node->mMeshes[i], nodeId));
			meshes[node->mMeshes[i]].instances.push_back(nodeId);
			if (meshes[node->mMeshes[i]].meshlightId != -1)
			{
				meshlightInstances.push_back(CPUMeshInstance(meshes[node->mMeshes[i]].meshlightId, nodeId)); // this nodeId refer to global matrix id
			}
		}

		addAiNode(node, nodeId);

		// after we've processed all of the meshes (if any) we then recursively process each of the children nodes
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			processNode(node->mChildren[i], scene, nodeId);
		}
	}

	void resetNegativeKeyframeTimes(aiNodeAnim* pAiNode)
	{
		auto resetTime = [](auto keys, uint32_t count)
		{
			if (count > 1) assert(keys[1].mTime >= 0);
			if (keys[0].mTime < 0) keys[0].mTime = 0;
		};
		resetTime(pAiNode->mPositionKeys, pAiNode->mNumPositionKeys);
		resetTime(pAiNode->mRotationKeys, pAiNode->mNumRotationKeys);
		resetTime(pAiNode->mScalingKeys, pAiNode->mNumScalingKeys);
	}

	std::shared_ptr<Animation> processAnimation(aiAnimation* pAiAnim)
	{
		assert(pAiAnim->mNumMeshChannels == 0);
		double duration = pAiAnim->mDuration;
		double ticksPerSecond = pAiAnim->mTicksPerSecond ? pAiAnim->mTicksPerSecond : 25;
		double durationInSeconds = duration / ticksPerSecond;

		std::shared_ptr<Animation> pAnimation = std::make_shared<Animation>(pAiAnim->mName.C_Str(), durationInSeconds);

		for (uint32_t i = 0; i < pAiAnim->mNumChannels; i++)
		{
			aiNodeAnim* pAiNode = pAiAnim->mChannels[i];
			resetNegativeKeyframeTimes(pAiNode);

			std::vector<size_t> channels;


			for (uint32_t i = 0; i < getNodeInstanceCount(pAiNode->mNodeName.C_Str()); i++)
			{
				int nodeID = mAiToSceneNodeID[mAiNodes.at(pAiNode->mNodeName.C_Str())[i]];
				channels.push_back(pAnimation->addChannel(nodeID));
			}

			uint32_t pos = 0, rot = 0, scale = 0;
			Animation::Keyframe keyframe;
			bool done = false;

			auto nextKeyTime = [&]()
			{
				double time = -std::numeric_limits<double>::max();
				if (pos < pAiNode->mNumPositionKeys) time = std::max(time, pAiNode->mPositionKeys[pos].mTime);
				if (rot < pAiNode->mNumRotationKeys) time = std::max(time, pAiNode->mRotationKeys[rot].mTime);
				if (scale < pAiNode->mNumScalingKeys) time = std::max(time, pAiNode->mScalingKeys[scale].mTime);
				assert(time != -std::numeric_limits<double>::max());
				return time;
			};

			while (!done)
			{
				double time = nextKeyTime();
				assert(time == 0 || (time / ticksPerSecond) > keyframe.time);
				keyframe.time = time / ticksPerSecond;

				// Note the order of the logical-and, we don't want to short-circuit the function calls
				done = parseAnimationChannel(pAiNode->mPositionKeys, pAiNode->mNumPositionKeys, time, pos, keyframe.translation);
				done = parseAnimationChannel(pAiNode->mRotationKeys, pAiNode->mNumRotationKeys, time, rot, keyframe.rotation) && done;
				done = parseAnimationChannel(pAiNode->mScalingKeys, pAiNode->mNumScalingKeys, time, scale, keyframe.scaling) && done;
				for (auto c : channels) pAnimation->addKeyframe(c, keyframe);
			}
		}

		return pAnimation;
	}


	CPUMesh processMesh(aiMesh *mesh, const aiScene *scene, int geomId);

	// checks all material textures of a given type and loads the textures if they're not loaded yet.
	// the required info is returned as a Texture struct.
	std::vector<CPUTexture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, std::string typeName)
	{
		std::vector<CPUTexture> textures;
		for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
		{
			aiString str;
			mat->GetTexture(type, i, &str);
			// check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
			bool skip = false;

			for (unsigned int j = 0; j < textures_loaded.size(); j++)
			{
				if (std::strcmp(textures_loaded[j].path.c_str(), str.C_Str()) == 0)
				{
					textures.push_back(textures_loaded[j]);
					textures.back().type = typeName; // same texture might be used for different types
					skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
					break;
				}
			}
			if (!skip)
			{   // if texture hasn't been loaded already, load it
				CPUTexture texture(std::string(str.C_Str()), this->directory, false, typeName);
				texture.id = textures_loaded.size();
				textures.push_back(texture);
				textures_loaded.push_back(texture);  // store it as texture loaded for entire model, to ensure we won't unnecesery load duplicate textures.
			}
		}
		return textures;
	}
};
