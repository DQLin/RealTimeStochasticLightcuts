// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "DefaultBlockSize.hlsli"
#include "../LightTreeUtilities.hlsli"


StructuredBuffer<Node> LeafNodes : register(t0);
ByteAddressBuffer keyIndexList : register(t1);
RWStructuredBuffer<Node> Nodes : register(u0);

cbuffer CSConstants : register(b0)
{
	int numMeshLights;
	int leafOffset;
	int numTLASLeafs;
}

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x < numMeshLights)
	{
		uint2 KeyIndexPair = keyIndexList.Load2(8 * DTid.x);
		int index = KeyIndexPair.x;
		Nodes[leafOffset + DTid.x] = LeafNodes[index];
	}
	else if (DTid.x < numTLASLeafs)
	{
		Nodes[leafOffset + DTid.x].intensity = 0;
	}
}