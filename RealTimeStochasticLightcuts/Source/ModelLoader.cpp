// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files (the "Software"), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all 
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.

//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  Alex Nankervis
//

#include "ModelLoader.h"
#include "Utility.h"
#include "TextureManager.h"
#include "GraphicsCore.h"
#include "DescriptorHeap.h"
#include "CommandContext.h"
#include <unordered_map>
#include <map>
#include <iostream>
#include "TestUtils.h"

bool Model1::LoadAssimpModel(const std::vector<std::string>& filenames)
{
	CPUModel cpuModel(filenames);

	for (int i = 0; i < cpuModel.animations.size(); i++)
	{
		m_Animations.push_back(cpuModel.animations[i]);
	}

	if (cpuModel.CameraNodeID != -1)
	{
		initialCameraMatrix = cpuModel.initialCameraMatrix;
		CameraNodeID = cpuModel.CameraNodeID;
	}

	int numMeshes = cpuModel.meshes.size();
	int numMaterials = cpuModel.materials.size();
	m_pMaterial = new Material[numMaterials]; // in our model each mesh has its own material
	m_pMesh = new Mesh[numMeshes];
	meshCentroids = new glm::vec3[numMeshes];
	meshModelMatrices = new glm::mat4[numMeshes];

	int numVerticesTotal = 0;
	int numIndicesTotal = 0;
	int numInstancesTotal = 0;
	m_Header.meshCount = numMeshes;
	m_Header.materialCount = numMaterials;
	m_pMaterialIsCutout.resize(m_Header.materialCount, false);
	m_pMaterialIsTransparent.resize(m_Header.materialCount, false);

	std::vector<CPUVertex> CPUVertexBuffer;
	std::vector<unsigned int> CPUIndexBuffer;

	for (int meshId = 0; meshId < numMeshes; meshId++)
	{
		Mesh mesh;
		const CPUMesh& cpuMesh = cpuModel.meshes[meshId];
		mesh.vertexCount = cpuMesh.vertices.size();
		numVerticesTotal += mesh.vertexCount;
		mesh.vertexDataByteOffset = CPUVertexBuffer.size() * sizeof(CPUVertex);
		CPUVertexBuffer.insert(CPUVertexBuffer.end(), cpuMesh.vertices.begin(), cpuMesh.vertices.end());
		mesh.indexCount = cpuMesh.indices.size();
		numIndicesTotal += mesh.indexCount;
		mesh.indexDataByteOffset = CPUIndexBuffer.size() * sizeof(unsigned int);
		CPUIndexBuffer.insert(CPUIndexBuffer.end(), cpuMesh.indices.begin(), cpuMesh.indices.end());
		mesh.vertexStride = sizeof(CPUVertex);
		m_CPUInstanceBuffer.insert(m_CPUInstanceBuffer.end(), cpuMesh.instances.begin(), cpuMesh.instances.end());
		mesh.instanceCount = cpuMesh.instances.size();
		mesh.instanceListOffset = numInstancesTotal;
		numInstancesTotal += mesh.instanceCount;
		mesh.materialIndex = cpuMesh.matId;
		meshCentroids[meshId] = cpuMesh.boundingBox.centroid();
		aabb meshBB = cpuMesh.boundingBox;
		mesh.boundingBox.min = Vector3(meshBB.pos.x, meshBB.pos.y, meshBB.pos.z);
		mesh.boundingBox.max = Vector3(meshBB.end.x, meshBB.end.y, meshBB.end.z);
		m_pMesh[meshId] = mesh;
	}

	m_NumMeshInstancesTotal = numInstancesTotal;

	int numNodes = cpuModel.sceneNodes.size();
	m_CPULocalMatrices.resize(numNodes);
	m_CPUGlobalMatrices.resize(numNodes);
	m_CPUGlobalInvTransposeMatrices.resize(numNodes);
	m_MatrixParentId.resize(numNodes);

	// assign meshes to mesh groups (meshes in a group share the same global matrix)
	std::map<int, std::vector<int>> meshGroupMap;

	for (int meshId = 0; meshId < numMeshes; meshId++)
	{
		Mesh& mesh = m_pMesh[meshId];
		if (mesh.instanceCount == 1)
		{
			int matrixId = m_CPUInstanceBuffer[mesh.instanceListOffset];
			meshGroupMap[matrixId].push_back(meshId);
		}
	}

	for (const auto& it : meshGroupMap) m_MeshGroups.push_back(it.second);

	m_NumBLASInstancesTotal = m_MeshGroups.size();

	for (int meshId = 0; meshId < numMeshes; meshId++)
	{
		Mesh& mesh = m_pMesh[meshId];
		if (mesh.instanceCount > 1)
		{
			m_NumBLASInstancesTotal += mesh.instanceCount;
			m_MeshGroups.push_back({ meshId });
		}
	}

	for (int nodeId = 0; nodeId < numNodes; nodeId++)
	{
		m_CPULocalMatrices[nodeId] = cpuModel.sceneNodes[nodeId].modelMatrix;
		int parentId = cpuModel.sceneNodes[nodeId].parentNodeID;
		m_MatrixParentId[nodeId] = parentId;
		if (parentId != -1) m_CPUGlobalMatrices[nodeId] = m_CPUGlobalMatrices[parentId] * m_CPULocalMatrices[nodeId];
		else m_CPUGlobalMatrices[nodeId] = m_CPULocalMatrices[nodeId];
		m_CPUGlobalInvTransposeMatrices[nodeId] = glm::transpose(glm::inverse(m_CPUGlobalMatrices[nodeId]));
	}

	m_GlobalMatrixBuffer.Create(L"GlobalMatrixBuffer", m_CPUGlobalMatrices.size(), sizeof(glm::mat4), m_CPUGlobalMatrices.data());
	m_PreviousGlobalMatrixBuffer.Create(L"PreviousGlobalMatrixBuffer", m_CPUGlobalMatrices.size(), sizeof(glm::mat4), m_CPUGlobalMatrices.data());
	m_GlobalInvTransposeMatrixBuffer.Create(L"GlobalInvTransposeMatrixBuffer", m_CPUGlobalInvTransposeMatrices.size(), sizeof(glm::mat4), m_CPUGlobalInvTransposeMatrices.data());

	for (int matId = 0; matId < numMaterials; matId++)
	{
		m_pMaterialIsCutout[matId] = cpuModel.materials[matId].isCutOut;
		m_pMaterialIsTransparent[matId] = false;// cpuModel.materials[matId].isTransparent;
		m_pMaterial[matId].diffuse = cpuModel.materials[matId].matDiffuseColor.GetVector3();
		m_pMaterial[matId].specular = cpuModel.materials[matId].matSpecularColor.GetVector3();
		m_pMaterial[matId].emissive = cpuModel.materials[matId].matEmissionColor.GetVector3();
	}


	for (int id = 0; id < cpuModel.meshLights.size(); id++)
	{
		int meshId = cpuModel.meshLights[id].geomId;
		auto& mesh = m_pMesh[meshId];

		cpuModel.meshLights[id].instanceOffset = m_CPUMeshLightInstancesBuffer.size();
		cpuModel.meshLights[id].instanceCount = mesh.instanceCount;

		for (int instId = 0; instId < mesh.instanceCount; instId++)
		{
			m_CPUMeshlightIdForInstancesBuffer.push_back(id);
			m_CPUMeshLightInstancesBuffer.push_back(m_CPUInstanceBuffer[mesh.instanceListOffset + instId]);

			auto& globalMatrix = m_CPUGlobalMatrices[m_CPUInstanceBuffer[mesh.instanceListOffset + instId]];
		}

		int vertexBufferId = mesh.vertexDataByteOffset / sizeof(CPUVertex);
		int indexBufferId = mesh.indexDataByteOffset / sizeof(unsigned int);
		int numTriangles = cpuModel.meshLights[id].numTriangles;
		cpuModel.meshLights[id].vertexOffset = m_CPUMeshLightVertexBuffer.size();

		for (int vertId = 0; vertId < mesh.vertexCount; vertId++)
		{
			m_CPUMeshLightVertexBuffer.push_back({ CPUVertexBuffer[vertexBufferId + vertId].Position, CPUVertexBuffer[vertexBufferId + vertId].Normal, CPUVertexBuffer[vertexBufferId + vertId].TexCoords });
		}
		cpuModel.meshLights[id].indexOffset = m_CPUMeshLightIndexBuffer.size();

		for (int triId = 0; triId < numTriangles; triId++)
		{
			for (int vertId = 0; vertId < 3; vertId++)
			{
				int oldIndex = CPUIndexBuffer[indexBufferId + 3 * triId + vertId];
				m_CPUMeshLightIndexBuffer.push_back(oldIndex);
			}
		}

		cpuModel.meshLights[id].indexCount = m_CPUMeshLightIndexBuffer.size() - cpuModel.meshLights[id].indexOffset;
	}

	m_CPUEmissiveTriangleIntensityBuffer.resize(m_CPUMeshLightIndexBuffer.size() / 3, 0.f);

	m_MeshLightIndexBuffer.Create(L"MeshLightIndexBuffer", m_CPUMeshLightIndexBuffer.size(), sizeof(unsigned int), m_CPUMeshLightIndexBuffer.data());
	m_MeshLightVertexBuffer.Create(L"MeshLightVertexBuffer", m_CPUMeshLightVertexBuffer.size(), sizeof(EmissiveVertex), m_CPUMeshLightVertexBuffer.data());
	m_MeshLightTriangleIntensityBuffer.Create(L"MeshLightTriangleIntensityBuffer", m_CPUEmissiveTriangleIntensityBuffer.size(), sizeof(unsigned), m_CPUEmissiveTriangleIntensityBuffer.data());
	m_MeshLightTriangleNumberOfTexelsBuffer.Create(L"MeshLightTriangleNumberOfTexelsBuffer", m_CPUEmissiveTriangleIntensityBuffer.size(), sizeof(unsigned), m_CPUEmissiveTriangleIntensityBuffer.data());
	m_MeshlightIdForInstancesBuffer.Create(L"m_MeshlightIdForInstancesBuffer", m_CPUMeshlightIdForInstancesBuffer.size(), sizeof(unsigned), m_CPUMeshlightIdForInstancesBuffer.data());

	m_VertexStride = sizeof(CPUVertex);
	m_VertexBuffer.Create(L"VertexBuffer", numVerticesTotal, sizeof(CPUVertex), CPUVertexBuffer.data());
	m_IndexBuffer.Create(L"IndexBuffer", numIndicesTotal, sizeof(unsigned int), CPUIndexBuffer.data());

	m_InstanceBuffer.Create(L"InstanceBuffer", numInstancesTotal, sizeof(unsigned int), m_CPUInstanceBuffer.data());

	m_Header.vertexDataByteSize = numVerticesTotal * sizeof(CPUVertex);
	m_Header.indexDataByteSize = numIndicesTotal * sizeof(unsigned int);

	ComputeGlobalBoundingBoxWithInstanceTransform(m_Header.boundingBox);

	LoadAssimpTextures(cpuModel);

	Vector3 globalBoundingBoxExtent = m_Header.boundingBox.max - m_Header.boundingBox.min;
	Vector3 globalBoundingBoxCenter = 0.5f*(m_Header.boundingBox.max + m_Header.boundingBox.min);
	float boundingboxRadius = Length(globalBoundingBoxExtent) * .5f;
	m_SceneBoundingSphere =
		Vector4(globalBoundingBoxCenter, boundingboxRadius);

	indexSize = 4;

	m_CPUMeshLights = cpuModel.meshLights;

	return true;
}

bool Model1::LoadDemoScene(const char *filename)
{
	std::cout << "Loading model \"" << filename << "\"...\n";
	FILE *file = nullptr;
	if (0 != fopen_s(&file, filename, "rb"))
		return false;

	bool ok = false;

	fread(&m_Header, sizeof(Header), 1, file);

	m_Header.meshCount--;
	m_pMesh = new Mesh[m_Header.meshCount];
	m_pMaterial = new Material[m_Header.materialCount];
	m_pMaterialIsTransparent.resize(m_Header.materialCount, false);
	m_emissiveTexId.resize(m_Header.materialCount, -1);

	Mesh unusedMesh;
	int unusedMeshIndex = 4;

	for (int meshId = 0; meshId < m_Header.meshCount; meshId++)
	{
		if (meshId == unusedMeshIndex) fread(&unusedMesh, sizeof(Mesh) - 16, 1, file);
		fread(m_pMesh + meshId, sizeof(Mesh)-16, 1, file);
	}

	fread(m_pMaterial, sizeof(Material) * m_Header.materialCount, 1, file);

	m_VertexStride = m_pMesh[0].vertexStride;
	m_VertexStrideDepth = m_pMesh[0].vertexStrideDepth;

	uint32_t vertexSkip = m_pMesh[unusedMeshIndex].vertexDataByteOffset - unusedMesh.vertexDataByteOffset;
	uint32_t indexSkip = m_pMesh[unusedMeshIndex].indexDataByteOffset - unusedMesh.indexDataByteOffset;
	uint32_t vertexDepthSkip = m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth - unusedMesh.vertexDataByteOffsetDepth;

	m_Header.vertexDataByteSize -= vertexSkip;
	m_Header.indexDataByteSize -= indexSkip;
	m_Header.vertexDataByteSizeDepth -= vertexDepthSkip;

	for (uint32_t meshIndex = unusedMeshIndex; meshIndex < m_Header.meshCount; ++meshIndex)
	{
		Mesh& mesh = m_pMesh[meshIndex];
		mesh.vertexDataByteOffset -= vertexSkip;
		mesh.indexDataByteOffset -= indexSkip;
		mesh.vertexDataByteOffsetDepth -= vertexDepthSkip;
	}

	m_pVertexData = new unsigned char[m_Header.vertexDataByteSize];
	m_pIndexData = new unsigned char[m_Header.indexDataByteSize];
	m_pVertexDataDepth = new unsigned char[m_Header.vertexDataByteSizeDepth];
	m_pIndexDataDepth = new unsigned char[m_Header.indexDataByteSize];

	fread(m_pVertexData, m_pMesh[unusedMeshIndex].vertexDataByteOffset, 1, file);
	fseek(file, vertexSkip, SEEK_CUR);
	fread(m_pVertexData + m_pMesh[unusedMeshIndex].vertexDataByteOffset, m_Header.vertexDataByteSize - m_pMesh[unusedMeshIndex].vertexDataByteOffset, 1, file);
	fread(m_pIndexData, m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);
	fseek(file, indexSkip, SEEK_CUR);
	fread(m_pIndexData + m_pMesh[unusedMeshIndex].indexDataByteOffset, m_Header.indexDataByteSize - m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);

	fread(m_pVertexDataDepth, m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth, 1, file);
	fseek(file, vertexDepthSkip, SEEK_CUR);
	fread(m_pVertexDataDepth + m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth, m_Header.vertexDataByteSizeDepth - m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth, 1, file);
	fread(m_pIndexDataDepth, m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);
	fseek(file, indexSkip, SEEK_CUR);
	fread(m_pIndexDataDepth + m_pMesh[unusedMeshIndex].indexDataByteOffset, m_Header.indexDataByteSize - m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);

	m_VertexBuffer.Create(L"VertexBuffer", m_Header.vertexDataByteSize / m_VertexStride, m_VertexStride, m_pVertexData);
	m_IndexBuffer.Create(L"IndexBuffer", m_Header.indexDataByteSize / sizeof(uint16_t), sizeof(uint16_t), m_pIndexData);

	m_VertexBufferDepth.Create(L"VertexBufferDepth", m_Header.vertexDataByteSizeDepth / m_VertexStrideDepth, m_VertexStrideDepth, m_pVertexDataDepth);
	m_IndexBufferDepth.Create(L"IndexBufferDepth", m_Header.indexDataByteSize / sizeof(uint16_t), sizeof(uint16_t), m_pIndexDataDepth);

	LoadDemoTextures();

	for (uint32_t meshIndex = 0; meshIndex < m_Header.meshCount; ++meshIndex)
	{
		Mesh& mesh = m_pMesh[meshIndex];
		m_pMaterial[mesh.materialIndex].diffuse = Vector3(1);
		mesh.instanceListOffset = 0;
		mesh.instanceCount = 1;
	}

	ComputeSceneBoundingSphere();

	m_pMaterialIsCutout.resize(m_Header.materialCount);

	for (uint32_t i = 0; i < m_Header.materialCount; ++i)
	{
		const Model1::Material& mat = m_pMaterial[i];
		if (std::string(mat.texDiffusePath).find("thorn") != std::string::npos ||
			std::string(mat.texDiffusePath).find("plant") != std::string::npos ||
			std::string(mat.texDiffusePath).find("chain") != std::string::npos)
		{
			m_pMaterialIsCutout[i] = true;
		}
		else
		{
			m_pMaterialIsCutout[i] = false;
		}
	}

	indexSize = 2;

	// for static scene, everything belong to the same mesh group
	m_MeshGroups.resize(1);
	for (int meshId = 0; meshId < m_Header.meshCount; meshId++)
	{
		m_MeshGroups[0].push_back(meshId);
	}
	m_NumMeshInstancesTotal = m_Header.meshCount;
	m_NumBLASInstancesTotal = 1;
	
	m_CPUInstanceBuffer.push_back(0);

	m_CPULocalMatrices.resize(1);
	m_CPUGlobalMatrices.resize(1);
	m_CPUGlobalInvTransposeMatrices.resize(1);
	m_MatrixParentId.resize(1, -1);

	m_GlobalMatrixBuffer.Create(L"GlobalMatrixBuffer", m_CPUGlobalMatrices.size(), sizeof(glm::mat4), m_CPUGlobalMatrices.data());
	m_PreviousGlobalMatrixBuffer.Create(L"PreviousGlobalMatrixBuffer", m_CPUGlobalMatrices.size(), sizeof(glm::mat4), m_CPUGlobalMatrices.data());
	m_GlobalInvTransposeMatrixBuffer.Create(L"GlobalInvTransposeMatrixBuffer", m_CPUGlobalInvTransposeMatrices.size(), sizeof(glm::mat4), m_CPUGlobalInvTransposeMatrices.data());

	m_InstanceBuffer.Create(L"InstanceBuffer", 1, sizeof(unsigned int), m_CPUInstanceBuffer.data());

	ok = true;

h3d_load_fail:

	if (EOF == fclose(file))
		ok = false;

	return ok;
}

void Model1::LoadAssimpTextures(CPUModel& model)
{
	m_SRVs.resize(m_Header.materialCount * 3);
	m_TextureFlags.resize(m_Header.materialCount * 3, 0);
	m_emissiveTexId.resize(m_Header.materialCount);
	const Texture* MatTextures[3] = {};
	bool hasTexType[3] = { false, false, false };
	int uniqueEmissiveTextures = 0;
	std::unordered_map<int, int> uniqueTextureId;
	std::unordered_map<int, int> uniqueEmissiveTextureId;

	for (uint32_t materialIdx = 0; materialIdx < m_Header.materialCount; ++materialIdx)
	{
		hasTexType[0] = false;
		hasTexType[1] = false;
		hasTexType[2] = false;

		const Material& pMaterial = m_pMaterial[materialIdx];

		m_emissiveTexId[materialIdx] = -1;

		auto isDDSFile = [](const std::string& path) { return path.substr(path.find_last_of('.') + 1) == "dds"; };

		for (CPUTexture tex : model.materials[materialIdx].textures)
		{
			if (tex.type == "texture_emissive")
			{
				if (uniqueEmissiveTextureId.find(tex.id) == uniqueEmissiveTextureId.end())
				{
					const ManagedTexture* temp = isDDSFile(tex.path) ? TextureManager::LoadDDSFromFile(model.directory + "/" + tex.path, true) :
										TextureManager::LoadFromRawData(model.directory + "/" + tex.path, tex.width, tex.height, tex.nrComponents, tex.data);
					uniqueEmissiveTextureId[tex.id] = uniqueEmissiveTextures;
					m_emissiveTextureResources.push_back((Texture)*temp);
					m_emissiveSRVs.push_back(temp->GetSRV());
					if (isDDSFile(tex.path))
					{
						tex.width = m_emissiveTextureResources.back().GetResource()->GetDesc().Width;
						tex.height = m_emissiveTextureResources.back().GetResource()->GetDesc().Height;
					}
					m_emissiveTexDimensions.push_back(std::make_pair(tex.width, tex.height));
					m_emissiveTexId[materialIdx] = uniqueEmissiveTextures++;
				}
				else
				{
					m_emissiveTexId[materialIdx] = uniqueEmissiveTextureId[tex.id];
				}
				continue;
			}
			int idx = tex.type == "texture_diffuse" ? 0 : tex.type == "texture_specular" ? 1 : 2;
			hasTexType[idx] = true;

			if (uniqueTextureId.find(tex.id) == uniqueTextureId.end())
			{
				// replace "\" by "/"
				if (tex.path.find("tex") != std::string::npos)
				{
					tex.path.replace(3, 1, "/");
				}

				bool isDDS = isDDSFile(tex.path);
				ManagedTexture* temp = isDDS ? TextureManager::LoadDDSFromFile(model.directory + "/" + tex.path, idx == 0) : // metal-roughness map is not srgb
					TextureManager::LoadFromRawData(model.directory + "/" + tex.path, tex.width, tex.height, tex.nrComponents, tex.data);

				if (!temp->IsValid())
				{
					std::cout << "invalid! tex name: " << model.directory + "/" + tex.path << std::endl;
					hasTexType[idx] = false;
				}
				else
				{
					if (idx == 2 && isDDS)
					{
						DXGI_FORMAT texFormat = temp->GetResource()->GetDesc().Format;
						// two-element normal map
						if (texFormat >= DXGI_FORMAT_BC5_TYPELESS && texFormat <= DXGI_FORMAT_BC5_SNORM)
						{
							m_TextureFlags[materialIdx * 3 + idx] = 1;
						}
					}
					m_SRVs[materialIdx * 3 + idx] = temp->GetSRV();
				}
				uniqueTextureId[tex.id] = materialIdx * 3 + idx;
			}
			else
			{
				m_SRVs[materialIdx * 3 + idx] = m_SRVs[uniqueTextureId[tex.id]];
				m_TextureFlags[materialIdx * 3 + idx] = m_TextureFlags[uniqueTextureId[tex.id]];
			}
		}

		for (int idx = 0; idx < 3; idx++)
		{
			if (!hasTexType[idx])
			{
				std::string suffix = idx == 0 ? "" : idx == 1 ? "_specular" : "_normal";
				const Texture* temp = TextureManager::LoadFromFile("DefaultData/default" + suffix, idx != 2);
				m_SRVs[materialIdx * 3 + idx] = temp->GetSRV();
			}
		}
	}
}

void Model1::LoadDemoTextures()
{
	m_SRVs.resize(m_Header.materialCount * 3);
	m_TextureFlags.resize(m_Header.materialCount * 3, 2);

	const ManagedTexture* MatTextures[3] = {};

	for (uint32_t materialIdx = 0; materialIdx < m_Header.materialCount; ++materialIdx)
	{
		const Material& pMaterial = m_pMaterial[materialIdx];

		// Load diffuse
		MatTextures[0] = TextureManager::LoadFromFile("DefaultData/" + std::string(pMaterial.texDiffusePath), true);
		if (!MatTextures[0]->IsValid())
		{
			MatTextures[0] = TextureManager::LoadFromFile("DefaultData/default", true);
		}

		// Load specular
		MatTextures[1] = TextureManager::LoadFromFile("DefaultData/" + std::string(pMaterial.texSpecularPath), true);
		if (!MatTextures[1]->IsValid())
		{
			MatTextures[1] = TextureManager::LoadFromFile("DefaultData/" + std::string(pMaterial.texDiffusePath) + "_specular", true);
			if (!MatTextures[1]->IsValid())
			{
				MatTextures[1] = TextureManager::LoadFromFile("DefaultData/default_specular", true);
			}
		}

		// Load normal
		MatTextures[2] = TextureManager::LoadFromFile("DefaultData/" + std::string(pMaterial.texNormalPath), false);
		if (!MatTextures[2]->IsValid())
		{
			MatTextures[2] = TextureManager::LoadFromFile("DefaultData/" + std::string(pMaterial.texDiffusePath) + "_normal", false);
			if (!MatTextures[2]->IsValid())
			{
				MatTextures[2] = TextureManager::LoadFromFile("DefaultData/default_normal", false);
			}
		}

		m_SRVs[materialIdx * 3 + 0] = MatTextures[0]->GetSRV();
		m_SRVs[materialIdx * 3 + 1] = MatTextures[1]->GetSRV();
		m_SRVs[materialIdx * 3 + 2] = MatTextures[2]->GetSRV();
	}
}

Model1::Model1()
	: m_pMesh(nullptr)
	, m_pMaterial(nullptr)
	, m_pVertexData(nullptr)
	, m_pIndexData(nullptr)
	, m_pVertexDataDepth(nullptr)
	, m_pIndexDataDepth(nullptr)
{
	Clear();
}

Model1::~Model1()
{
	Clear();
}

void Model1::Clear()
{
	m_VertexBuffer.Destroy();
	m_IndexBuffer.Destroy();
	m_VertexBufferDepth.Destroy();
	m_IndexBufferDepth.Destroy();

	delete[] m_pMesh;
	m_pMesh = nullptr;
	m_Header.meshCount = 0;

	delete[] m_pMaterial;
	m_pMaterial = nullptr;
	m_Header.materialCount = 0;

	delete[] m_pVertexData;
	delete[] m_pIndexData;
	delete[] m_pVertexDataDepth;
	delete[] m_pIndexDataDepth;

	m_pVertexData = nullptr;
	m_Header.vertexDataByteSize = 0;
	m_pIndexData = nullptr;
	m_Header.indexDataByteSize = 0;
	m_pVertexDataDepth = nullptr;
	m_Header.vertexDataByteSizeDepth = 0;
	m_pIndexDataDepth = nullptr;

	m_Header.boundingBox.min = Vector3(0.0f);
	m_Header.boundingBox.max = Vector3(0.0f);
}

void Model1::PopulateMeshLightTriangleIntensityBuffer(GraphicsContext& gfxContext)
{
	std::vector<unsigned> retIntensBuffer = TestUtils::ReadBackCPUVector<unsigned>(gfxContext, m_MeshLightTriangleIntensityBuffer, m_CPUEmissiveTriangleIntensityBuffer.size());
	std::vector<unsigned> retNumberOfTexelsBuffer = TestUtils::ReadBackCPUVector<unsigned>(gfxContext, m_MeshLightTriangleNumberOfTexelsBuffer, m_CPUEmissiveTriangleIntensityBuffer.size());

	// with zero intensity triangles removed
	std::vector<unsigned> CPUMeshLightIndexBufferCompressed;
	std::vector<EmissiveVertex> CPUMeshLightVertexBufferCompressed;
	std::vector<CPUMeshLight> CPUMeshLightCompressed = m_CPUMeshLights;
	std::vector<float> CPUMeshLightIntensityBufferCompressed;

	int numMeshLights = m_CPUMeshLights.size();
	int triangleOffset = 0;

	std::unordered_map<int, int> indexMap;
	int uniqueIndexCount = 0;

	for (int id = 0; id < numMeshLights; id++)
	{
		CPUMeshLight& meshlight = m_CPUMeshLights[id];
		int numTriangles = meshlight.numTriangles;

		CPUMeshLightCompressed[id].indexOffset = CPUMeshLightIndexBufferCompressed.size();
		CPUMeshLightCompressed[id].vertexOffset = CPUMeshLightVertexBufferCompressed.size();

		for (int triId = 0; triId < numTriangles; triId++)
		{
			int v[3];
			v[0] = m_CPUMeshLightIndexBuffer[meshlight.indexOffset + 3 * triId];
			v[1] = m_CPUMeshLightIndexBuffer[meshlight.indexOffset + 3 * triId + 1];
			v[2] = m_CPUMeshLightIndexBuffer[meshlight.indexOffset + 3 * triId + 2];
			glm::vec3 p0 = m_CPUMeshLightVertexBuffer[meshlight.vertexOffset + v[0]].position;
			glm::vec3 p1 = m_CPUMeshLightVertexBuffer[meshlight.vertexOffset + v[1]].position;
			glm::vec3 p2 = m_CPUMeshLightVertexBuffer[meshlight.vertexOffset + v[2]].position;
			float area = 0.5 * length(cross(p1 - p0, p2 - p0));

			if (meshlight.emitMatId == -1 || !meshlight.texCoordDefined)
			{
				float emissionintensity = meshlight.emission.r + meshlight.emission.g + meshlight.emission.b;
				float power = PI * emissionintensity * area;
				CPUMeshLightIntensityBufferCompressed.push_back(power);
				for (int vertId = 0; vertId < 3; vertId++)
				{
					int oldGlobalIndex = meshlight.vertexOffset + v[vertId];
					if (indexMap.find(oldGlobalIndex) == indexMap.end())
					{
						CPUMeshLightVertexBufferCompressed.push_back(m_CPUMeshLightVertexBuffer[oldGlobalIndex]);
						indexMap[oldGlobalIndex] = uniqueIndexCount++;
					}
					CPUMeshLightIndexBufferCompressed.push_back(indexMap[oldGlobalIndex]);
				}
			}
			else
			{
				float L = retIntensBuffer[meshlight.indexOffset / 3 + triId] / 255.0;
				if (L > 0)
				{
					float dA = area / retNumberOfTexelsBuffer[meshlight.indexOffset / 3 + triId];
					float power = PI * L * dA;
					CPUMeshLightIntensityBufferCompressed.push_back(power);
					for (int vertId = 0; vertId < 3; vertId++)
					{
						int oldGlobalIndex = meshlight.vertexOffset + v[vertId];
						if (indexMap.find(oldGlobalIndex) == indexMap.end())
						{
							CPUMeshLightVertexBufferCompressed.push_back(m_CPUMeshLightVertexBuffer[oldGlobalIndex]);
							indexMap[oldGlobalIndex] = uniqueIndexCount++;
						}
						CPUMeshLightIndexBufferCompressed.push_back(indexMap[oldGlobalIndex]);
					}
				}
			}
		}
		CPUMeshLightCompressed[id].indexCount = CPUMeshLightIndexBufferCompressed.size() - CPUMeshLightCompressed[id].indexOffset;
		CPUMeshLightCompressed[id].numTriangles = CPUMeshLightCompressed[id].indexCount / 3;
	}

	int numMeshLightInstances = m_CPUMeshlightIdForInstancesBuffer.size();
	for (int instanceId = 0; instanceId < numMeshLightInstances; instanceId++)
	{
		int id = m_CPUMeshlightIdForInstancesBuffer[instanceId];
		int numTriangles = CPUMeshLightCompressed[id].numTriangles;

		for (int primId = 0; primId < numTriangles; primId++)
		{
			int firstIndex = CPUMeshLightCompressed[id].indexOffset + 3 * primId;
			m_CPUMeshLightInstancePrimitiveBuffer.push_back({ firstIndex, instanceId });
		}
	}


#ifdef LIGHT_CONE
	int numEmissiveTriangles = m_CPUMeshLightIndexBuffer.size() / 3;
	for (int primId = 0; primId < numEmissiveTriangles; primId++)
	{
		int v0 = CPUMeshLightIndexBufferCompressed[3*primId];
		int v1 = CPUMeshLightIndexBufferCompressed[3 * primId + 1];
		int v2 = CPUMeshLightIndexBufferCompressed[3 * primId + 2];
		glm::vec3 n0 = CPUMeshLightVertexBufferCompressed[v0].normal;
		glm::vec3 n1 = CPUMeshLightVertexBufferCompressed[v1].normal;
		glm::vec3 n2 = CPUMeshLightVertexBufferCompressed[v2].normal;

		glm::vec4 cone = MergeCones(MergeCones(glm::vec4(n0, 0), glm::vec4(n1, 0)), glm::vec4(n2, 0));
		m_CPUMeshLightPrecomputedBoundingCones.push_back(cone);
	}
#endif

	m_CPUMeshLightVertexBuffer = CPUMeshLightVertexBufferCompressed;
	m_CPUMeshLightIndexBuffer = CPUMeshLightIndexBufferCompressed;
	m_CPUEmissiveTriangleIntensityBuffer = CPUMeshLightIntensityBufferCompressed;
	m_CPUMeshLights = CPUMeshLightCompressed;

	m_MeshLightIndexBuffer.Create(L"MeshLightIndexBuffer", m_CPUMeshLightIndexBuffer.size(), sizeof(unsigned int), m_CPUMeshLightIndexBuffer.data());
	m_MeshLightVertexBuffer.Create(L"MeshLightVertexBuffer", m_CPUMeshLightVertexBuffer.size(), sizeof(EmissiveVertex), m_CPUMeshLightVertexBuffer.data());
#ifdef LIGHT_CONE
	m_MeshLightPrecomputedBoundingCones.Create(L"MeshLightPrecomputedBoundingCones", m_CPUMeshLightPrecomputedBoundingCones.size(), sizeof(glm::vec4), m_CPUMeshLightPrecomputedBoundingCones.data());
#endif
	m_MeshLightTriangleIntensityBuffer.Create(L"MeshLightTriangleIntensityBuffer", m_CPUEmissiveTriangleIntensityBuffer.size(), sizeof(unsigned), m_CPUEmissiveTriangleIntensityBuffer.data());
	m_MeshLightInstancePrimitiveBuffer.Create(L"MeshLightInstancePrimitiveBuffer", m_CPUMeshLightInstancePrimitiveBuffer.size(), sizeof(MeshLightInstancePrimtive), m_CPUMeshLightInstancePrimitiveBuffer.data());
	m_MeshLightTriangleNumberOfTexelsBuffer.Destroy();
}

// assuming at least 3 floats for position
void Model1::ComputeMeshBoundingBox(unsigned int meshIndex, BoundingBox &bbox) const
{
	const Mesh *mesh = m_pMesh + meshIndex;

	if (mesh->vertexCount > 0)
	{
		unsigned int vertexStride = mesh->vertexStride;

		const float *p = (float*)(m_pVertexData + mesh->vertexDataByteOffset + mesh->attrib[attrib_position].offset);
		const float *pEnd = (float*)(m_pVertexData + mesh->vertexDataByteOffset + mesh->vertexCount * mesh->vertexStride + mesh->attrib[attrib_position].offset);
		bbox.min = Scalar(FLT_MAX);
		bbox.max = Scalar(-FLT_MAX);

		while (p < pEnd)
		{
			Vector3 pos(*(p + 0), *(p + 1), *(p + 2));

			bbox.min = Min(bbox.min, pos);
			bbox.max = Max(bbox.max, pos);

			(*(uint8_t**)&p) += vertexStride;
		}
	}
	else
	{
		bbox.min = Scalar(0.0f);
		bbox.max = Scalar(0.0f);
	}
}

void Model1::ComputeGlobalBoundingBox(BoundingBox &bbox) const
{
	if (m_Header.meshCount > 0)
	{
		bbox.min = Scalar(FLT_MAX);
		bbox.max = Scalar(-FLT_MAX);
		for (unsigned int meshIndex = 0; meshIndex < m_Header.meshCount; meshIndex++)
		{
			const Mesh *mesh = m_pMesh + meshIndex;

			bbox.min = Min(bbox.min, mesh->boundingBox.min);
			bbox.max = Max(bbox.max, mesh->boundingBox.max);
		}
	}
	else
	{
		bbox.min = Scalar(0.0f);
		bbox.max = Scalar(0.0f);
	}
}

void Model1::ComputeGlobalBoundingBoxWithInstanceTransform(BoundingBox &bbox) const
{
	if (m_Header.meshCount > 0)
	{
		bbox.min = Scalar(FLT_MAX);
		bbox.max = Scalar(-FLT_MAX);

		for (unsigned int meshIndex = 0; meshIndex < m_Header.meshCount; meshIndex++)
		{
			const Mesh *mesh = m_pMesh + meshIndex;

			for (int instId = 0; instId < mesh->instanceCount; instId++)
			{
				auto& globalMatrix = m_CPUGlobalMatrices[m_CPUInstanceBuffer[mesh->instanceListOffset + instId]];

				glm::vec3 min = glm::vec3(mesh->boundingBox.min.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.min.GetZ());
				glm::vec3 max = glm::vec3(mesh->boundingBox.max.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.max.GetZ());

				glm::vec3 xa = glm::vec3(globalMatrix[0] * min.x);
				glm::vec3 xb = glm::vec3(globalMatrix[0] * max.x);
				glm::vec3 xMin = glm::min(xa, xb);
				glm::vec3 xMax = glm::max(xa, xb);

				glm::vec3 ya = glm::vec3(globalMatrix[1] * min.y);
				glm::vec3 yb = glm::vec3(globalMatrix[1] * max.y);
				glm::vec3 yMin = glm::min(ya, yb);
				glm::vec3 yMax = glm::max(ya, yb);

				glm::vec3 za = glm::vec3(globalMatrix[2] * min.z);
				glm::vec3 zb = glm::vec3(globalMatrix[2] * max.z);
				glm::vec3 zMin = glm::min(za, zb);
				glm::vec3 zMax = glm::max(za, zb);

				glm::vec3 newMin = xMin + yMin + zMin + glm::vec3(globalMatrix[3]);
				glm::vec3 newMax = xMax + yMax + zMax + glm::vec3(globalMatrix[3]);
				bbox.min = Min(bbox.min, Vector3(newMin.x, newMin.y, newMin.z));
				bbox.max = Max(bbox.max, Vector3(newMax.x, newMax.y, newMax.z));
			}
		}
	}
	else
	{
		bbox.min = Scalar(0.0f);
		bbox.max = Scalar(0.0f);
	}
}



void Model1::ComputeAllBoundingBoxes()
{
	for (unsigned int meshIndex = 0; meshIndex < m_Header.meshCount; meshIndex++)
	{
		Mesh *mesh = m_pMesh + meshIndex;
		ComputeMeshBoundingBox(meshIndex, mesh->boundingBox);
	}
	ComputeGlobalBoundingBox(m_Header.boundingBox);
}
