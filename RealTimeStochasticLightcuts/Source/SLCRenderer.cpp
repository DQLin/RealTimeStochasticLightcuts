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

#include "SLCRenderer.h"
#include "ScreenShaderVS.h"
#include "ScreenShaderPS.h"
#include "ComputeGradLinearDepthPS.h"
#include "LightCutFinderCS.h"
#include "SLCVizShaderVS.h"
#include "SLCVizShaderPS.h"

extern BoolVar m_EnableNodeViz;

IntVar SLCRenderer::m_MaxDepth("VPL/Max Ray Depth", 3, 1, 10, 1);
IntVar SLCRenderer::m_MaxLightSamples("Stochastic Lightcuts/Max Light Samples", 8, 1, 32, 1);
const char* SLCRenderer::pickTypeText[3] = { "Random", "Light Tree", "LightCut" };
EnumVar SLCRenderer::m_lightSamplingPickType("Rendering/Sampling Method", 2, 3, pickTypeText);
BoolVar SLCRenderer::m_Filtering("Filtering/Enable", true);
BoolVar SLCRenderer::m_OneLevelSLC("Stochastic Lightcuts/One Level Tree", true);
BoolVar SLCRenderer::m_bCutSharing("Stochastic Lightcuts/Cut Sharing", true);

NumVar SLCRenderer::m_VisualizeSLCLevel("Visualization/SLC Viz Level", 0, 0, 66, 1);
const char* SLCRenderer::vizModeText[3] = { "All", "TLAS", "BLAS" };
EnumVar SLCRenderer::m_VisualizeSLCVizMode("Visualization/SLC Viz Mode", 0, 3, vizModeText);

const char* SLCRenderer::PresetVPLEmissionOrderOfMagnitudeText[6] = { "Custom", "10^3", "10^4", "10^5", "10^6", "10^7" };
EnumVar SLCRenderer::m_PresetVPLOrderOfMagnitude("VPL/Preset Density Level", 3, 6, PresetVPLEmissionOrderOfMagnitudeText);

NumVar SLCRenderer::m_VPLEmissionLevel("VPL/Density", 3.9, 0.1, 40.0, 0.1);

const char* SLCRenderer::interleaveRateOptionsText[3] = { "N/A", "2x2", "4x4" };
EnumVar SLCRenderer::m_InterleaveRate("Stochastic Lightcuts/Interleave Rate", 0, 3, interleaveRateOptionsText);
ExpVar SLCRenderer::m_ErrorLimit("Stochastic Lightcuts/Min Node Error Bound", 0.001f, -32.f, 32.f, 0.2f);
BoolVar SLCRenderer::m_UseApproximateCosineBound("Stochastic Lightcuts/Use Approx Cosine Bound", true);

NumVar SLCRenderer::m_ShadowBiasScale("Rendering/Shadow Offset", 0.001, 0.0001, 0.1, 0.0001);

#ifdef EXPLORE_DISTANCE_TYPE
const char* SLCRenderer::distanceTypeText[3] = { "Yuksel19", "Center", "MinMax" };
EnumVar SLCRenderer::m_DistanceType("Stochastic Lightcuts/Distance Type", 2, 3, distanceTypeText);
#endif

IntVar SLCRenderer::m_CutSharingBlockSize("Stochastic Lightcuts/Cutsharing Blocksize", 8, 1, 64);

BoolVar SLCRenderer::m_bRayTracedReflection("Rendering/Ray Traced Reflection", true);

void SLCRenderer::InitBuffers(int scrWidth, int scrHeight)
{
#ifdef GROUND_TRUTH
	m_samplingBuffer.Create(L"SLC Sampling Buffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
	m_filteredBuffer.Create(L"SLC Filtered Buffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
	m_filteredCombined.Create(L"SLC Filtered Shadow Ratio", scrWidth, scrHeight, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
	m_reflectionSamplingBuffer.Create(L"reflection sampling Buffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
#else
	m_samplingBuffer.Create(L"SLC Sampling Buffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_filteredBuffer.Create(L"SLC Filtered Buffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_filteredCombined.Create(L"SLC Filtered Shadow Ratio", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_reflectionSamplingBuffer.Create(L"reflection sampling Buffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
#endif

	m_debugbuffer.Create(L"debug", 16 * scrHeight*scrWidth, 4);
	m_LightCutBuffer.Create(L"Light cut buffer", 2 * MAX_CUT_NODES * ((scrWidth + 7) / 8) * ((scrHeight + 7) / 8), 4);
	m_LightCutCDFBuffer.Create(L"Light cut CDF buffer", MAX_CUT_NODES * ((scrWidth + 7) / 8) * ((scrHeight + 7) / 8), 4);
	m_DiscontinuityBuffer.Create(L"DiscontinuityBuffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R8_UNORM);

	m_vizBuffer.Create(L"VizBuffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
}

void SLCRenderer::InitRootSignatures()
{
	//Initialize root signature
	m_RootSig.Reset(8, 2);
	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;
	m_RootSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(1, Graphics::SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);

	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 3, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 32, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 3, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[5].InitAsConstants(1, 2, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 4, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[7].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.Finalize(L"LGHGIRenderer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	//Initialize compute root signature
	m_ComputeRootSig.Reset(4, 2);
	m_ComputeRootSig.InitStaticSampler(0, Graphics::SamplerPointClampDesc);
	m_ComputeRootSig.InitStaticSampler(1, Graphics::SamplerLinearClampDesc);
	m_ComputeRootSig[0].InitAsConstantBuffer(0);
	m_ComputeRootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 16);
	m_ComputeRootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 16);
	m_ComputeRootSig[3].InitAsConstantBuffer(1);
	m_ComputeRootSig.Finalize(L"SLC Image Processing");
	m_bLastRayTracedRefleciton = m_bRayTracedReflection;
}

void SLCRenderer::InitComputePSOs()
{
#define CreateComputePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(m_ComputeRootSig); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();
	CreateComputePSO(m_LightCutFinderPSO, g_pLightCutFinderCS);
}

void SLCRenderer::InitPSOs()
{
	D3D12_INPUT_ELEMENT_DESC VizElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "LOWER", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "UPPER",  0, DXGI_FORMAT_R32G32B32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "LEVEL",  0, DXGI_FORMAT_R32_SINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "INDEX",  0, DXGI_FORMAT_R32_SINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
	};

	D3D12_INPUT_ELEMENT_DESC screenVertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	DXGI_FORMAT sceneColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT sceneDepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();

	//Initialize PSO
	m_ComputeGradLinearDepthPSO.SetRootSignature(m_RootSig);
	m_ComputeGradLinearDepthPSO.SetRasterizerState(Graphics::RasterizerDefault);
	m_ComputeGradLinearDepthPSO.SetBlendState(Graphics::BlendDisable);
	m_ComputeGradLinearDepthPSO.SetDepthStencilState(Graphics::DepthStateDisabled);
	m_ComputeGradLinearDepthPSO.SetInputLayout(_countof(screenVertElem), screenVertElem);
	m_ComputeGradLinearDepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_ComputeGradLinearDepthPSO.SetRenderTargetFormats(1, &Graphics::g_GradLinearDepth.GetFormat(), sceneDepthFormat);
	m_ComputeGradLinearDepthPSO.SetVertexShader(g_pScreenShaderVS, sizeof(g_pScreenShaderVS));
	m_ComputeGradLinearDepthPSO.SetPixelShader(g_pComputeGradLinearDepthPS, sizeof(g_pComputeGradLinearDepthPS));
	m_ComputeGradLinearDepthPSO.Finalize();

	m_VizPSO = m_ComputeGradLinearDepthPSO;
	m_VizPSO.SetRasterizerState(Graphics::RasterizerDefault);
	m_VizPSO.SetBlendState(Graphics::BlendDisable);
	m_VizPSO.SetDepthStencilState(Graphics::DepthStateDisabled);
	m_VizPSO.SetInputLayout(_countof(VizElem), VizElem);
	m_VizPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
	m_VizPSO.SetRenderTargetFormats(1, &m_vizBuffer.GetFormat(), sceneDepthFormat);
	m_VizPSO.SetVertexShader(g_pSLCVizShaderVS, sizeof(g_pSLCVizShaderVS));
	m_VizPSO.SetPixelShader(g_pSLCVizShaderPS, sizeof(g_pSLCVizShaderPS));
	m_VizPSO.Finalize();
}

void SLCRenderer::SVGFiltering(ComputeContext& cptContext, RootSignature& m_ComputeRootSig, const ViewConfig& viewConfig)
{
	if (m_bLastRayTracedRefleciton != m_bRayTracedReflection) svgfDenoiser.IsInitialized = false;
	if (!svgfDenoiser.IsInitialized) svgfDenoiser.Initialize(cptContext, m_ComputeRootSig, m_bRayTracedReflection ? 2 : 1);
	m_bLastRayTracedRefleciton = m_bRayTracedReflection;
	svgfDenoiser.Reproject(cptContext, viewConfig.m_Camera, m_samplingBuffer, m_reflectionSamplingBuffer, false);
	svgfDenoiser.FilterMoments(cptContext);
	svgfDenoiser.Filter(cptContext, m_filteredCombined);
}

void SLCRenderer::UpdatePointLightPrimitiveSrvs()
{
	D3D12_CPU_DESCRIPTOR_HANDLE lightPositionSRV = mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::POSITION].GetSRV();
	D3D12_CPU_DESCRIPTOR_HANDLE lightNormalSRV = mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::NORMAL].GetSRV();
	D3D12_CPU_DESCRIPTOR_HANDLE lightColorSRV = mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::COLOR].GetSRV();

	if (m_PointLightPrimitiveHandle[0].ptr)
	{
		Graphics::g_Device->CopyDescriptorsSimple(1, m_PointLightPrimitiveHandle[0], lightPositionSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_PointLightPrimitiveHandle[1], lightNormalSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_PointLightPrimitiveHandle[2], lightColorSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	else
	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, lightPositionSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_PointLightPrimitiveHandle[0] = srvHandle;
		m_PrimitiveSrvs = vplManager.m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, lightNormalSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_PointLightPrimitiveHandle[1] = srvHandle;

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, lightColorSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_PointLightPrimitiveHandle[2] = srvHandle;
	}
}

void SLCRenderer::UpdateMeshSLCSrvs()
{
	const D3D12_CPU_DESCRIPTOR_HANDLE & BLASHeadersSRV = gUseMeshLight ? mMeshLightTreeBuilder.m_BLASInstanceHeaders.GetSRV() : mVPLLightTreeBuilder.dummyBLASHeader.GetSRV();
	const D3D12_CPU_DESCRIPTOR_HANDLE & TLASSRV = gUseMeshLight ? mMeshLightTreeBuilder.m_TLAS.GetSRV() : mVPLLightTreeBuilder.dummyTLASNodes.GetSRV();
	const D3D12_CPU_DESCRIPTOR_HANDLE & BLASSRV = gUseMeshLight ? mMeshLightTreeBuilder.m_BLAS.GetSRV() : mVPLLightTreeBuilder.nodes.GetSRV();

	if (m_MeshSLCDescriptorHeapHandle[0].ptr)
	{
		Graphics::g_Device->CopyDescriptorsSimple(1, m_MeshSLCDescriptorHeapHandle[0], BLASHeadersSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_MeshSLCDescriptorHeapHandle[1], TLASSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_MeshSLCDescriptorHeapHandle[2], BLASSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	else
	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, BLASHeadersSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_MeshSLCDescriptorHeapHandle[0] = srvHandle;
		m_MeshSLCSrvs = vplManager.m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, TLASSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_MeshSLCDescriptorHeapHandle[1] = srvHandle;

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, BLASSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_MeshSLCDescriptorHeapHandle[2] = srvHandle;
	}
}

void SLCRenderer::SampleSLC(GraphicsContext & context, int frameId, int passId, const ViewConfig& viewConfig)
{
	// Prepare constants
	SLCSamplingConstants slcSamplingConstants = {};
	const Vector3& viewerPos = viewConfig.m_Camera.GetPosition();
	slcSamplingConstants.viewerPos = glm::vec3(viewerPos.GetX(), viewerPos.GetY(), viewerPos.GetZ());
	slcSamplingConstants.errorLimit = m_ErrorLimit;
	slcSamplingConstants.useApproximateCosineBound = m_UseApproximateCosineBound;
	slcSamplingConstants.frameId = frameId;
	slcSamplingConstants.sceneRadius = vplManager.sceneBoundingSphere.GetW();

	if (gUseMeshLight)
	{
		slcSamplingConstants.TLASLeafStartIndex = mMeshLightTreeBuilder.GetTLASLeafStartIndex();
		slcSamplingConstants.numMeshLightTriangles = mMeshLightTreeBuilder.GetNumMeshLightTriangles();
	}
	else
	{
		slcSamplingConstants.TLASLeafStartIndex = mVPLLightTreeBuilder.GetTLASLeafStartIndex();
		slcSamplingConstants.numMeshLightTriangles = vplManager.numVPLs;
	}

	slcSamplingConstants.pickType = m_lightSamplingPickType;
	slcSamplingConstants.maxLightSamples = m_MaxLightSamples;
	slcSamplingConstants.VertexStride = 12;
	slcSamplingConstants.passId = passId;

	slcSamplingConstants.oneLevelTree = gUseMeshLight ? m_OneLevelSLC : true;
	slcSamplingConstants.cutSharingSize = m_bCutSharing ? m_CutSharingBlockSize : -1;
	slcSamplingConstants.interleaveRate = interleaveRates[m_InterleaveRate];
	slcSamplingConstants.invNumPaths = 1.f / vplManager.numPaths;
	slcSamplingConstants.shadowBiasScale = m_ShadowBiasScale;
#ifdef EXPLORE_DISTANCE_TYPE
	slcSamplingConstants.distanceType = m_DistanceType;
#endif
	slcSamplingConstants.gUseMeshLight = gUseMeshLight;

	context.TransitionResource(m_slcSamplingConstantBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
	context.WriteBuffer(m_slcSamplingConstantBuffer, 0, &slcSamplingConstants, sizeof(slcSamplingConstants));
	context.TransitionResource(m_slcSamplingConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	ID3D12GraphicsCommandList * pCommandList = context.GetCommandList();

	vplManager.SetRayTracingDescriptorHeaps(pCommandList);

	context.TransitionResource(m_samplingBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(Graphics::g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(Graphics::g_SceneSpecularBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	vplManager.BindRootSignatureAndScene(pCommandList);
	vplManager.BindGBuffer(pCommandList);
	pCommandList->SetComputeRootConstantBufferView(1, m_slcSamplingConstantBuffer.GetGpuVirtualAddress());

	if (gUseMeshLight)
		pCommandList->SetComputeRootConstantBufferView(11, mMeshLightTreeBuilder.m_meshLightGlobalBounds.GetGpuVirtualAddress());
	else
		pCommandList->SetComputeRootConstantBufferView(11, mVPLLightTreeBuilder.m_lightGlobalBounds.GetGpuVirtualAddress());

	pCommandList->SetComputeRootDescriptorTable(5, m_resultUavs);
	pCommandList->SetComputeRootDescriptorTable(8, m_MeshSLCSrvs);
	pCommandList->SetComputeRootDescriptorTable(gUseMeshLight ? 9 : 12, m_PrimitiveSrvs);
	pCommandList->SetComputeRootDescriptorTable(4, m_SLCCutSrv);
	if (gUseMeshLight && vplManager.m_GpuSceneEmissiveTextureSrvs.ptr) pCommandList->SetComputeRootDescriptorTable(10, vplManager.m_GpuSceneEmissiveTextureSrvs);

	vplManager.DispatchRays(SLCSampling, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height);
}

void SLCRenderer::SampleRayTraceReflection(GraphicsContext & context, int frameId, const ViewConfig& viewConfig)
{
	ScopedTimer _p0(L"RT reflection", context);
	RayTraceReflectionConstants constants = {};
	constants.ViewProjMatrix = viewConfig.m_Camera.GetViewProjMatrix();
	constants.frameId = frameId;
	const Vector3& viewerPos = viewConfig.m_Camera.GetPosition();
	constants.viewerPos = glm::vec3(viewerPos.GetX(), viewerPos.GetY(), viewerPos.GetZ());
	constants.sceneRadius = vplManager.sceneBoundingSphere.GetW();
	constants.shadowBiasScale = m_ShadowBiasScale;
	constants.ZMagic = (viewConfig.m_Camera.GetFarClip() - viewConfig.m_Camera.GetNearClip()) / viewConfig.m_Camera.GetNearClip();

	context.TransitionResource(m_raytraceReflecitonConstantBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
	context.WriteBuffer(m_raytraceReflecitonConstantBuffer, 0, &constants, sizeof(constants));
	context.TransitionResource(m_raytraceReflecitonConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	ID3D12GraphicsCommandList * pCommandList = context.GetCommandList();
	vplManager.SetRayTracingDescriptorHeaps(pCommandList);

	context.TransitionResource(m_reflectionSamplingBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(Graphics::g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(Graphics::g_SceneSpecularBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	vplManager.BindRootSignatureAndScene(pCommandList);
	vplManager.BindGBuffer(pCommandList);
	pCommandList->SetComputeRootConstantBufferView(1, m_raytraceReflecitonConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(5, m_reflectionResultUavs);
	pCommandList->SetComputeRootDescriptorTable(8, m_GlobalMatrixSrv);
	if (vplManager.m_GpuSceneEmissiveTextureSrvs.ptr) pCommandList->SetComputeRootDescriptorTable(10, vplManager.m_GpuSceneEmissiveTextureSrvs);

	vplManager.DispatchRays(RayTraceReflection, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height);
}

void SLCRenderer::VisualizeSLCNodes(GraphicsContext& gfxContext, const ViewConfig& viewConfig, StructuredBuffer& vizNodeBuffer, int showLevel, bool clearBuffer)
{
	gfxContext.SetRootSignature(m_RootSig);
	gfxContext.TransitionResource(vizNodeBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(m_vizBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);

	gfxContext.SetIndexBuffer(m_cube.m_LineStripIndexBuffer.IndexBufferView());

	__declspec(align(16)) struct VSConstants
	{
		Matrix4 modelToProjection;
	} vsConstants;

	vsConstants.modelToProjection = viewConfig.m_Camera.GetViewProjMatrix();
	gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

	__declspec(align(16)) struct PSConstants
	{
		int showLevel;
	} psConstants;
	psConstants.showLevel = showLevel - 1; // show all levels
	gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

	const D3D12_VERTEX_BUFFER_VIEW VBViews[] = { m_cube.m_VertexBuffer.VertexBufferView(), vizNodeBuffer.VertexBufferView()};
	if (clearBuffer) gfxContext.ClearColor(m_vizBuffer);

	gfxContext.SetVertexBuffers(0, 2, VBViews);
	gfxContext.SetDynamicDescriptor(4, 0, gUseMeshLight ? mMeshLightTreeBuilder.m_BLASInstanceHeaders.GetSRV() : mVPLLightTreeBuilder.dummyBLASHeader.GetSRV());
	gfxContext.SetPipelineState(m_VizPSO);
	gfxContext.SetRenderTarget(m_vizBuffer.GetRTV());
	gfxContext.SetViewportAndScissor(viewConfig.m_MainViewport, viewConfig.m_MainScissor);

	gfxContext.DrawIndexedInstanced(m_cube.lineStripIndicesPerInstance, vizNodeBuffer.GetElementCount(), 0, 0, 0);
}

void SLCRenderer::ComputeLinearDepthGradient(GraphicsContext & gfxContext, const ViewConfig& viewConfig)
{
	int frameParity = TemporalEffects::GetFrameIndexMod2();
	gfxContext.SetRootSignature(m_RootSig);
	gfxContext.TransitionResource(Graphics::g_LinearDepth[frameParity],
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(Graphics::g_GradLinearDepth,
		D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	gfxContext.SetIndexBuffer(m_quad.m_IndexBuffer.IndexBufferView());
	gfxContext.SetVertexBuffer(0, m_quad.m_VertexBuffer.VertexBufferView());
	gfxContext.SetDynamicDescriptor(3, 0, Graphics::g_LinearDepth[frameParity].GetSRV());
	gfxContext.SetPipelineState(m_ComputeGradLinearDepthPSO);
	gfxContext.SetRenderTarget(Graphics::g_GradLinearDepth.GetRTV());
	gfxContext.SetViewportAndScissor(viewConfig.m_MainViewport, viewConfig.m_MainScissor);
	gfxContext.DrawIndexed(m_quad.indicesPerInstance, 0, 0);
}

void SLCRenderer::Initialize(Model1* model, int numModels, int scrWidth, int scrHeight)
{
	lastVPLEmissionLevel = m_VPLEmissionLevel;
	lastPresetOrderOfMagnitude = m_PresetVPLOrderOfMagnitude;
	lastMaxDepth = m_MaxDepth;
	lastIsOneLevelSLC = m_OneLevelSLC;
	lastCutSharingBlockSize = m_CutSharingBlockSize;
	m_Model = model;
	InitBuffers(scrWidth, scrHeight);
	InitRootSignatures();
	InitComputePSOs();
	InitPSOs();
	m_cube.Init();
	m_quad.Init();
	vplManager.Initialize(m_Model, numModels);


	m_MeshSLCDescriptorHeapHandle[0].ptr = 0;
	m_MeshSLCDescriptorHeapHandle[1].ptr = 0;
	m_MeshSLCDescriptorHeapHandle[2].ptr = 0;

	if (gUseMeshLight)
	{
		{
			D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
			UINT srvDescriptorIndex;

			vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_Model->m_MeshLightIndexBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_PrimitiveSrvs = vplManager.m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

			vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_Model->m_MeshLightVertexBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_Model->m_MeshLightInstancePrimitiveBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		{
			D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
			UINT srvDescriptorIndex;

			vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, model->m_GlobalMatrixBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_GlobalMatrixSrv = vplManager.m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);
		}
	}
	else
	{
		m_PointLightPrimitiveHandle[0].ptr = 0;
		m_PointLightPrimitiveHandle[1].ptr = 1;
		m_PointLightPrimitiveHandle[2].ptr = 2;
	}

	m_slcSamplingConstantBuffer.Create(L"SLC Sampling Constant Buffer", 1, sizeof(SLCSamplingConstants));
	m_raytraceReflecitonConstantBuffer.Create(L"RT reflection Constant Buffer", 1, sizeof(RayTraceReflectionConstants));

	{
		D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
		UINT uavDescriptorIndex;
		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, m_samplingBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_resultUavs = vplManager.m_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, m_debugbuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, m_reflectionSamplingBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_reflectionResultUavs = vplManager.m_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;
		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_LightCutBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_MeshSLCDescriptorHeapHandle[3] = srvHandle;
		m_SLCCutSrv = vplManager.m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		vplManager.m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_LightCutCDFBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_MeshSLCDescriptorHeapHandle[4] = srvHandle;
	}

}

void SLCRenderer::BuildMeshLightTree(GraphicsContext& context, const ViewConfig& viewConfig, int frameId, bool hasSceneChange, bool hasMeshLightChange)
{
	bool needReinit = mMeshLightTreeBuilder.isFirstTime;

	if (m_OneLevelSLC != lastIsOneLevelSLC)
	{
		needReinit = true;
		mMeshLightTreeBuilder.haveUpdated[0] = false;
		mMeshLightTreeBuilder.haveUpdated[1] = false;
		lastIsOneLevelSLC = m_OneLevelSLC;
	}

	if (mMeshLightTreeBuilder.isFirstTime)
	{
		hasSceneChange = false;
		hasMeshLightChange = false;
	}

	if (hasSceneChange) vplManager.UpdateAccelerationStructure();

	bool rebuildRequested = hasMeshLightChange || needReinit;

	if (needReinit) mMeshLightTreeBuilder.Init(context.GetComputeContext(), m_Model, 1, m_OneLevelSLC);

	if (rebuildRequested)
	{
		if (hasMeshLightChange) mMeshLightTreeBuilder.UpdateInstances(context.GetComputeContext(), frameId);
		else mMeshLightTreeBuilder.Build(context.GetComputeContext(), frameId);
		UpdateMeshSLCSrvs();
	}

	if (m_CutSharingBlockSize != lastCutSharingBlockSize)
	{
		lastCutSharingBlockSize = m_CutSharingBlockSize;

		m_LightCutBuffer.Create(L"Light cut buffer", 2 * MAX_CUT_NODES * ((viewConfig.m_MainViewport.Width + (m_CutSharingBlockSize-1)) / m_CutSharingBlockSize) * 
																		 ((viewConfig.m_MainViewport.Height + (m_CutSharingBlockSize - 1)) / m_CutSharingBlockSize), 4);
		m_LightCutCDFBuffer.Create(L"Light cut CDF buffer", MAX_CUT_NODES * ((viewConfig.m_MainViewport.Width + (m_CutSharingBlockSize - 1)) / m_CutSharingBlockSize) *
																			((viewConfig.m_MainViewport.Height + (m_CutSharingBlockSize - 1)) / m_CutSharingBlockSize), 4);

		Graphics::g_Device->CopyDescriptorsSimple(1, m_MeshSLCDescriptorHeapHandle[3], m_LightCutBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_MeshSLCDescriptorHeapHandle[4], m_LightCutCDFBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
}

void SLCRenderer::BuildVPLLightTree(GraphicsContext& context, Vector3 lightDirection, float lightIntensity, int frameId, bool hasSceneChange /*= false*/)
{
	bool rebuildRequested = false;
	if (hasSceneChange) vplManager.UpdateAccelerationStructure();

	bool hasRequiredVPLsChange = m_VPLEmissionLevel != lastVPLEmissionLevel || m_MaxDepth != lastMaxDepth;

	if (m_PresetVPLOrderOfMagnitude != 0 && lastPresetOrderOfMagnitude != m_PresetVPLOrderOfMagnitude)
	{
		hasRequiredVPLsChange = true;
		m_VPLEmissionLevel = PresetEmissionLevels[m_PresetVPLOrderOfMagnitude - 1];
	}
	else if (hasRequiredVPLsChange && m_MaxDepth == lastMaxDepth)
	{
		m_PresetVPLOrderOfMagnitude = 0; //set preset to custom
	}

	rebuildRequested = vplManager.GenerateVPLs(context, m_VPLEmissionLevel, lightDirection, lightIntensity, frameId, m_MaxDepth, hasSceneChange || hasRequiredVPLsChange);

	bool needReinit = mVPLLightTreeBuilder.isFirstTime;
	if (hasRequiredVPLsChange)
	{
		lastVPLEmissionLevel = m_VPLEmissionLevel;
		lastPresetOrderOfMagnitude = m_PresetVPLOrderOfMagnitude;
		lastMaxDepth = m_MaxDepth;
		int newNumLevels = mVPLLightTreeBuilder.CalculateTreeLevels(vplManager.numVPLs);

#ifdef CPU_BUILDER
		if (!mVPLLightTreeBuilder.isFirstTime && vplManager.numVPLs > 2 * mVPLLightTreeBuilder.numVPLs) // storage not enough, reinit
		{
			needReinit = true;
		}
#endif

		if (newNumLevels != mVPLLightTreeBuilder.numTreeLevels)
		{
			needReinit = true;
		}
	}

	if (needReinit) mVPLLightTreeBuilder.Init(context.GetComputeContext(), vplManager.numVPLs, vplManager.VPLBuffers, 1024);
	else mVPLLightTreeBuilder.numVPLs = vplManager.numVPLs;

	if (rebuildRequested)
	{
		mVPLLightTreeBuilder.Build(context.GetComputeContext(), true, frameId);
		UpdatePointLightPrimitiveSrvs();
		UpdateMeshSLCSrvs();
	}
}

void SLCRenderer::Render(GraphicsContext& gfxContext, const ViewConfig& viewConfig, int frameId, bool hasSceneChange)
{
	if (gUseMeshLight)
	{
		gfxContext.TransitionResource(m_Model->m_MeshLightIndexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(m_Model->m_MeshLightVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(mMeshLightTreeBuilder.m_BLASInstanceHeaders, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(mMeshLightTreeBuilder.m_BLAS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(mMeshLightTreeBuilder.m_TLAS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	else
	{
		if (hasSceneChange) vplManager.UpdateAccelerationStructure();
		gfxContext.TransitionResource(mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(mVPLLightTreeBuilder.nodes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
	}

	if (m_bCutSharing)
	{
		FindLightCuts(gfxContext.GetComputeContext(), viewConfig, frameId);
		gfxContext.TransitionResource(m_LightCutBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
	}

	int numPasses = m_bCutSharing || m_lightSamplingPickType < 2 ? ceil(float(m_MaxLightSamples) / (interleaveRates[m_InterleaveRate] * interleaveRates[m_InterleaveRate])) : 1;

	{
		ScopedTimer _prof(L"Sample SLC", gfxContext);
		for (int passId = 0; passId < numPasses; passId++)
			SampleSLC(gfxContext, frameId, passId, viewConfig);
	}

	if (gUseMeshLight && m_bRayTracedReflection) SampleRayTraceReflection(gfxContext, frameId, viewConfig);

	gfxContext.Flush(true);

	// filtering
	{
		ScopedTimer _prof(L"Filtering", gfxContext);

		ComputeContext& cptContext = gfxContext.GetComputeContext();
		cptContext.SetRootSignature(m_ComputeRootSig);

		ComputeLinearDepthGradient(gfxContext, viewConfig);
		
		SVGFiltering(cptContext, m_ComputeRootSig, viewConfig);
	}

	// generate visualization
	if (m_EnableNodeViz)
	{
		if (gUseMeshLight)
		{
			if (m_VisualizeSLCVizMode != 1) VisualizeSLCNodes(gfxContext, viewConfig, mMeshLightTreeBuilder.m_BLASViz, m_VisualizeSLCLevel, true);
			if (!m_OneLevelSLC && m_VisualizeSLCVizMode < 2) VisualizeSLCNodes(gfxContext, viewConfig, mMeshLightTreeBuilder.m_TLASViz, m_VisualizeSLCLevel, m_VisualizeSLCVizMode == 1);
		}
		else
		{
			VisualizeSLCNodes(gfxContext, viewConfig, mVPLLightTreeBuilder.m_BLASViz, m_VisualizeSLCLevel, true);
		}
	}
}

void SLCRenderer::FindLightCuts(ComputeContext& cptContext, const ViewConfig& viewConfig, int frameId)
{
	ScopedTimer _prof(L"Find Mesh Cuts", cptContext);
	cptContext.SetRootSignature(m_ComputeRootSig);

	__declspec(align(16)) struct
	{
		glm::vec3 viewerPos;
		int LeafStartIndex; //only for one level tree
		int MaxCutNodes;
		int CutShareGroupSize;
		int scrWidth;
		int scrHeight;
		int oneLevelTree;
		int pickType;
		int frameId;
		float errorLimit;
		float invNumPaths;
		int gUseMeshLight;
		int useApproximateCosineBound;
	} csConstants;
	
	Vector3 cameraPos = viewConfig.m_Camera.GetPosition();
	csConstants.viewerPos = glm::vec3(cameraPos.GetX(), cameraPos.GetY(), cameraPos.GetZ());
	csConstants.LeafStartIndex = gUseMeshLight ? mMeshLightTreeBuilder.GetTLASLeafStartIndex() : mVPLLightTreeBuilder.GetTLASLeafStartIndex();
	csConstants.MaxCutNodes = m_MaxLightSamples;

	csConstants.CutShareGroupSize = m_CutSharingBlockSize;
	csConstants.scrWidth = viewConfig.m_MainViewport.Width;
	csConstants.scrHeight = viewConfig.m_MainViewport.Height;

	csConstants.oneLevelTree = gUseMeshLight ? m_OneLevelSLC : true;
	csConstants.pickType = m_lightSamplingPickType;
	csConstants.frameId = frameId;
	csConstants.errorLimit = m_ErrorLimit;
	csConstants.useApproximateCosineBound = m_UseApproximateCosineBound;
	csConstants.invNumPaths = 1.f / vplManager.numPaths;
	csConstants.gUseMeshLight = gUseMeshLight;

	cptContext.SetConstantBuffer(3, gUseMeshLight ? mMeshLightTreeBuilder.m_meshLightGlobalBounds.GetGpuVirtualAddress() : mVPLLightTreeBuilder.m_lightGlobalBounds.GetGpuVirtualAddress());

	if (gUseMeshLight)
	{
		cptContext.TransitionResource(m_Model->m_MeshLightIndexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_Model->m_MeshLightVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_Model->m_MeshLightInstancePrimitiveBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(mMeshLightTreeBuilder.m_BLASInstanceHeaders, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(mMeshLightTreeBuilder.m_TLAS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(mMeshLightTreeBuilder.m_BLAS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	else
	{
		cptContext.TransitionResource(mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(mVPLLightTreeBuilder.nodes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	cptContext.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cptContext.TransitionResource(m_LightCutBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(m_LightCutCDFBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants), &csConstants);
	cptContext.SetDynamicDescriptor(1, 0, m_LightCutBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, m_LightCutCDFBuffer.GetUAV());

	if (gUseMeshLight)
	{
		cptContext.SetDynamicDescriptor(2, 0, m_Model->m_MeshLightIndexBuffer.GetSRV());
		cptContext.SetDynamicDescriptor(2, 1, m_Model->m_MeshLightVertexBuffer.GetSRV());
		cptContext.SetDynamicDescriptor(2, 2, m_Model->m_MeshLightInstancePrimitiveBuffer.GetSRV());
		cptContext.SetDynamicDescriptor(2, 3, mMeshLightTreeBuilder.m_BLASInstanceHeaders.GetSRV());
		cptContext.SetDynamicDescriptor(2, 4, mMeshLightTreeBuilder.m_TLAS.GetSRV());
		cptContext.SetDynamicDescriptor(2, 5, mMeshLightTreeBuilder.m_BLAS.GetSRV());
	}
	else
	{
		cptContext.SetDynamicDescriptor(2, 8, mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::POSITION].GetSRV());
		cptContext.SetDynamicDescriptor(2, 9, mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::NORMAL].GetSRV());
		cptContext.SetDynamicDescriptor(2, 10, mVPLLightTreeBuilder.VPLs[VPLLightTreeBuilder::COLOR].GetSRV());
		cptContext.SetDynamicDescriptor(2, 3, mVPLLightTreeBuilder.dummyBLASHeader.GetSRV());
		cptContext.SetDynamicDescriptor(2, 4, mVPLLightTreeBuilder.dummyTLASNodes.GetSRV());
		cptContext.SetDynamicDescriptor(2, 5, mVPLLightTreeBuilder.nodes.GetSRV());
	}

	cptContext.SetDynamicDescriptor(2, 6, Graphics::g_ScenePositionBuffer.GetSRV());
	cptContext.SetDynamicDescriptor(2, 7, Graphics::g_SceneNormalBuffer.GetSRV());

	cptContext.SetPipelineState(m_LightCutFinderPSO);
	cptContext.Dispatch2D(((int)viewConfig.m_MainViewport.Width  + m_CutSharingBlockSize - 1) / m_CutSharingBlockSize, 
				    	  ((int)viewConfig.m_MainViewport.Height + m_CutSharingBlockSize - 1) / m_CutSharingBlockSize, 16, 16);
	cptContext.Flush();
}

void SLCRenderer::GetSubViewportAndScissor(int i, int j, int rate, const ViewConfig& viewConfig, D3D12_VIEWPORT & viewport, D3D12_RECT & scissor)
{
	viewport.Width = viewConfig.m_MainViewport.Width / rate;
	viewport.Height = viewConfig.m_MainViewport.Height / rate;
	viewport.TopLeftX = 0.5 + viewport.Width * j;
	viewport.TopLeftY = 0.5 + viewport.Height * i;

	scissor = viewConfig.m_MainScissor;
}
