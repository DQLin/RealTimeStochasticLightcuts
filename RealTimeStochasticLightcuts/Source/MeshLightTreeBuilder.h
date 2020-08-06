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

#pragma once
#include "TestUtils.h"
#include "LightTreeMacros.h"
#include "ModelLoader.h"
#include "CPUaabb.h"
#include "BitonicSort.h"
#include <iostream>
#include <utility>

#include "HelpUtils.h"
#ifdef CPU_BUILDER
#include "CPULightCuts.h"
#endif
class MeshLightTreeBuilder
{
public:

	MeshLightTreeBuilder() { isFirstTime = true; };

	inline int CalculateTreeLevels(int numVPLs)
	{
		return int(ceil(log2(numVPLs))) + 1;
	}

	void Init(ComputeContext& cptContext, Model1* model, int numModels = 1, bool isReinit = false, bool oneLevelTree = false);

	void Build(ComputeContext & cptContext, int frameId);

	void UpdateInstances(ComputeContext & cptContext, int frameId);

private:

	//one level
	void PopulateBLAS(ComputeContext & cptContext);

	void BuildBLAS(ComputeContext & cptContext, bool sortLights);

	void BuildTLAS(ComputeContext & cptContext, bool sortLights);

	void FillTLASLeafs(ComputeContext & cptContext);

	void SortASLeafs(ComputeContext& cptContext, int numLights, int leafStartIndex, int quantLevels,
		StructuredBuffer& leafBuffer, StructuredBuffer& nodeBuffer, int isBLAS);

public:

	int GetTLASLeafStartIndex() 
	{
#ifdef CPU_BUILDER
		if (oneLevelTree) return 2 * numTotalTriangleInstances;
		else return 2 * numMeshLightInstances;
#else
		return 1 << (numTLASLevels - 1); 
#endif
	};
	int GetNumMeshLightTriangles() { return numTotalTriangleInstances; };

	void GenerateLevelIds(const std::vector<Node>& nodes, std::vector<int>& levelIds, int curId, int offset, int leafStartIndex, int curLevel)
	{
		levelIds[offset + curId] = curLevel;
		const Node& node = nodes[offset + curId];
		int leftChild = node.ID;
		int rightChild = node.ID + 1;
		if (leftChild >= leafStartIndex || rightChild >= leafStartIndex) return;

		GenerateLevelIds(nodes, levelIds, leftChild, offset, leafStartIndex, curLevel + 1);
		GenerateLevelIds(nodes, levelIds, rightChild, offset, leafStartIndex, curLevel + 1);
	}

	void PrepareVizNodes(ComputeContext& cptContext, int numNodes, bool isBLAS, bool isTwoLevel, bool needLevelIds);

	StructuredBuffer m_TLAS;
	StructuredBuffer m_BLAS;

	StructuredBuffer m_nodeBLASInstanceId;
#ifdef CPU_BUILDER
	StructuredBuffer m_TLASNodeLevel;
	StructuredBuffer m_BLASNodeLevel;
#endif
	StructuredBuffer m_TLASViz;
	StructuredBuffer m_BLASViz;

	float globalIntensity;

	StructuredBuffer m_BLASInstanceHeaders;

	bool isFirstTime;
	StructuredBuffer m_meshLightGlobalBounds;
	bool haveUpdated[2] = { false, false };

private:

	RootSignature RootSig;

	ComputePSO m_ReorderMeshLightByKeyPSO;
	ComputePSO m_GenMeshLightMortonCodePSO;
	ComputePSO m_PopulateBLASLeafPSO;
	ComputePSO m_PopulateTLASLeafPSO;
	ComputePSO m_ExportVizNodesPSO;

	std::vector<aabb> m_BLASBounds;
#ifdef LIGHT_CONE
	std::vector<glm::vec4> m_BLASCones;
#endif
	std::vector<float> m_BLASIntensities;
	std::vector<BLASInstanceHeader> CPUBLASInstanceHeaders;

	StructuredBuffer m_TLASMeshLights;
	StructuredBuffer m_TLASMeshLightsSrc;

	ByteAddressBuffer IndexKeyList;
	ByteAddressBuffer ListCounter[2]; // for bitonic sorting

	// only for one level builder
	StructuredBuffer m_BLASLeafs;

	StructuredBuffer m_BoundMinBuffer;
	StructuredBuffer m_BoundMaxBuffer;

	Model1* m_Model;
	int numMeshLights;
	int numMeshLightInstances;
	int numTotalTriangleInstances;
	int numTLASLevels; // this is numLevels for one level tree

	bool oneLevelTree;

#ifdef CPU_BUILDER
	LightCuts cpuLightCuts;
	std::default_random_engine state;
#endif
};