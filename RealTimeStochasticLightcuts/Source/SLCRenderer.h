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

#include "ViewHelper.h"
#include "ColorBuffer.h"
#include "dxgi1_3.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "VPLLightTreeBuilder.h"
#include "MeshLightTreeBuilder.h"
#include "BufferManager.h"
#include "VPLManager.h"
#include "Cube.h"
#include "Quad.h"
#include "SVGFDenoiser.h"

class SLCRenderer
{
public:

	__declspec(align(16)) struct SLCSamplingConstants
	{
		glm::vec3 viewerPos;
		float sceneRadius;
		int TLASLeafStartIndex;
		float errorLimit;
		int frameId;
		int pickType;
		int maxLightSamples;
		int VertexStride;
		int numMeshLightTriangles;
		int oneLevelTree;
		int cutSharingSize;
		int interleaveRate;
		float invNumPaths;
		int passId;
		float shadowBiasScale;
#ifdef EXPLORE_DISTANCE_TYPE
		int distanceType;
#endif
		int gUseMeshLight;
		int useApproximateCosineBound;
	};

	__declspec(align(16)) struct RayTraceReflectionConstants
	{
		Matrix4 ViewProjMatrix;
		glm::vec3 viewerPos;
		int frameId;
		float sceneRadius;
		float shadowBiasScale;
		float ZMagic;
	};


	SLCRenderer() {};
	void Initialize(Model1 * model, int numModels, int scrWidth, int scrHeight);
	void BuildMeshLightTree(GraphicsContext& context, const ViewConfig& viewConfig, int frameId, bool hasSceneChange = false, bool hasMeshLightChange = false);
	void BuildVPLLightTree(GraphicsContext& context, Vector3 lightDirection, float lightIntensity, int frameId, bool hasSceneChange = false);
	void Render(GraphicsContext& gfxContext, const ViewConfig& viewConfig, int frameId, bool hasSceneChange = false);
	void FindLightCuts(ComputeContext& cptContext, const ViewConfig& viewConfig, int frameId);

	ColorBuffer m_reflectionSamplingBuffer;
	ColorBuffer m_samplingBuffer;
	ColorBuffer m_filteredBuffer, m_filteredCombined;
	ColorBuffer m_vizBuffer;
	StructuredBuffer m_debugbuffer;

	//Controls
	static IntVar m_MaxDepth;
	static IntVar m_MaxLightSamples;
	static const char* pickTypeText[3];
	static EnumVar m_lightSamplingPickType;
	static BoolVar m_bCutSharing;
	static BoolVar m_Filtering;
	static BoolVar m_OneLevelSLC;
	static NumVar m_VisualizeSLCLevel;
	static EnumVar m_VisualizeSLCVizMode;
	static const char* vizModeText[3];
	static BoolVar m_bRayTracedReflection;
	bool m_bLastRayTracedRefleciton;

	static const char* PresetVPLEmissionOrderOfMagnitudeText[6];
	const float PresetEmissionLevels[5] = { 0.4, 1.25, 3.9, 12.3, 39.0 };
	static EnumVar m_PresetVPLOrderOfMagnitude;
	static NumVar m_VPLEmissionLevel;
	static ExpVar m_ErrorLimit;
	static BoolVar m_UseApproximateCosineBound;

	static const char* interleaveRateOptionsText[3];
	enum InterleaveRateOptions { NoInterleave = 1, Interleave2x2 = 2, Interleave4x4 = 4 };
	const int interleaveRates[3] = { NoInterleave, Interleave2x2, Interleave4x4 };
	static EnumVar m_InterleaveRate;

	static NumVar m_ShadowBiasScale;

	static BoolVar m_UseCenter;

#ifdef EXPLORE_DISTANCE_TYPE
	static const char* distanceTypeText[3];
	static EnumVar m_DistanceType; // 0 Yuksel2019, 1 distance to center, 2 min-max distance (ours)
#endif

	static IntVar m_CutSharingBlockSize;

	bool gUseMeshLight = true;

private:
	float lastVPLEmissionLevel;
	int lastPresetOrderOfMagnitude;
	int lastMaxDepth;
	bool lastIsOneLevelSLC;
	int lastCutSharingBlockSize;

	Cube m_cube;
	Quad m_quad;
	Model1* m_Model;
	RootSignature m_RootSig;
	RootSignature m_ComputeRootSig;

	VPLManager vplManager;

	MeshLightTreeBuilder mMeshLightTreeBuilder;
	VPLLightTreeBuilder mVPLLightTreeBuilder;

	// pipeline state objects
	GraphicsPSO m_ComputeGradLinearDepthPSO;
	GraphicsPSO m_VizPSO;
	ComputePSO m_LightCutFinderPSO;

	// for SLC sampling
	D3D12_CPU_DESCRIPTOR_HANDLE m_PointLightPrimitiveHandle[3]; //point light only

	D3D12_GPU_DESCRIPTOR_HANDLE m_SLCCutSrv;
	D3D12_GPU_DESCRIPTOR_HANDLE m_PrimitiveSrvs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_MeshSLCSrvs;
	D3D12_CPU_DESCRIPTOR_HANDLE m_MeshSLCDescriptorHeapHandle[5];
	D3D12_GPU_DESCRIPTOR_HANDLE m_resultUavs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_reflectionResultUavs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_GlobalMatrixSrv;

public:

	// todo: implement A-SVGF
	SVGFDenoiser svgfDenoiser;
	SVGFDenoiser svgfDenoiser2;

private:

	ColorBuffer m_DiscontinuityBuffer;

	StructuredBuffer m_LightCutBuffer;
	StructuredBuffer m_LightCutCDFBuffer;

	ByteAddressBuffer          m_slcSamplingConstantBuffer;
	ByteAddressBuffer          m_raytraceReflecitonConstantBuffer;

	void InitBuffers(int scrWidth, int scrHeight);
	void InitRootSignatures();
	void InitComputePSOs();
	void InitPSOs();
	void SVGFiltering(ComputeContext& cptContext, RootSignature& m_ComputeRootSig, const ViewConfig& viewConfig );
	void UpdatePointLightPrimitiveSrvs();
	void UpdateMeshSLCSrvs();

	void SampleSLC(GraphicsContext & context, int frameId, int passId, const ViewConfig& viewConfig);

	void SampleRayTraceReflection(GraphicsContext & context, int frameId, const ViewConfig& viewConfig);

	void VisualizeSLCNodes(GraphicsContext& gfxContext, const ViewConfig& viewConfig, StructuredBuffer& vizNodeBuffer, int showLevel, bool clearBuffer);

	void ComputeLinearDepthGradient(GraphicsContext& gfxContext, const ViewConfig& viewConfig);
	void GetSubViewportAndScissor(int i, int j, int rate, const ViewConfig& viewConfig, D3D12_VIEWPORT & viewport, D3D12_RECT & scissor);
};