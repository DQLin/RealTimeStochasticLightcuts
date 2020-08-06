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
// Author(s):   Alex Nankervis
//              James Stanard
//

#pragma once

#include "VectorMath.h"
#include "TextureManager.h"
#include "GpuBuffer.h"
#include "ColorBuffer.h"
#include "CPUModel.h"
#include "LightTreeMacros.h"

using namespace Math;

class Model1
{
public:

	glm::mat4 m_modelMatrix;

	unsigned int indexSize;

	Model1();
	~Model1();

	void Clear();

	enum
	{
		attrib_mask_0 = (1 << 0),
		attrib_mask_1 = (1 << 1),
		attrib_mask_2 = (1 << 2),
		attrib_mask_3 = (1 << 3),
		attrib_mask_4 = (1 << 4),
		attrib_mask_5 = (1 << 5),
		attrib_mask_6 = (1 << 6),
		attrib_mask_7 = (1 << 7),
		attrib_mask_8 = (1 << 8),
		attrib_mask_9 = (1 << 9),
		attrib_mask_10 = (1 << 10),
		attrib_mask_11 = (1 << 11),
		attrib_mask_12 = (1 << 12),
		attrib_mask_13 = (1 << 13),
		attrib_mask_14 = (1 << 14),
		attrib_mask_15 = (1 << 15),

		// friendly name aliases
		attrib_mask_position = attrib_mask_0,
		attrib_mask_texcoord0 = attrib_mask_1,
		attrib_mask_normal = attrib_mask_2,
		attrib_mask_tangent = attrib_mask_3,
		attrib_mask_bitangent = attrib_mask_4,
	};

	enum
	{
		attrib_0 = 0,
		attrib_1 = 1,
		attrib_2 = 2,
		attrib_3 = 3,
		attrib_4 = 4,
		attrib_5 = 5,
		attrib_6 = 6,
		attrib_7 = 7,
		attrib_8 = 8,
		attrib_9 = 9,
		attrib_10 = 10,
		attrib_11 = 11,
		attrib_12 = 12,
		attrib_13 = 13,
		attrib_14 = 14,
		attrib_15 = 15,

		// friendly name aliases
		attrib_position = attrib_0,
		attrib_texcoord0 = attrib_1,
		attrib_normal = attrib_2,
		attrib_tangent = attrib_3,
		attrib_bitangent = attrib_4,

		maxAttribs = 16
	};

	enum
	{
		attrib_format_none = 0,
		attrib_format_ubyte,
		attrib_format_byte,
		attrib_format_ushort,
		attrib_format_short,
		attrib_format_float,

		attrib_formats
	};

	struct BoundingBox
	{
		Vector3 min;
		Vector3 max;
	};

	struct Header
	{
		uint32_t meshCount;
		uint32_t materialCount;
		uint32_t vertexDataByteSize;
		uint32_t indexDataByteSize;
		uint32_t vertexDataByteSizeDepth;
		BoundingBox boundingBox;
	};
	Header m_Header;

	struct Attrib
	{
		uint16_t offset; // byte offset from the start of the vertex
		uint16_t normalized; // if true, integer formats are interpreted as [-1, 1] or [0, 1]
		uint16_t components; // 1-4
		uint16_t format;
	};
	struct Mesh
	{
		BoundingBox boundingBox;

		unsigned int materialIndex;

		unsigned int attribsEnabled;
		unsigned int attribsEnabledDepth;
		unsigned int vertexStride;
		unsigned int vertexStrideDepth;
		Attrib attrib[maxAttribs];
		Attrib attribDepth[maxAttribs];

		unsigned int vertexDataByteOffset;
		unsigned int vertexCount;
		unsigned int indexDataByteOffset;
		unsigned int indexCount;

		unsigned int vertexDataByteOffsetDepth;
		unsigned int vertexCountDepth;

		unsigned int instanceListOffset;
		unsigned int instanceCount;

		Mesh()
		{
			attrib[attrib_position].offset = 0;
			attrib[attrib_texcoord0].offset = 3 * 4;
			attrib[attrib_normal].offset = 5 * 4;
			attrib[attrib_tangent].offset = 8 * 4;
			attrib[attrib_bitangent].offset = 11 * 4;
		};

	};

	struct Mesh2
	{
		BoundingBox boundingBox;

		unsigned int materialIndex;

		unsigned int attribsEnabled;
		unsigned int attribsEnabledDepth;
		unsigned int vertexStride;
		unsigned int vertexStrideDepth;
		Attrib attrib[maxAttribs];
		Attrib attribDepth[maxAttribs];

		unsigned int vertexDataByteOffset;
		unsigned int vertexCount;
		unsigned int indexDataByteOffset;
		unsigned int indexCount;

		unsigned int vertexDataByteOffsetDepth;
		unsigned int vertexCountDepth;

		Mesh2()
		{
			attrib[attrib_position].offset = 0;
			attrib[attrib_texcoord0].offset = 3 * 4;
			attrib[attrib_normal].offset = 5 * 4;
			attrib[attrib_tangent].offset = 8 * 4;
			attrib[attrib_bitangent].offset = 11 * 4;
		};

	};
	Mesh *m_pMesh;

	struct MeshInstance
	{
		unsigned int meshID;
	};

	struct SceneNode
	{
		unsigned int parentNodeID;
		glm::mat4 modelMatrix;
	};

	glm::mat4* meshModelMatrices;
	glm::vec3* meshCentroids;

	std::vector<std::vector<int>> m_MeshGroups;
	std::vector<unsigned int> m_BLASInstanceMatrixId;

	int m_NumMeshInstancesTotal;
	int m_NumBLASInstancesTotal;

	struct Material
	{
		Vector3 diffuse;
		Vector3 specular;
		Vector3 ambient;
		Vector3 emissive;
		Vector3 transparent; // light passing through a transparent surface is multiplied by this filter color
		float opacity;
		float shininess; // specular exponent
		float specularStrength; // multiplier on top of specular color

		enum { maxTexPath = 128 };
		enum { texCount = 3 };
		char texDiffusePath[maxTexPath];
		char texSpecularPath[maxTexPath];
		char texEmissivePath[maxTexPath];
		char texNormalPath[maxTexPath];
		char texLightmapPath[maxTexPath];
		char texReflectionPath[maxTexPath];

		enum { maxMaterialName = 128 };
		char name[maxMaterialName];
	};
	Material *m_pMaterial;

	std::vector<CPUMeshLight> m_CPUMeshLights;
	std::vector<int> m_CPUMeshLightInstancesBuffer; // this just extracts corresponding instances from the mesh instance list

	std::vector<EmissiveVertex> m_CPUMeshLightVertexBuffer;
	std::vector<unsigned int> m_CPUMeshLightIndexBuffer;
#ifdef LIGHT_CONE
	std::vector<glm::vec4> m_CPUMeshLightPrecomputedBoundingCones;
#endif
	// expanded indices for all mesh light instances and contains pointer to instances
	std::vector<float> m_CPUEmissiveTriangleIntensityBuffer;
	std::vector<int> m_CPUMeshlightIdForInstancesBuffer;
	std::vector<MeshLightInstancePrimtive> m_CPUMeshLightInstancePrimitiveBuffer;

	StructuredBuffer m_MeshLightIndexBuffer;
	StructuredBuffer m_MeshLightVertexBuffer;
#ifdef LIGHT_CONE
	StructuredBuffer m_MeshLightPrecomputedBoundingCones;
#endif
	StructuredBuffer m_MeshLightTriangleIntensityBuffer;
	StructuredBuffer m_MeshLightTriangleNumberOfTexelsBuffer;
	StructuredBuffer m_MeshlightIdForInstancesBuffer;
	StructuredBuffer m_MeshLightInstancePrimitiveBuffer;

	std::vector<int> m_MatrixParentId;
	std::vector<std::shared_ptr<GeneralAnimation>> m_Animations;
	int m_ActiveAnimation = 0;

	std::vector<glm::mat4> m_CPUGlobalMatrices;
	std::vector<glm::mat4> m_CPUGlobalInvTransposeMatrices;
	std::vector<glm::mat4> m_CPULocalMatrices;
	std::vector<unsigned int> m_CPUInstanceBuffer;

	unsigned char *m_pVertexData;
	unsigned char *m_pIndexData;
	StructuredBuffer m_VertexBuffer;
	StructuredBuffer m_InstanceBuffer; // provide global matrix id
	StructuredBuffer m_GlobalMatrixBuffer;
	StructuredBuffer m_PreviousGlobalMatrixBuffer;
	StructuredBuffer m_GlobalInvTransposeMatrixBuffer;
	ByteAddressBuffer m_IndexBuffer;
	uint32_t m_VertexStride;

	// optimized for depth-only rendering
	unsigned char *m_pVertexDataDepth;
	unsigned char *m_pIndexDataDepth;
	StructuredBuffer m_VertexBufferDepth;
	ByteAddressBuffer m_IndexBufferDepth;
	uint32_t m_VertexStrideDepth;

	Vector4 m_SceneBoundingSphere;

	glm::mat4 initialCameraMatrix;
	int CameraNodeID = -1;

	struct MeshLight //GPU mesh light struct
	{
		XMFLOAT3 Emission;
		int IndexOffset;
	};

	std::vector<bool> m_pMaterialIsCutout;
	std::vector<bool> m_pMaterialIsTransparent;

	virtual bool Load(const std::vector<std::string>& filenames)
	{
		if (filenames[0].substr(filenames[0].find_last_of(".")) == ".h3d")
		{
			return LoadDemoScene(filenames[0].c_str());
		}
		else return LoadAssimpModel(filenames);
	}

	const BoundingBox& GetBoundingBox() const
	{
		return m_Header.boundingBox;
	}

	const D3D12_CPU_DESCRIPTOR_HANDLE* GetSRVs(uint32_t materialIdx) const
	{
		return m_SRVs.data() + materialIdx * 3;
	}

	int GetTextureFlags(uint32_t materialIdx) const
	{
		return m_TextureFlags[materialIdx * 3 + 2];
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetEmitSRV(uint32_t materialIdx) const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = 0;
		if (m_emissiveTexId[materialIdx] >= 0) return m_emissiveSRVs[m_emissiveTexId[materialIdx]];
		else return handle;
	}

	Texture& GetEmitResource(uint32_t materialIdx)
	{
		return m_emissiveTextureResources[m_emissiveTexId[materialIdx]];
	}

	D3D12_VIEWPORT GetEmitIntegrationViewport(uint32_t materialIdx) const
	{
		int emissiveTexId = m_emissiveTexId[materialIdx];
		D3D12_VIEWPORT vp;
		if (emissiveTexId < 0) return vp;
		vp.Width = 2 * m_emissiveTexDimensions[emissiveTexId].first;
		vp.Height = 2 * m_emissiveTexDimensions[emissiveTexId].second;

		float maxDim = std::max(vp.Width, vp.Height);
		if (maxDim < 1024)
		{
			if (vp.Width > vp.Height)
			{
				float ratio = 1024.f / vp.Width;
				vp.Width = 1024;
				vp.Height *= ratio;
				vp.Height = round(vp.Height);
			}
			else
			{
				float ratio = 1024.f / vp.Height;
				vp.Height = 1024;
				vp.Width *= ratio;
				vp.Width = round(vp.Width);
			}
		}

		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.MinDepth = 0;
		vp.MaxDepth = 1;
		return vp;
	}

	void ComputeSceneBoundingSphere()
	{
		Vector3 anchor[3] = { m_pMesh[0].boundingBox.min, 0, 0 };
		Vector3 center;
		float radius;
		float maxDist = 0;
		for (int k = 0; k < 3; k++)
		{
			for (unsigned int meshIndex = 0; meshIndex < m_Header.meshCount; meshIndex++)
			{
				const Mesh *mesh = m_pMesh + meshIndex;
				Vector3 verts[8] = {
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.max.GetZ()),
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.max.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.max.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.max.GetZ())
				};

				for (int j = 0; j < 8; j++)
				{
					float dist = Length(verts[j] - anchor[k]);
					if (dist > maxDist)
					{
						if (k < 2) {
							maxDist = dist;
							anchor[k + 1] = verts[j];
						}
						else {
							Vector3 extra = verts[j];
							center = center + 0.5*(dist - radius)*Normalize(extra - center);
							radius = 0.5*(dist + radius);
						}
					}
				}
			}
			if (k == 1)
			{
				center = (0.5f*(anchor[1] + anchor[2]));
				radius = 0.5f * Length(anchor[1] - anchor[2]);
			}
		}

		m_SceneBoundingSphere = Vector4(center, radius);
	}

	void PopulateMeshLightTriangleIntensityBuffer(GraphicsContext& gfxContext);

	std::vector<Texture> m_emissiveTextureResources;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_emissiveSRVs;
	std::vector<int> m_emissiveTexId;

protected:

	bool LoadAssimpModel(const std::vector<std::string>& filenames);
	bool LoadDemoScene(const char *filename);

	void ComputeMeshBoundingBox(unsigned int meshIndex, BoundingBox &bbox) const;
	void ComputeGlobalBoundingBox(BoundingBox &bbox) const;
	void ComputeGlobalBoundingBoxWithInstanceTransform(BoundingBox &bbox) const;
	void ComputeAllBoundingBoxes();

	void LoadAssimpTextures(CPUModel& model);
	void LoadDemoTextures();
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_SRVs;
	std::vector<int> m_TextureFlags;
	std::vector<std::pair<int, int>> m_emissiveTexDimensions;
};
