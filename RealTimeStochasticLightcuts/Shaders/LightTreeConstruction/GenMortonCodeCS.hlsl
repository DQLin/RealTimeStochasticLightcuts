// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "DefaultBlockSize.hlsli"

StructuredBuffer<float4> vplPositions : register(t0);
RWByteAddressBuffer keyIndexList : register(u0);

cbuffer CSConstants : register(b0)
{
	int numVpls;
	int quantLevels;
};

cbuffer BoundConstants : register(b1)
{
	float3 corner;
	int pad0;
	float3 dimension;
	int pad1;
}

inline uint3 BitExpansion(uint3 x)
{
	//https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
	x = (x | x << 16) & 0x30000ff;
	x = (x | x << 8) & 0x300f00f;
	x = (x | x << 4) & 0x30c30c3;
	x = (x | x << 2) & 0x9249249;
	return x;
}

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x < numVpls)
	{
		//normalize position to [0,1]
		float3 normPos = (vplPositions[DTid.x].xyz - corner) / dimension;
		uint3 quantPos = min(max(0, uint3(normPos * quantLevels)), quantLevels - 1);
		quantPos = BitExpansion(quantPos);
		uint mortonCode = quantPos.x * 4 + quantPos.y * 2 + quantPos.z;

		uint2 KeyIndexPair = uint2(DTid.x, mortonCode);
		keyIndexList.Store2(8 * DTid.x, KeyIndexPair);
	}
}