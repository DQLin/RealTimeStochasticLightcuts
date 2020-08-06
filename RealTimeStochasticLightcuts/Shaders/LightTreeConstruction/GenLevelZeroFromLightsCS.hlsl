// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "DefaultBlockSize.hlsli"
#include "../LightTreeUtilities.hlsli"
cbuffer CSConstants : register(b0)
{
	int numLevelLights;
	int numLevels;
	int numVPLs;
};
StructuredBuffer<float4> lightPositions : register(t0);
StructuredBuffer<float4> lightNormals : register(t1);
StructuredBuffer<float4> lightColors : register(t2);
ByteAddressBuffer keyIndexList : register(t3);
RWStructuredBuffer<Node> nodes : register(u0);

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int levelNodeId = DTid.x;

	if (levelNodeId < numLevelLights)
	{
		Node node;
		int nodeArr = (1 << (numLevels - 1)) + levelNodeId;

		if (levelNodeId < numVPLs)
		{
			uint2 KeyIndexPair = keyIndexList.Load2(8 * levelNodeId);
			int index = KeyIndexPair.x;
			float3 lightPos = lightPositions[index].xyz;
			float3 lightN = lightNormals[index].xyz;
			float3 lightColor = lightColors[index].xyz;
			node.ID = index;
			// For 16 bit version this doesn't matter (if we really need to construct the similar invalid bound it 
			// needs to be corner + (1+eps)dimension)
			float3 boundMin = 1e10;
			float3 boundMax = -1e10;
			node.intensity = GetColorIntensity(lightColor.xyz);

			if (node.intensity > 0) //real light
			{
				boundMin = lightPos.xyz;
			}

			node.boundMax = node.boundMin = boundMin;
#ifdef LIGHT_CONE
			if (node.intensity > 0)
			{
				node.cone.xyz = lightN.xyz;
				node.cone.w = 0;
			}
#endif
		}
		else
		{
			node.intensity = 0;
			node.boundMin = 1e10;
			node.boundMax = -1e10;
		}

		nodes[nodeArr] = node;
	}
}