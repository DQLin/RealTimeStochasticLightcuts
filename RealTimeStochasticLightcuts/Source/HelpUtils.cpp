#include "HelpUtils.h"
#include "FindVPLBboxMaxCS.h"
#include "FindVPLBboxMinCS.h"
#include "GenLevelFromLevelCS.h"
#include "ExportVizNodesCS.h"
namespace
{
	// now we assume only one program calls HelpUtils, as a result it uses the caller's root signature to reduce switching cost
	RootSignature* s_RootSignature;
	ComputePSO s_FindVPLBboxMinPSO;
	ComputePSO s_FindVPLBboxMaxPSO;
	ComputePSO s_GenLevelFromLevelPSO;
	ComputePSO s_ExportVizNodesPSO;
	StructuredBuffer bboxReductionBuffer[2];
}

void HelpUtils::Init(RootSignature* rootSig)
{
	s_RootSignature = rootSig;
#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(*s_RootSignature); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();

	CreatePSO(s_FindVPLBboxMinPSO, g_pFindVPLBboxMinCS);
	CreatePSO(s_FindVPLBboxMaxPSO, g_pFindVPLBboxMaxCS);
	CreatePSO(s_GenLevelFromLevelPSO, g_pGenLevelFromLevelCS);
	CreatePSO(s_ExportVizNodesPSO, g_pExportVizNodesCS);
}

void HelpUtils::InitBboxReductionBuffers(int numPoints)
{
	bboxReductionBuffer[0].Create(L"Reductionbuffer", numPoints, sizeof(Math::Vector3));
	bboxReductionBuffer[1].Create(L"Reductionbuffer", numPoints, sizeof(Math::Vector3));
}

void HelpUtils::FindBoundingBox(ComputeContext & cptContext, int numPoints, 
	const D3D12_CPU_DESCRIPTOR_HANDLE& minBoundBufferSRV, const D3D12_CPU_DESCRIPTOR_HANDLE& maxBoundBufferSRV,
	StructuredBuffer& SceneBoundBuffer)
{
	ScopedTimer _p0(L"Find BBox", cptContext);

	int numGroups = (numPoints + 2047) / 2048;
	bool largeNumVPLs = numGroups > 2048;

	cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(SceneBoundBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	__declspec(align(16)) struct {
		int n;
		int isLastPass;
	} CSConstants = { numPoints, numPoints <= 2048 };

	cptContext.SetRootSignature(*s_RootSignature);
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, SceneBoundBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, maxBoundBufferSRV);
	cptContext.SetPipelineState(s_FindVPLBboxMaxPSO);
	cptContext.Dispatch1D(numPoints, 1024);
	if (numPoints > 2048)
	{
		CSConstants.n = numGroups;
		CSConstants.isLastPass = !largeNumVPLs;
		cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
		cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[0].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[1].GetSRV());
		cptContext.Dispatch1D(numGroups, 1024);
	}

	if (largeNumVPLs) // > 4M VPLs
	{
		int numGroups_2 = (numGroups + 2047) / 2048;
		CSConstants.n = numGroups_2;
		CSConstants.isLastPass = true;
		cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
		cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[0].GetSRV());
		cptContext.Dispatch1D(numGroups_2, 1024);
	}

	//// do the same for min
	CSConstants.n = numPoints;
	CSConstants.isLastPass = numPoints <= 2048;
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(SceneBoundBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, SceneBoundBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, minBoundBufferSRV);
	cptContext.SetPipelineState(s_FindVPLBboxMinPSO);
	cptContext.Dispatch1D(numPoints, 1024);
	if (numPoints > 2048)
	{
		CSConstants.n = numGroups;
		CSConstants.isLastPass = !largeNumVPLs;
		cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
		cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[0].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[1].GetSRV());
		cptContext.Dispatch1D(numGroups, 1024);
	}
	if (largeNumVPLs) // > 4M VPLs
	{
		int numGroups_2 = (numGroups + 2047) / 2048;
		CSConstants.n = numGroups_2;
		CSConstants.isLastPass = true;
		cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
		cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[0].GetSRV());
		cptContext.Dispatch1D(numGroups_2, 1024);
	}
}

void HelpUtils::GenerateMultipleLevels(ComputeContext & cptContext, int srcLevel, int dstLevelStart, int dstLevelEnd, const D3D12_CPU_DESCRIPTOR_HANDLE& nodesUAV, int numLevels)
{
	ScopedTimer _p0(L"Generate Level " + std::to_wstring(dstLevelStart) + L"-" + std::to_wstring(dstLevelEnd-1), cptContext);

	__declspec(align(16)) struct {
		int srcLevel;
		int dstLevelStart;
		int dstLevelEnd;
		int numLevels;
		int numDstLevelsLights;
	} constants;

	constants.srcLevel = srcLevel;
	constants.dstLevelStart = dstLevelStart;
	constants.dstLevelEnd = dstLevelEnd;
	constants.numLevels = numLevels;
	constants.numDstLevelsLights = (1 << (numLevels - dstLevelStart)) - (1 << (numLevels - dstLevelEnd));

	cptContext.SetDynamicConstantBufferView(0, sizeof(constants), &constants);
	cptContext.SetDynamicDescriptor(1, 0, nodesUAV); //vpl positions
	cptContext.SetPipelineState(s_GenLevelFromLevelPSO);
	cptContext.Dispatch1D(constants.numDstLevelsLights, 512);
}

void HelpUtils::GenerateInternalLevels(ComputeContext & cptContext, int levelGroupSize, int numLevels, StructuredBuffer& nodes)
{
	int buildPasses = (numLevels - 1 + levelGroupSize - 1) / levelGroupSize;
	{
		ScopedTimer _p0(L"Gen Levels", cptContext);
		
		cptContext.TransitionResource(nodes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		const int maxWorkLoad = 2048;
		int srcLevel = 0;
		for (int dstLevelStart = 1; dstLevelStart < numLevels; )
		{
			int dstLevelEnd;
			int workLoad = 0;
			for (dstLevelEnd = dstLevelStart + 1; dstLevelEnd < numLevels; dstLevelEnd++)
			{
				workLoad += 1 << (numLevels - 1 - srcLevel);
				if (workLoad > maxWorkLoad) break;
			}

			cptContext.InsertUAVBarrier(nodes);
			GenerateMultipleLevels(cptContext, srcLevel, dstLevelStart, dstLevelEnd, nodes.GetUAV(), numLevels);

			srcLevel = dstLevelEnd - 1;
			dstLevelStart = dstLevelEnd;
		}
	}
}

ComputePSO* HelpUtils::GetExportVizNodesPSO()
{
	return &s_ExportVizNodesPSO;
}
