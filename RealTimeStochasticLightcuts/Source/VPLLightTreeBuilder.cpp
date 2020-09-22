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

#include "VPLLightTreeBuilder.h"
#include "GenMortonCodeCS.h"
#include "GenLevelFromLevelCS.h"
#include "GenLevelZeroFromLightsCS.h"
#include "RTXHelper.h"
#include "HelpUtils.h"
#include <aclapi.h>

extern BoolVar m_EnableNodeViz;

void VPLLightTreeBuilder::Init(ComputeContext& cptContext, int _numVPLs, std::vector<StructuredBuffer>& _VPLs, int _quantizationLevels)
{
	VPLs = _VPLs;
	numVPLs = _numVPLs;
	quantizationLevels = _quantizationLevels;

	m_lightGlobalBounds.Create(L"Scene Bound Buffer", 1, 2 * sizeof(glm::vec4));

	int numBboxGroups = (MAXIMUM_NUM_VPLS + 1023) / 1024;

	// compute nearest power of two
	numTreeLevels = CalculateTreeLevels(numVPLs);
	numTreeLights = 1 << (numTreeLevels - 1);

#ifdef CPU_BUILDER
	int numStorageNodes = 4 * numVPLs;
#else
	int numStorageNodes = 2 * numTreeLights;
	IndexKeyList.Create(L"GPU Sort List", numTreeLights, sizeof(uint64_t), nullptr, D3D12_HEAP_FLAG_SHARED);

	__declspec(align(16)) uint32_t ListCount[1] = { numVPLs };
	ListCounter.Create(L"GPU List Counter", 1, sizeof(uint32_t), ListCount);
#endif

	nodes.Create(L"tree node List", numStorageNodes, sizeof(Node));
	dummyTLASNodes.Create(L"SLC dummy TLAS", 1, sizeof(Node));
	dummyBLASHeader.Create(L"SLC BLAS Headers", 1, sizeof(BLASInstanceHeader));

	m_BLASViz.Create(L"SLC Viz Nodes", numStorageNodes, sizeof(VizNode));
#ifdef CPU_BUILDER
	m_BLASNodeLevel.Create(L"SLC CPU BUILDER Node Level", numStorageNodes, sizeof(int));
#endif
	HelpUtils::InitBboxReductionBuffers(numTreeLights);

	if (isFirstTime)
	{
		RootSig.Reset(4);
		RootSig[0].InitAsConstantBuffer(0);
		RootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 10);
		RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 7);
		RootSig[3].InitAsConstantBuffer(1);
		RootSig.Finalize(L"GPU Lighting Grid");

#define CreatePSO( ObjName, ShaderByteCode ) \
				ObjName.SetRootSignature(RootSig); \
				ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
				ObjName.Finalize();

		CreatePSO(m_GenMortonCodePSO, g_pGenMortonCodeCS);
		CreatePSO(m_GenLevelZeroFromLightsPSO, g_pGenLevelZeroFromLightsCS);
		HelpUtils::Init(&RootSig);
	}

	isFirstTime = false;
}

void VPLLightTreeBuilder::Build(ComputeContext & cptContext, bool sortLights, int frameId)
{
#ifdef CPU_BUILDER
	ScopedTimer _p0(L"Build light tree (CPU)", cptContext);
	std::vector<Vector3> lightPositions = TestUtils::ReadBackCPUVector<Vector3>(cptContext, VPLs[POSITION], numVPLs);
	std::vector<Vector3> lightColors = TestUtils::ReadBackCPUVector<Vector3>(cptContext, VPLs[COLOR], numVPLs);
#ifdef LIGHT_CONE
	std::vector<Vector3> lightNormals = TestUtils::ReadBackCPUVector<Vector3>(cptContext, VPLs[NORMAL], numVPLs);
#endif

	cpuLightCuts.SetLightType(LightCuts::LightType::POINT);

	state.seed(frameId + 2); 	// use this for sponza default

	cpuLightCuts.Build(numVPLs, [&](int i) {return CPUColor(lightColors[i].GetX(), lightColors[i].GetY(), lightColors[i].GetZ()); },
		[&](int i) {return glm::vec3(lightPositions[i].GetX(), lightPositions[i].GetY(), lightPositions[i].GetZ()); },
#ifdef LIGHT_CONE
		[&](int i) {return glm::vec4(lightNormals[i].GetX(), lightNormals[i].GetY(), lightNormals[i].GetZ(), 0); },
#else
		[&](int i) {},
#endif
		[&](int i) {return aabb(lightPositions[i]); }, [&]() {return getUniform1D(state); });

	int numNodes = 2 * numVPLs;

	std::vector<Node> cpuNodes(numNodes);
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

	nodes.Update(0, numNodes, cpuNodes.data());
	cptContext.Flush(true);

	std::vector<int> CPUNodeBLASLevelBuffer(numNodes, -1);
	GenerateLevelIds(cpuNodes, CPUNodeBLASLevelBuffer, 1, 0, numNodes, 0);
	m_BLASNodeLevel.Update(0, numNodes, CPUNodeBLASLevelBuffer.data());
#else
	ScopedTimer _p0(L"Build light tree", cptContext);

	FindBoundingBox(cptContext);
	if (sortLights) Sort(cptContext);

	cptContext.FlushResourceBarriers();

	// fill level zero
	GenerateLevelZero(cptContext);
	HelpUtils::GenerateInternalLevels(cptContext, 4, numTreeLevels, nodes);

#endif

	if (m_EnableNodeViz)
	{
#ifdef CPU_BUILDER
		PrepareVizNodes(cptContext, numNodes, true);
#else
		PrepareVizNodes(cptContext, 2 * numTreeLights, false);
#endif
	}
}

void VPLLightTreeBuilder::FindBoundingBox(ComputeContext & cptContext)
{
	cptContext.TransitionResource(VPLs[POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLs[POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	HelpUtils::FindBoundingBox(cptContext, numVPLs, VPLs[POSITION].GetSRV(), VPLs[POSITION].GetSRV(),
		m_lightGlobalBounds);
	cptContext.TransitionResource(m_lightGlobalBounds, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
}

void VPLLightTreeBuilder::Sort(ComputeContext& cptContext)
{
	ScopedTimer _p0(L"Morton Curve Sorting", cptContext);

	//sort VPLs

	{
		ScopedTimer _p0(L"gen morton code", cptContext);

		__declspec(align(16)) uint32_t ListCount[1] = { numVPLs };
		// Put the list size in GPU memory
		ListCounter.Update(0, 1, ListCount);

		__declspec(align(16)) struct {
			int numVpls; int quantLevels;
		} keyIndexConstants;

		keyIndexConstants.numVpls = numVPLs;
		keyIndexConstants.quantLevels = quantizationLevels;

		cptContext.TransitionResource(IndexKeyList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLs[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetRootSignature(RootSig);
		cptContext.SetDynamicConstantBufferView(0, sizeof(keyIndexConstants), &keyIndexConstants);
		cptContext.SetConstantBuffer(3, m_lightGlobalBounds.GetGpuVirtualAddress());
		cptContext.SetDynamicDescriptor(1, 0, IndexKeyList.GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLs[0].GetSRV()); //vpl positions
		cptContext.SetPipelineState(m_GenMortonCodePSO);
		cptContext.Dispatch1D(numVPLs, 512);
	}

	{
		ScopedTimer _p0(L"sorting", cptContext);
		BitonicSort::Sort(cptContext, IndexKeyList, ListCounter, 0, false, true);
		cptContext.SetRootSignature(RootSig);
	}
}

void VPLLightTreeBuilder::GenerateLevelZero(ComputeContext& cptContext)
{
	ScopedTimer _p0(L"Gen Level 0 ", cptContext);
	__declspec(align(16)) struct {
		int numLevelLights;
		int numLevels;
		int numVPLs;
	} constants;

	constants.numLevelLights = numTreeLights;
	constants.numLevels = numTreeLevels;
	constants.numVPLs = numVPLs;

	cptContext.TransitionResource(VPLs[POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLs[NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLs[COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(IndexKeyList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cptContext.SetDynamicConstantBufferView(0, sizeof(constants), &constants);
	cptContext.SetDynamicDescriptor(2, 0, VPLs[POSITION].GetSRV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 1, VPLs[NORMAL].GetSRV()); //vpl normals
	cptContext.SetDynamicDescriptor(2, 2, VPLs[COLOR].GetSRV()); //vpl colors
	cptContext.SetDynamicDescriptor(2, 3, IndexKeyList.GetSRV()); //sort keys
	cptContext.SetDynamicDescriptor(1, 0, nodes.GetUAV()); //vpl positions

	cptContext.SetPipelineState(m_GenLevelZeroFromLightsPSO);

	cptContext.Dispatch1D(constants.numLevelLights, 512);
}

void VPLLightTreeBuilder::PrepareVizNodes(ComputeContext& cptContext, int numNodes, bool needLevelIds)
{
	ScopedTimer _p0(L"Prepare Viz Nodes", cptContext);

	cptContext.SetRootSignature(RootSig);

	__declspec(align(16)) struct {
		int numNodes;
		int isBLASInTwoLevel;
		int needLevelIds;
	} constants;

	constants.numNodes = numNodes;
	constants.isBLASInTwoLevel = false;
	constants.needLevelIds = needLevelIds;

	cptContext.SetDynamicConstantBufferView(0, sizeof(constants), &constants);

	cptContext.SetDynamicDescriptor(1, 0, m_BLASViz.GetUAV()); //vpl positions
	cptContext.SetDynamicDescriptor(2, 0, nodes.GetSRV()); //vpl positions
#ifdef CPU_BUILDER
	cptContext.SetDynamicDescriptor(2, 2, m_BLASNodeLevel.GetSRV()); //vpl positions
#endif

	cptContext.SetPipelineState(*HelpUtils::GetExportVizNodesPSO());
	cptContext.Dispatch1D(numNodes, 512);
}
