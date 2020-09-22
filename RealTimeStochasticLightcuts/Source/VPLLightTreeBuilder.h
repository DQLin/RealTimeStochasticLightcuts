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
#include "BitonicSort.h"
#include "VPLConstants.h"
#include <iostream>
#include "LightTreeMacros.h"
#ifdef CPU_BUILDER
#include "CPULightCuts.h"
#endif

#ifdef CUDA_SORT
#include "RadixSort/sort.h"
#endif
using namespace Math;
class VPLLightTreeBuilder
{
public:
	VPLLightTreeBuilder() {};

	inline int CalculateTreeLevels(int numVPLs)
	{
		return int(ceil(log2(numVPLs))) + 1;
	}

	void Init(ComputeContext& cptContext, int _numVPLs, std::vector<StructuredBuffer>& _VPLs, int _quantizationLevels);

	void Build(ComputeContext & cptContext, bool sortLights, int frameId);

	void FindBoundingBox(ComputeContext & cptContext);

	void Sort(ComputeContext& cptContext);

	void GenerateLevelZero(ComputeContext& cptContext);

	void GenerateLevelIds(const std::vector<Node>& nodes, std::vector<int>& levelIds, int curId, int offset, int leafStartIndex, int curLevel)
	{
		levelIds[offset + curId] = curLevel;
		const Node& node = nodes[curId];
		int leftChild = node.ID;
		if (leftChild >= leafStartIndex) return;
		int rightChild = node.ID + 1;
		GenerateLevelIds(nodes, levelIds, leftChild, offset, leafStartIndex, curLevel + 1);
		GenerateLevelIds(nodes, levelIds, rightChild, offset, leafStartIndex, curLevel + 1);
	}

	void PrepareVizNodes(ComputeContext& cptContext, int numNodes, bool needLevelIds);

	int GetTLASLeafStartIndex()
	{
#ifdef CPU_BUILDER
		return 2 * numVPLs;
#else
		return 1 << (numTreeLevels - 1);
#endif
	};

	// create lighting grid
	enum VPLAttributes
	{
		POSITION = 0,
		NORMAL,
		COLOR
	};

	RootSignature RootSig;

	ComputePSO m_GenMortonCodePSO;
	ComputePSO m_GenLevelZeroFromLightsPSO;

	float highestCellSize;
	float baseRadius;
	StructuredBuffer m_lightGlobalBounds;

#ifdef CUDA_SORT
	ByteAddressBuffer IndexKeyList[2];
#else
	ByteAddressBuffer IndexKeyList;
#endif

	ByteAddressBuffer ListCounter; // for bitonic sorting

	bool isFirstTime = true;

	int numVPLs;
	int numTreeLights;
	int numTreeLevels;
	unsigned int quantizationLevels;
	std::vector<StructuredBuffer> VPLs;

	StructuredBuffer nodes;
	StructuredBuffer dummyTLASNodes;
	StructuredBuffer dummyBLASHeader;

	StructuredBuffer m_BLASViz;
#ifdef CPU_BUILDER
	StructuredBuffer m_BLASNodeLevel;
#endif

	float globalIntensity;

#ifdef CPU_BUILDER
	LightCuts cpuLightCuts;
	std::default_random_engine state;
#endif
};