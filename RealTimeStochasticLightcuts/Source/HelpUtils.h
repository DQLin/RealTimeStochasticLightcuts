#pragma once
#include "GpuBuffer.h"
#include "CommandContext.h"
#include "ReadbackBuffer.h"

class HelpUtils
{
public:

	static void Init(RootSignature* rootSig);

	RootSignature* RootSig;

	static void InitBboxReductionBuffers(int numPoints);

	static void FindBoundingBox(ComputeContext & cptContext, int numPoints, 
		const D3D12_CPU_DESCRIPTOR_HANDLE& minBoundBuffer, const D3D12_CPU_DESCRIPTOR_HANDLE& maxBoundBuffer,
		StructuredBuffer& SceneBoundBuffer);

	// generate several levels at same time
	static void GenerateMultipleLevels(ComputeContext & cptContext, int srcLevel, int dstLevelStart, int dstLevelEnd, 
		const D3D12_CPU_DESCRIPTOR_HANDLE& nodesUAV, int numLevels);

	static void GenerateInternalLevels(ComputeContext & cptContext, int levelGroupSize, int numLevels,
		StructuredBuffer& nodes);

	static ComputePSO* GetExportVizNodesPSO();
};
