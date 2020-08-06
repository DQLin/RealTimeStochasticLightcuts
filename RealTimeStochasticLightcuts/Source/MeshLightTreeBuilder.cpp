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

#include "MeshLightTreeBuilder.h"

#include "GenMeshLightMortonCodeCS.h"
#include "ReorderMeshLightByKeyCS.h"
#include "PopulateBLASLeafCS.h"
#include "PopulateTLASLeafCS.h"
#include "ExportVizNodesCS.h"

#include <ppl.h>

BoolVar m_EnableNodeViz("Visualization/Enable Node Viz", false);

void MeshLightTreeBuilder::Init(ComputeContext& cptContext, Model1* model, int numModels /*= 1*/, bool isReinit /*= false*/, bool oneLevelTree /*= false*/)
{
	this->oneLevelTree = oneLevelTree;
	m_Model = model;
	if (numModels != 1) printf("Error! more than one model not supported yet!\n");

	m_meshLightGlobalBounds.Create(L"Scene Bound Buffer", 8, sizeof(float));

	std::vector<CPUMeshLight>& meshLights = model->m_CPUMeshLights;

	numTotalTriangleInstances = 0;
	int numTotalLeafs = 0; //including bogus triangles
	int numTotalBLASNodes = 0;
	int numTotalBLASInstanceNodes = 0;

	numMeshLights = meshLights.size();
	numMeshLightInstances = model->m_CPUMeshLightInstancesBuffer.size();
	CPUBLASInstanceHeaders.resize(numMeshLightInstances);

	ListCounter[0].Create(L"GPU List Counter TLAS", 1, sizeof(uint32_t));
	ListCounter[1].Create(L"GPU List Counter BLAS", 1, sizeof(uint32_t));

	for (int i = 0; i < numMeshLightInstances; i++)
	{
		auto& globalMatrix = model->m_CPUGlobalMatrices[model->m_CPUMeshLightInstancesBuffer[i]];
		glm::mat3 rotScale = glm::mat3(globalMatrix);
		CPUBLASInstanceHeaders[i].scaling = length(rotScale[0]);
		CPUBLASInstanceHeaders[i].rotation = rotScale / CPUBLASInstanceHeaders[i].scaling;
		CPUBLASInstanceHeaders[i].translation = glm::vec3(globalMatrix[3]);
	}

	if (!oneLevelTree)
	{
		std::vector<int> BLASOffsets(numMeshLights);
		std::vector<int> BLASTreeLeafs(numMeshLights);
		std::vector<int> BLASTreeLevels(numMeshLights);

		for (int meshlightId = 0; meshlightId < numMeshLights; meshlightId++)
		{
			int instanceOffset = model->m_CPUMeshLights[meshlightId].instanceOffset;

			int numTreeLevels = CalculateTreeLevels(meshLights[meshlightId].numTriangles);
#ifdef CPU_BUILDER
			int numTreeLeafs = meshLights[meshlightId].numTriangles;
#else
			int numTreeLeafs = 1 << (numTreeLevels - 1);
#endif
			BLASOffsets[meshlightId] = numTotalBLASNodes;
			BLASTreeLeafs[meshlightId] = numTreeLeafs;
			BLASTreeLevels[meshlightId] = numTreeLevels;
			for (int instId = 0; instId < model->m_CPUMeshLights[meshlightId].instanceCount; instId++)
			{
				numTotalTriangleInstances += meshLights[meshlightId].numTriangles;
				CPUBLASInstanceHeaders[instanceOffset + instId].BLASId = meshlightId;
				CPUBLASInstanceHeaders[instanceOffset + instId].numTreeLevels = numTreeLevels;
				CPUBLASInstanceHeaders[instanceOffset + instId].nodeOffset = numTotalBLASNodes;
				CPUBLASInstanceHeaders[instanceOffset + instId].numTreeLeafs = numTreeLeafs;
				CPUBLASInstanceHeaders[instanceOffset + instId].emission = meshLights[meshlightId].emission;
				CPUBLASInstanceHeaders[instanceOffset + instId].emitTexId = meshLights[meshlightId].emitMatId == -1 ? -1 : model->m_emissiveTexId[meshLights[meshlightId].emitMatId];
				numTotalBLASInstanceNodes += 2 * numTreeLeafs;
			}

			numTotalLeafs += numTreeLeafs;
			numTotalBLASNodes += 2 * numTreeLeafs;
		}

		m_BLASIntensities.resize(numMeshLights);
		m_BLASBounds.resize(numMeshLights);
#ifdef LIGHT_CONE
		m_BLASCones.resize(numMeshLights);
#endif

		IndexKeyList.Create(L"GPU Sort List", numMeshLightInstances, sizeof(uint64_t));

		std::vector<glm::uvec2> CPUNodeBLASInstanceIdBuffer(numTotalBLASInstanceNodes);
		std::vector<int> CPUNodeBLASLevelBuffer(numTotalBLASNodes, -1);

#ifdef CPU_BUILDER

		std::vector<Node> BLAS(numTotalBLASNodes);

		for (int meshId = 0; meshId < numMeshLights; meshId++)
		{
			cpuLightCuts.SetLightType(LightCuts::LightType::REAL);
			int meshIndexOffset = meshLights[meshId].indexOffset;
			int numBLASTriangles = meshLights[meshId].numTriangles;
			std::vector<glm::vec4> triangleCones(numBLASTriangles);
			std::vector<glm::vec3> triangleCentroids(numBLASTriangles);
			std::vector<CPUColor> trianglePowers(numBLASTriangles);
			std::vector<aabb> triangleBounds(numBLASTriangles);
			aabb BLASbound;

			for (int triId = 0; triId < numBLASTriangles; triId++)
			{
				int v0 = model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId];
				int v1 = model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId + 1];
				int v2 = model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId + 2];
				glm::vec3 p0 = model->m_CPUMeshLightVertexBuffer[v0].position;
				glm::vec3 p1 = model->m_CPUMeshLightVertexBuffer[v1].position;
				glm::vec3 p2 = model->m_CPUMeshLightVertexBuffer[v2].position;
				triangleCentroids[triId] = (p0 + p1 + p2) / 3;

				triangleCones[triId] = model->m_CPUMeshLightPrecomputedBoundingCones[meshIndexOffset / 3 + triId];

				trianglePowers[triId] = model->m_CPUEmissiveTriangleIntensityBuffer[meshIndexOffset / 3 + triId];

				aabb bbox;
				bbox.Union(p0);
				bbox.Union(p1);
				bbox.Union(p2);
				triangleBounds[triId] = bbox;
				BLASbound.Union(bbox);
			}

			int BLASOffset = BLASOffsets[meshId];
			int numNodes = 2 * numBLASTriangles;

			cpuLightCuts.Build(numBLASTriangles, [&](int i) {return trianglePowers[i]; },
				[&](int i) {return triangleCentroids[i]; },
#ifdef LIGHT_CONE
				[&](int i) {return triangleCones[i]; },
#else
				[&](int i) {},
#endif
				[&](int i) {return triangleBounds[i]; }, [&]() {return getUniform1D(state); });

			for (int i = 1; i < numNodes; i++)
			{
				LightCuts::Node curnode = cpuLightCuts.GetNode(i - 1);
				BLAS[BLASOffset + i].boundMin = curnode.boundBox.pos;
				BLAS[BLASOffset + i].boundMax = curnode.boundBox.end;
				BLAS[BLASOffset + i].intensity = curnode.probTree;
				BLAS[BLASOffset + i].ID = curnode.primaryChild >= numNodes ? numNodes + meshIndexOffset + 3 * (curnode.primaryChild - numNodes) : curnode.primaryChild;
#ifdef LIGHT_CONE
				BLAS[BLASOffset + i].cone = curnode.boundingCone;
#endif
			}

			m_BLASBounds[meshId] = BLASbound;
#ifdef LIGHT_CONE
			m_BLASCones[meshId] = BLAS[BLASOffset + 1].cone;
#endif
			m_BLASIntensities[meshId] = BLAS[BLASOffset + 1].intensity;

			// generate node levels by traversal
			GenerateLevelIds(BLAS, CPUNodeBLASLevelBuffer, 1, BLASOffset, numNodes, 0);
		}
#else

		std::vector<Node> BLAS(numTotalBLASNodes);
		std::vector<Node> BLASRoots(numMeshLights);

		m_BLASBounds.resize(numMeshLights);
#ifdef LIGHT_CONE
		m_BLASCones.resize(numMeshLights);
#endif

		// populate BLAS triangles
		for (int meshId = 0; meshId < numMeshLights; meshId++)
		{
			int meshIndexOffset = meshLights[meshId].indexOffset;
			float emissionintensity = meshLights[meshId].emission.r + meshLights[meshId].emission.g + meshLights[meshId].emission.b;

			int numBLASTriangles = meshLights[meshId].numTriangles;

			aabb BLASbound;

			std::vector<std::pair<int, Node>> LocalBLAS(numBLASTriangles);

			for (int triId = 0; triId < numBLASTriangles; triId++)
			{
				int v0 = model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId];
				int v1 = model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId + 1];
				int v2 = model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId + 2];
				glm::vec3 p0 = model->m_CPUMeshLightVertexBuffer[v0].position;
				glm::vec3 p1 = model->m_CPUMeshLightVertexBuffer[v1].position;
				glm::vec3 p2 = model->m_CPUMeshLightVertexBuffer[v2].position;
				float power = model->m_CPUEmissiveTriangleIntensityBuffer[meshIndexOffset / 3 + triId];
				aabb bbox;
				bbox.Union(p0);
				bbox.Union(p1);
				bbox.Union(p2);

				BLASbound.Union(bbox);

				Node node;
				node.boundMin = bbox.pos;
				node.boundMax = bbox.end;
				node.intensity = power;
				node.ID = meshIndexOffset + 3 * triId;
#ifdef LIGHT_CONE
				node.cone = model->m_CPUMeshLightPrecomputedBoundingCones[meshIndexOffset / 3 + triId];
#endif
				LocalBLAS[triId].second = node;
			}

			// generate sort keys

			const int quantLevel = 32; //must <= 1024

			for (int i = 0; i < numBLASTriangles; i++) {
				// center of bbox
				glm::vec3 normPos = (0.5f * (LocalBLAS[i].second.boundMax + LocalBLAS[i].second.boundMin) - BLASbound.pos) / BLASbound.dimension();
				unsigned quantX = BitExpansion(std::min(std::max(0u, unsigned(normPos.x * quantLevel)), (unsigned)quantLevel - 1));
				unsigned quantY = BitExpansion(std::min(std::max(0u, unsigned(normPos.y * quantLevel)), (unsigned)quantLevel - 1));
				unsigned quantZ = BitExpansion(std::min(std::max(0u, unsigned(normPos.z * quantLevel)), (unsigned)quantLevel - 1));
				unsigned mortonCode = quantX * 4 + quantY * 2 + quantZ;
				LocalBLAS[i].first = mortonCode;
			}

			// sort it
			std::sort(LocalBLAS.begin(), LocalBLAS.end(), [](const std::pair<int, Node>& lhs, const std::pair<int, Node>& rhs)->bool {
				return lhs.first < rhs.first;
				});

			int BLASOffset = BLASOffsets[meshId] + BLASTreeLeafs[meshId];

			for (int triId = 0; triId < numBLASTriangles; triId++)
			{
				BLAS[BLASOffset + triId] = LocalBLAS[triId].second;
			}

			// fill bogus lights
			for (int triId = numBLASTriangles; triId < BLASTreeLeafs[meshId]; triId++)
			{
				Node node;
				node.boundMin = glm::vec3(FLT_MAX);
				node.boundMax = glm::vec3(-FLT_MAX);
				node.intensity = 0;
				BLAS[BLASOffset + triId] = node;
			}

			m_BLASBounds[meshId] = BLASbound;
		}

		// generate BLAS
		for (int meshId = 0; meshId < numMeshLights; meshId++)
		{
			int numTreeLevels = BLASTreeLevels[meshId];
			int numTreeLeafs = BLASTreeLeafs[meshId];
			int nodeOffset = BLASOffsets[meshId];
			// build level by level
			for (int level = 1; level < numTreeLevels; level++)
			{
				int numLevelLights = numTreeLeafs >> level;
				int levelStart = numLevelLights - 1 + 1;
				int BLASOffset = nodeOffset;
				for (int levelLightId = 0; levelLightId < numLevelLights; levelLightId++)
				{
					int nodeid = levelStart + levelLightId;
					int nodeAddr = BLASOffset + nodeid;
					int firstChildId = nodeid << 1;
					int secondChildId = firstChildId + 1;
					int firstChildAddr = BLASOffset + firstChildId;
					int secondChildAddr = BLASOffset + secondChildId;
					BLAS[nodeAddr].intensity = BLAS[firstChildAddr].intensity + BLAS[secondChildAddr].intensity;
					aabb temp_parent;
					aabb temp_firstChild = aabb(BLAS[firstChildAddr].boundMin, BLAS[firstChildAddr].boundMax);
					aabb temp_secondChild = aabb(BLAS[secondChildAddr].boundMin, BLAS[secondChildAddr].boundMax);
					temp_parent.Union(temp_firstChild);
					temp_parent.Union(temp_secondChild);
					BLAS[nodeAddr].boundMin = temp_parent.pos;
					BLAS[nodeAddr].boundMax = temp_parent.end;

#ifdef LIGHT_CONE
					if (BLAS[secondChildAddr].intensity > 0) 
					{
						BLAS[nodeAddr].cone = MergeCones(BLAS[firstChildAddr].cone, BLAS[secondChildAddr].cone);
					}
					else
					{
						BLAS[nodeAddr].cone = BLAS[firstChildAddr].cone;
					}
#endif
				}
			}
#ifdef LIGHT_CONE
			m_BLASCones[meshId] = BLAS[nodeOffset + 1].cone;
#endif
			m_BLASIntensities[meshId] = BLAS[nodeOffset + 1].intensity;
			
			Node BLASRootNode;
			BLASRootNode.boundMin = BLAS[nodeOffset + 1].boundMin;
			BLASRootNode.boundMax = BLAS[nodeOffset + 1].boundMax;
#ifdef LIGHT_CONE
			BLASRootNode.cone = BLAS[nodeOffset + 1].cone;
#endif
			BLASRootNode.ID = meshId;
			BLASRootNode.intensity = BLAS[nodeOffset + 1].intensity;
			BLASRoots[meshId] = BLASRootNode;
		}

#endif

		int BLASNodeCount = 0;
		for (int instanceId = 0; instanceId < numMeshLightInstances; instanceId++)
		{
			int meshId = model->m_CPUMeshlightIdForInstancesBuffer[instanceId];

			int numBLASNodes = 2 * BLASTreeLeafs[meshId];

			for (int nodeID = 0; nodeID < numBLASNodes; nodeID++)
			{
				CPUNodeBLASInstanceIdBuffer[BLASNodeCount++] = glm::uvec2(instanceId, BLASOffsets[meshId] + nodeID);
			}
		}
		assert(BLASNodeCount == numTotalBLASInstanceNodes);

		// dummy value in case of CPU builder
		numTLASLevels = CalculateTreeLevels(numMeshLightInstances);

#ifdef CPU_BUILDER
		int numTLASNodes = 2 * numMeshLightInstances;
#else
		int numTLASNodes = 1 << numTLASLevels;
#endif

		m_TLAS.Create(L"SLC TLAS", numTLASNodes, sizeof(Node));
		m_BLAS.Create(L"SLC BLAS", numTotalBLASNodes, sizeof(Node), BLAS.data());

		m_BoundMinBuffer.Create(L"SLC Bound Min Buffer", numMeshLightInstances, sizeof(Vector4));
		m_BoundMaxBuffer.Create(L"SLC Bound Min Buffer", numMeshLightInstances, sizeof(Vector4));

		m_TLASViz.Create(L"SLC TLAS VIZ", numTLASNodes, sizeof(VizNode));
		m_BLASViz.Create(L"SLC BLAS VIZ", numTotalBLASInstanceNodes, sizeof(VizNode));

		m_nodeBLASInstanceId.Create(L"SLC BLAS Node BLAS Instance Id", numTotalBLASInstanceNodes, sizeof(glm::uvec2), CPUNodeBLASInstanceIdBuffer.data());

#ifdef CPU_BUILDER
		m_TLASNodeLevel.Create(L"SLC BLAS Node BLAS Id", numTLASNodes, sizeof(int));
		m_BLASNodeLevel.Create(L"BLAS Node Level Id", numTotalBLASNodes, sizeof(int), CPUNodeBLASLevelBuffer.data());
#endif

#ifndef CPU_BUILDER
		m_TLASMeshLights.Create(L"SLC TLAS Mesh lights", numMeshLightInstances, sizeof(Node));
		m_TLASMeshLightsSrc.Create(L"SLC TLAS Mesh lights Original", numMeshLights, sizeof(Node), BLASRoots.data());
#endif
		m_BLASInstanceHeaders.Create(L"SLC BLAS Instance Headers", numMeshLightInstances, sizeof(BLASInstanceHeader), CPUBLASInstanceHeaders.data()); // currently GPU version only uses the first two variables
		HelpUtils::InitBboxReductionBuffers(numMeshLightInstances);
	}
	else
	{
		for (int i = 0; i < numMeshLightInstances; i++)
		{
			int meshlightId = model->m_CPUMeshlightIdForInstancesBuffer[i];
			numTotalTriangleInstances += meshLights[meshlightId].numTriangles;
			CPUBLASInstanceHeaders[i].emission = meshLights[meshlightId].emission;
			CPUBLASInstanceHeaders[i].emitTexId = meshLights[meshlightId].emitMatId == -1 ? -1 : model->m_emissiveTexId[meshLights[meshlightId].emitMatId];
		}

		// dummy value in case of CPU builder
		numTLASLevels = CalculateTreeLevels(numTotalTriangleInstances);
#ifdef CPU_BUILDER
		int numTreeLeafs = numTotalTriangleInstances;

		m_BLASNodeLevel.Create(L"BLAS Node Level Id", 2 * numTreeLeafs, sizeof(int));

#else
		int numTreeLeafs = 1 << (numTLASLevels - 1);
#endif
		IndexKeyList.Create(L"GPU Sort List", numTotalTriangleInstances, sizeof(uint64_t));
		m_TLAS.Create(L"SLC dummy TLAS", 1, sizeof(Node));
		m_BLAS.Create(L"SLC BLAS (one level)", 2 * numTreeLeafs, sizeof(Node));
		m_BLASViz.Create(L"SLC BLAS VIZ (one level)", 2 * numTreeLeafs, sizeof(VizNode));
		m_nodeBLASInstanceId.Create(L"SLC dummy BLAS Node BLAS Id", 1, sizeof(glm::uvec2));
		m_BLASLeafs.Create(L"SLC BLAS Leafs (one level)", numTotalTriangleInstances, sizeof(Node));
		m_BoundMinBuffer.Create(L"SLC Bound Min Buffer", numTotalTriangleInstances, sizeof(Vector4));
		m_BoundMaxBuffer.Create(L"SLC Bound Min Buffer", numTotalTriangleInstances, sizeof(Vector4));
		m_BLASInstanceHeaders.Create(L"SLC BLAS Headers", numMeshLightInstances, sizeof(BLASInstanceHeader), CPUBLASInstanceHeaders.data());
		HelpUtils::InitBboxReductionBuffers(numTotalTriangleInstances);
	}


	if (!isReinit)
	{
		RootSig.Reset(4);
		RootSig[0].InitAsConstantBuffer(0);
		RootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 5);
		RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6);
		RootSig[3].InitAsConstantBuffer(1);
		RootSig.Finalize(L"GPU Lighting Grid");

#define CreatePSO( ObjName, ShaderByteCode ) \
				ObjName.SetRootSignature(RootSig); \
				ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
				ObjName.Finalize();

		CreatePSO(m_GenMeshLightMortonCodePSO, g_pGenMeshLightMortonCodeCS);
		CreatePSO(m_ReorderMeshLightByKeyPSO, g_pReorderMeshLightByKeyCS);
		CreatePSO(m_PopulateBLASLeafPSO, g_pPopulateBLASLeafCS);
		CreatePSO(m_PopulateTLASLeafPSO, g_pPopulateTLASLeafCS);
		CreatePSO(m_ExportVizNodesPSO, g_pExportVizNodesCS);
		HelpUtils::Init(&RootSig);
	}

	if (!oneLevelTree)
	{
		// prepare viz nodes
#ifdef CPU_BUILDER
		PrepareVizNodes(cptContext, numTotalBLASInstanceNodes, true, true, true);
#else
		PrepareVizNodes(cptContext, numTotalBLASInstanceNodes, true, true, false);
#endif
	}

	isFirstTime = false;
}

void MeshLightTreeBuilder::Build(ComputeContext & cptContext, int frameId)
{
	if (!oneLevelTree)
	{
#ifdef CPU_BUILDER
		ScopedTimer _p0(L"Build light tree (CPU)", cptContext);

		std::vector<aabb> newBLASBounds(numMeshLightInstances);
		std::vector<float> newBLASIntensities(numMeshLightInstances); //todo: enable color animation?
#ifdef LIGHT_CONE
		std::vector<glm::vec4> newBLASCones(numMeshLightInstances);
#endif

		concurrency::parallel_for(0, numMeshLightInstances, 1, [&](int meshInstanceId)
		{
			int meshId = m_Model[0].m_CPUMeshlightIdForInstancesBuffer[meshInstanceId];
			glm::mat3 rotMatrix = CPUBLASInstanceHeaders[meshInstanceId].rotation;
			glm::vec3 translation = CPUBLASInstanceHeaders[meshInstanceId].translation;
			float scaling = CPUBLASInstanceHeaders[meshInstanceId].scaling;
			aabb newBounds;
#ifdef LIGHT_CONE
			glm::vec4 newCone = glm::vec4(rotMatrix * glm::vec3(m_BLASCones[meshId].x, m_BLASCones[meshId].y, m_BLASCones[meshId].z), m_BLASCones[meshId].w);
#endif
			for (int i = 0; i < 8; i++)
			{
				glm::vec3 newpoint = rotMatrix * scaling * m_BLASBounds[meshId][i];
				newBounds.Union(newpoint);
			}
			newBLASIntensities[meshInstanceId] = scaling * m_BLASIntensities[meshId];
#ifdef LIGHT_CONE
			newBLASCones[meshInstanceId] = newCone;
#endif
			newBLASBounds[meshInstanceId].pos = newBounds.pos + translation;
			newBLASBounds[meshInstanceId].end = newBounds.end + translation;
		}
		);

		int numNodes = 2 * numMeshLightInstances;

		std::vector<int> CPUNodeTLASLevelBuffer(numNodes, -1);

		std::vector<Node> cpuNodes(numNodes);

		state.seed(frameId);
		cpuLightCuts.Build(numMeshLightInstances, [&](int i) {return CPUColor(newBLASIntensities[i],0,0); },
			[&](int i) {return newBLASBounds[i].centroid(); },
#ifdef LIGHT_CONE
			[&](int i) {return newBLASCones[i]; },
#else
			[&](int i) {},
#endif
			[&](int i) {return newBLASBounds[i]; },
			[&]() {return getUniform1D(state); });

		for (int i = 1; i < numNodes; i++)
		{
			LightCuts::Node curnode = cpuLightCuts.GetNode(i - 1);
			cpuNodes[i].boundMin = curnode.boundBox.pos;
			cpuNodes[i].boundMax = curnode.boundBox.end;
			cpuNodes[i].intensity = curnode.probTree;
			cpuNodes[i].ID = curnode.primaryChild;
#ifdef LIGHT_CONE
			cpuNodes[i].cone = curnode.boundingCone;
#endif
		}

		m_meshLightGlobalBounds.Update(4 * 7, 1, &cpuLightCuts.globalBoundDiag);
		m_TLAS.Update(0, numNodes, cpuNodes.data());

		// generate node levels by traversal
		GenerateLevelIds(cpuNodes, CPUNodeTLASLevelBuffer, 1, 0, numNodes, 0);
		m_TLASNodeLevel.Update(0, numNodes, CPUNodeTLASLevelBuffer.data());

#else

		FillTLASLeafs(cptContext);
		// find bound
		cptContext.TransitionResource(m_BoundMinBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_BoundMaxBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		HelpUtils::FindBoundingBox(cptContext, numMeshLightInstances, m_BoundMinBuffer.GetSRV(), m_BoundMaxBuffer.GetSRV(),
			m_meshLightGlobalBounds); 
		
		std::vector<float> globalbounds = TestUtils::ReadBackCPUVectorPartial<float>(cptContext, m_meshLightGlobalBounds, 0, 8);

		cptContext.TransitionResource(m_meshLightGlobalBounds, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		BuildTLAS(cptContext, true);

#endif
	}
	else
	{
#ifdef CPU_BUILDER

		ScopedTimer _p0(L"Build light tree (CPU)", cptContext);

		std::vector<glm::vec4> triangleCones(numTotalTriangleInstances);
		std::vector<glm::vec3> triangleCentroids(numTotalTriangleInstances);
		std::vector<CPUColor> trianglePowers(numTotalTriangleInstances);
		std::vector<aabb> triangleBounds(numTotalTriangleInstances);

		std::vector<CPUMeshLight>& meshLights = m_Model->m_CPUMeshLights;

		int count = 0;

		for (int meshInstId = 0; meshInstId < numMeshLightInstances; meshInstId++)
		{
			int meshId = m_Model->m_CPUMeshlightIdForInstancesBuffer[meshInstId];

			int numBLASTriangles = meshLights[meshId].numTriangles;
			int meshIndexOffset = meshLights[meshId].indexOffset;

			aabb BLASbound;

			for (int triId = 0; triId < numBLASTriangles; triId++, count++)
			{
				int v0 = m_Model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId];
				int v1 = m_Model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId + 1];
				int v2 = m_Model->m_CPUMeshLightIndexBuffer[meshIndexOffset + 3 * triId + 2];
				glm::vec3 p0 = m_Model->m_CPUMeshLightVertexBuffer[v0].position;
				glm::vec3 p1 = m_Model->m_CPUMeshLightVertexBuffer[v1].position;
				glm::vec3 p2 = m_Model->m_CPUMeshLightVertexBuffer[v2].position;

				BLASInstanceHeader& header = CPUBLASInstanceHeaders[meshInstId];
				p0 = header.rotation * header.scaling * p0 + header.translation;
				p1 = header.rotation * header.scaling * p1 + header.translation;
				p2 = header.rotation * header.scaling * p2 + header.translation;

				triangleCentroids[count] = (p0 + p1 + p2) / 3;

				glm::vec4 cone = m_Model->m_CPUMeshLightPrecomputedBoundingCones[meshIndexOffset / 3 + triId];
				triangleCones[count] = glm::vec4(header.rotation * glm::vec3(cone), cone.w);

				trianglePowers[count] = header.scaling *  m_Model->m_CPUEmissiveTriangleIntensityBuffer[meshIndexOffset / 3 + triId];

				aabb bbox;
				bbox.Union(p0);
				bbox.Union(p1);
				bbox.Union(p2);
				triangleBounds[count] = bbox;
				BLASbound.Union(bbox);
			}
		}

		int numNodes = 2 * numTotalTriangleInstances;
		std::vector<Node> cpuNodes(numNodes);
		std::vector<int> CPUNodeBLASLevelBuffer(numNodes, -1);

		state.seed(frameId);

		cpuLightCuts.SetLightType(LightCuts::LightType::REAL);
		cpuLightCuts.Build(numTotalTriangleInstances, [&](int i) {return trianglePowers[i]; },
			[&](int i) {return triangleCentroids[i]; },
#ifdef LIGHT_CONE
			[&](int i) {return triangleCones[i]; },
#else
			[&](int i) {},
#endif
			[&](int i) {return triangleBounds[i]; }, [&]() {return getUniform1D(state); });

		for (int i = 1; i < numNodes; i++)
		{
			LightCuts::Node curnode = cpuLightCuts.GetNode(i - 1);
			cpuNodes[i].boundMin = curnode.boundBox.pos;
			cpuNodes[i].boundMax = curnode.boundBox.end;
			cpuNodes[i].intensity = curnode.probTree;
			cpuNodes[i].ID = curnode.primaryChild >= numNodes ? numNodes + (curnode.primaryChild - numNodes) : curnode.primaryChild;
#ifdef LIGHT_CONE
			cpuNodes[i].cone = curnode.boundingCone;
#endif
		}
		m_meshLightGlobalBounds.Update(4 * 7, 1, &cpuLightCuts.globalBoundDiag);

		m_BLAS.Update(0, numNodes, cpuNodes.data());

		GenerateLevelIds(cpuNodes, CPUNodeBLASLevelBuffer, 1, 0, numNodes, 0);
		m_BLASNodeLevel.Update(0, numNodes, CPUNodeBLASLevelBuffer.data());
#else
		//populate BLAS
		PopulateBLAS(cptContext);

		// find bound
		Vector3 boundMin;
		Vector3 boundMax;
		cptContext.TransitionResource(m_BoundMinBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_BoundMaxBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		HelpUtils::FindBoundingBox(cptContext, numTotalTriangleInstances, m_BoundMinBuffer.GetSRV(), m_BoundMaxBuffer.GetSRV(),
			m_meshLightGlobalBounds);
		BuildBLAS(cptContext, true);
#endif

	}

	if (m_EnableNodeViz)
	{
		// prepare viz nodes
#ifdef CPU_BUILDER
		PrepareVizNodes(cptContext, oneLevelTree ? 2 * numTotalTriangleInstances : 2 * numMeshLightInstances, oneLevelTree, !oneLevelTree, true);
#else
		PrepareVizNodes(cptContext, 1 << numTLASLevels, oneLevelTree, !oneLevelTree, false);
#endif
	}
}

//todo: move to gpu
void MeshLightTreeBuilder::UpdateInstances(ComputeContext & cptContext, int frameId)
{
	for (int i = 0; i < numMeshLightInstances; i++)
	{
		int matrixId = m_Model[0].m_CPUMeshLightInstancesBuffer[i];
		auto& globalMatrix = m_Model[0].m_CPUGlobalMatrices[matrixId];
		glm::mat3 rotScale = glm::mat3(globalMatrix);
		CPUBLASInstanceHeaders[i].scaling = length(rotScale[0]);
		CPUBLASInstanceHeaders[i].rotation = rotScale / CPUBLASInstanceHeaders[i].scaling;
		CPUBLASInstanceHeaders[i].translation = glm::vec3(globalMatrix[3]);
	}
	m_BLASInstanceHeaders.Update(0, numMeshLightInstances, CPUBLASInstanceHeaders.data()); // currently GPU version only uses the first two variables
	Build(cptContext, frameId); // comment this line when testing performance of uniform random sampling (such that it does not build a light tree)! 
}

void MeshLightTreeBuilder::PopulateBLAS(ComputeContext & cptContext)
{
	ScopedTimer _p0(L"Populate BLAS", cptContext);

	__declspec(align(16)) struct {
		int numTotalTriangles;
	} CSConstants;

	CSConstants.numTotalTriangles = numTotalTriangleInstances;

	cptContext.TransitionResource(m_BLASLeafs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(m_BoundMinBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(m_BoundMaxBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(m_BLASInstanceHeaders, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(m_Model->m_MeshLightIndexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(m_Model->m_MeshLightVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(m_Model->m_MeshLightInstancePrimitiveBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(m_Model->m_MeshLightTriangleIntensityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
#ifdef LIGHT_CONE
	cptContext.TransitionResource(m_Model->m_MeshLightPrecomputedBoundingCones, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
#endif

	cptContext.SetRootSignature(RootSig);
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.SetDynamicDescriptor(1, 0, m_BLASLeafs.GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, m_BoundMinBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(1, 2, m_BoundMaxBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, m_BLASInstanceHeaders.GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 1, m_Model->m_MeshLightIndexBuffer.GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 2, m_Model->m_MeshLightVertexBuffer.GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 3, m_Model->m_MeshLightInstancePrimitiveBuffer.GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 4, m_Model->m_MeshLightTriangleIntensityBuffer.GetSRV()); //vpl positions
#ifdef LIGHT_CONE
	cptContext.SetDynamicDescriptor(2, 5, m_Model->m_MeshLightPrecomputedBoundingCones.GetSRV()); //vpl positions
#endif

	cptContext.SetPipelineState(m_PopulateBLASLeafPSO);
	cptContext.Dispatch1D(numTotalTriangleInstances, 512);
}

void MeshLightTreeBuilder::BuildBLAS(ComputeContext & cptContext, bool sortLights)
{
	ScopedTimer _p0(L"Build light tree", cptContext);

	if (sortLights) SortASLeafs(cptContext, numTotalTriangleInstances, 1 << (numTLASLevels - 1),
		1024, m_BLASLeafs, m_BLAS, 1);

	HelpUtils::GenerateInternalLevels(cptContext, 4, numTLASLevels, m_BLAS);
}

void MeshLightTreeBuilder::BuildTLAS(ComputeContext & cptContext, bool sortLights)
{
	ScopedTimer _p0(L"Build light tree", cptContext);

	if (sortLights) SortASLeafs(cptContext, numMeshLightInstances, GetTLASLeafStartIndex(), 1024,  m_TLASMeshLights, m_TLAS, 0);
	HelpUtils::GenerateInternalLevels(cptContext, 4, numTLASLevels, m_TLAS);
}

// todo make this parallel
void MeshLightTreeBuilder::FillTLASLeafs(ComputeContext & cptContext)
{
	ScopedTimer _p0(L"Fill TLAS Leafs", cptContext);

	__declspec(align(16)) struct {
		int numMeshLights;
		int numMeshLightInstances;
	} CSConstants;

	CSConstants.numMeshLights = numMeshLights;
	CSConstants.numMeshLightInstances = numMeshLightInstances;

	cptContext.TransitionResource(m_TLASMeshLights, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(m_BoundMinBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(m_BoundMaxBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(m_BLASInstanceHeaders, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(m_TLASMeshLightsSrc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(m_Model->m_MeshlightIdForInstancesBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cptContext.SetRootSignature(RootSig);
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.SetDynamicDescriptor(1, 0, m_TLASMeshLights.GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, m_BoundMinBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(1, 2, m_BoundMaxBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, m_BLASInstanceHeaders.GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 1, m_TLASMeshLightsSrc.GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 2, m_Model->m_MeshlightIdForInstancesBuffer.GetSRV()); //vpl positions

	cptContext.SetPipelineState(m_PopulateTLASLeafPSO);
	cptContext.Dispatch1D(numMeshLightInstances, 512);
}

void MeshLightTreeBuilder::SortASLeafs(ComputeContext& cptContext, int numLights, int leafStartIndex, int quantLevels, StructuredBuffer& leafBuffer, StructuredBuffer& nodeBuffer, int isBLAS)
{
	ScopedTimer _p0(L"Morton Curve Sorting", cptContext);

	//sort VPLs

	// Put the list size in GPU memory
	{
		ScopedTimer _p0(L"gen morton code", cptContext);

		if (!haveUpdated[isBLAS])
		{
			__declspec(align(16)) uint32_t ListCount[1] = { numLights };
			// Put the list size in GPU memory
			ListCounter[isBLAS].Update(0, 1, ListCount);
			haveUpdated[isBLAS] = true;
		}
		__declspec(align(16)) struct {
			int numMeshLights; int quantLevels;
		} keyIndexConstants;

		keyIndexConstants.numMeshLights = numLights;
		keyIndexConstants.quantLevels = quantLevels;

		cptContext.TransitionResource(IndexKeyList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(leafBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetRootSignature(RootSig);
		cptContext.SetDynamicConstantBufferView(0, sizeof(keyIndexConstants), &keyIndexConstants);
		cptContext.SetConstantBuffer(3, m_meshLightGlobalBounds.GetGpuVirtualAddress());

		cptContext.SetDynamicDescriptor(1, 0, IndexKeyList.GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, leafBuffer.GetSRV()); //vpl positions
		cptContext.SetPipelineState(m_GenMeshLightMortonCodePSO);
		cptContext.Dispatch1D(numLights, 512);
	}

	{
		ScopedTimer _p0(L"sorting", cptContext);

		BitonicSort::Sort(cptContext, IndexKeyList, ListCounter[isBLAS], 0, false, true);
		cptContext.SetRootSignature(RootSig);
	}

	{
		ScopedTimer _p0(L"reordering", cptContext);
		cptContext.TransitionResource(nodeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(leafBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(IndexKeyList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		cptContext.SetDynamicDescriptor(1, 0, nodeBuffer.GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, leafBuffer.GetSRV());
		cptContext.SetDynamicDescriptor(2, 1, IndexKeyList.GetSRV());

		int numTLASLeafs = 1 << (numTLASLevels - 1);
		__declspec(align(16)) struct
		{
			int numMeshLights;
			int leafOffset;
			int numTLASLeafs;
		} reorderConstants;

		reorderConstants.numMeshLights = numLights;
		reorderConstants.leafOffset = leafStartIndex;
		reorderConstants.numTLASLeafs = numTLASLeafs;
		cptContext.SetDynamicConstantBufferView(0, sizeof(reorderConstants), &reorderConstants);
		cptContext.SetPipelineState(m_ReorderMeshLightByKeyPSO);
		cptContext.Dispatch1D(numTLASLeafs, 512);
	}

}

void MeshLightTreeBuilder::PrepareVizNodes(ComputeContext& cptContext, int numNodes, bool isBLAS, bool isTwoLevel, bool needLevelIds)
{
	ScopedTimer _p0(L"Prepare Viz Nodes", cptContext);

	cptContext.SetRootSignature(RootSig);

	__declspec(align(16)) struct {
		int numNodes;
		int isBLASInTwoLevel;
		int needLevelIds;
	} constants;

	constants.numNodes = numNodes;
	constants.isBLASInTwoLevel = isBLAS && isTwoLevel;
	constants.needLevelIds = needLevelIds;

	cptContext.SetDynamicConstantBufferView(0, sizeof(constants), &constants);

	cptContext.SetDynamicDescriptor(1, 0, isBLAS ? m_BLASViz.GetUAV() : m_TLASViz.GetUAV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 0, isBLAS ? m_BLAS.GetSRV() : m_TLAS.GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 1, m_BLASInstanceHeaders.GetSRV()); //vpl positions
#ifdef CPU_BUILDER
	cptContext.SetDynamicDescriptor(2, 2, isBLAS ? m_BLASNodeLevel.GetSRV() : m_TLASNodeLevel.GetSRV()); //vpl positions
#endif
	cptContext.SetDynamicDescriptor(2, 3, m_nodeBLASInstanceId.GetSRV()); //vpl positions

	cptContext.SetPipelineState(*HelpUtils::GetExportVizNodesPSO());
	cptContext.Dispatch1D(numNodes, 512);
}
