// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "../LightTreeUtilities.hlsli"
#include "../RandomGenerator.hlsli"

StructuredBuffer<uint> g_MeshLightIndexBuffer : register(t0);
StructuredBuffer<EmissiveVertex> g_MeshLightVertexBuffer : register(t1);
StructuredBuffer<MeshLightInstancePrimtive> g_MeshLightInstancePrimitiveBuffer : register(t2);
StructuredBuffer<float4> g_lightPositions : register(t8);
StructuredBuffer<float4> g_lightNormals : register(t9);
StructuredBuffer<float4> g_lightColors : register(t10);

StructuredBuffer<BLASInstanceHeader> g_BLASHeaders : register(t3);
StructuredBuffer<Node> TLAS : register(t4);
StructuredBuffer<Node> BLAS : register(t5);
Texture2D<float4> texPosition : register(t6);
Texture2D<float4> texNormal : register(t7);

RWStructuredBuffer<int> lightcutBuffer : register(u0);
RWStructuredBuffer<float> lightcutCDFBuffer : register(u1);

cbuffer Constants : register(b0)
{
	float3 viewerPos;
	int TLASLeafStartIndex;
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
}

cbuffer BoundConstants : register(b1)
{
	float3 corner;
	int pad0;
	float3 dimension;
	float sceneLightBoundRadius;
}

#include "BRDF.hlsli"
#include "SLCCommonFunctions.hlsli"

struct LightHeapData
{
	int    TLASNodeID; //negative indiates the BLAS pointed to by TLAS leaf
	int    BLASNodeID;
	float  error;// temporarily stores the error
};

struct OneLevelLightHeapData
{
	int NodeID;
	float error;
};

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int2 anchor = DTid.xy * CutShareGroupSize;
	if (anchor.x >= scrWidth || anchor.y >= scrHeight) return;

	RandomSequence rng;

	int dispatchWidth = (scrWidth + CutShareGroupSize - 1) / CutShareGroupSize;
	RandomSequence_Initialize(rng, dispatchWidth * DTid.y + DTid.x, frameId);
	rng.Type = 0;

	int2 realCutSharedGroupSize = int2(min(scrWidth - anchor.x, CutShareGroupSize),
		min(scrHeight - anchor.y, CutShareGroupSize));

	int offset = min(realCutSharedGroupSize.x * realCutSharedGroupSize.y - 1,
		int(realCutSharedGroupSize.x * realCutSharedGroupSize.y * RandomSequence_GenerateSample1D(rng)));
	// randomize pivot pixel position
	int offsetX = offset % realCutSharedGroupSize.x;
	int offsetY = offset / realCutSharedGroupSize.x;

	int2 samplePosition = CutShareGroupSize * DTid.xy + int2(offsetX, offsetY);
	float3 p = texPosition[samplePosition].xyz;
	float3 N = texNormal[samplePosition].xyz;
	float3 V = normalize(viewerPos - p);

	int startAddr = MAX_CUT_NODES * (DTid.y * ((scrWidth + CutShareGroupSize - 1) / CutShareGroupSize) + DTid.x);
	int numLights = 1;

	if (oneLevelTree)
	{
		OneLevelLightHeapData heap[MAX_CUT_NODES + 1];
		heap[1].NodeID = 1;
		heap[1].error = 1e27;
		int maxId = 1;
		int lightcutNodes[MAX_CUT_NODES];
		lightcutNodes[0] = 1;
		while (numLights < MaxCutNodes)
		{
			int id = maxId;
			int NodeID = heap[id].NodeID;

#ifdef CPU_BUILDER
			int pChild = BLAS[NodeID].ID;
#else
			int pChild = NodeID << 1;
#endif
			int sChild = pChild + 1;

			lightcutNodes[id - 1] = pChild;
			heap[id].NodeID = pChild;
			heap[id].error = errorFunction(-1, pChild, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
			);

			// check bogus light
			if (BLAS[sChild].intensity > 0)
			{
				numLights++;
				lightcutNodes[numLights - 1] = sChild;
				heap[numLights].NodeID = sChild;
				heap[numLights].error = errorFunction(-1, sChild, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
				);
			}

			// find maxId
			float maxError = -1e10;
			for (int i = 1; i <= numLights; i++)
			{
				if (heap[i].error > maxError)
				{
					maxError = heap[i].error;
					maxId = i;
				}
			}
			if (maxError <= 0) break;
		}

		// write lightcut nodes
		int startAddr = MAX_CUT_NODES * (DTid.y * ((scrWidth + CutShareGroupSize - 1) / CutShareGroupSize) + DTid.x);
		for (int i = 0; i < MaxCutNodes; i++)
		{
			if (i < numLights) lightcutBuffer[startAddr + i] = lightcutNodes[i];
			else lightcutBuffer[startAddr + i] = -1;
		}
	}
	else
	{
		LightHeapData heap[MAX_CUT_NODES + 1];
		heap[1].TLASNodeID = 1;
		heap[1].BLASNodeID = -1;
		heap[1].error = 1e27;
		numLights = 1;
		int maxId = 1;

		int lightcutNodes[2 * MAX_CUT_NODES];
		lightcutNodes[0] = 1;
		lightcutNodes[1] = -1;

		while (numLights < MaxCutNodes)
		{
			int id = maxId;

			int p_TLASNodeID = heap[id].TLASNodeID;
			int p_BLASNodeID = heap[id].BLASNodeID;
			int s_TLASNodeID = p_TLASNodeID;
			int s_BLASNodeID = p_BLASNodeID;

			int pChild = 0;
			int sChild = 0;

			int BLASOffset = -1;
			int BLASId = -1;

			TwoLevelTreeGetChildrenInfo(p_TLASNodeID, p_BLASNodeID, s_TLASNodeID, s_BLASNodeID,
				pChild, sChild, BLASOffset, BLASId, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex, false, -1, -1);

			lightcutNodes[2 * (id - 1)] = p_TLASNodeID;
			lightcutNodes[2 * id - 1] = p_BLASNodeID;

			heap[id].TLASNodeID = p_TLASNodeID;
			heap[id].BLASNodeID = p_BLASNodeID;

			// check bogus light
			if (sChild != -1)
			{
				numLights++;
				lightcutNodes[2 * (numLights - 1)] = s_TLASNodeID;
				lightcutNodes[2 * numLights - 1] = s_BLASNodeID;
				heap[numLights].TLASNodeID = s_TLASNodeID;
				heap[numLights].BLASNodeID = s_BLASNodeID;
			}

			if (BLASId >= 0)
			{
				float3x3 rotT = transpose(g_BLASHeaders[BLASId].rotation);
				float3 p_transformed = (1.f / g_BLASHeaders[BLASId].scaling) * mul(rotT, p - g_BLASHeaders[BLASId].translation);
				float3 N_transformed = mul(rotT, N);
				float3 V_transformed = mul(rotT, V);

				heap[id].error = errorFunction(pChild, BLASId, p_transformed, N_transformed, V_transformed, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
				);

				if (sChild != -1)
				{
					heap[numLights].error = errorFunction(sChild, BLASId, p_transformed, N_transformed, V_transformed, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
					);
				}
			}
			else
			{
				heap[id].error = errorFunction(pChild, BLASId, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
				);

				if (sChild != -1)
				{
					heap[numLights].error = errorFunction(sChild, BLASId, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
					);
				}
			}

			// find maxId
			float maxError = -1e10;
			for (int i = 1; i <= numLights; i++)
			{
				if (heap[i].error > maxError)
				{
					maxError = heap[i].error;
					maxId = i;
				}
			}

			if (maxError <= 0) break;
		}

		// write lightcut nodes
		int startAddr = 2 * MAX_CUT_NODES * (DTid.y * ((scrWidth + CutShareGroupSize - 1) / CutShareGroupSize) + DTid.x);

		for (int i = 0; i < MaxCutNodes; i++)
		{
			if (i < numLights)
			{
				lightcutBuffer[startAddr + 2 * i] = lightcutNodes[2 * i];
				lightcutBuffer[startAddr + 2 * i + 1] = lightcutNodes[2 * i + 1];
			}
			else
			{
				lightcutBuffer[startAddr + 2 * i] = -1;
			}
		}
	}
}