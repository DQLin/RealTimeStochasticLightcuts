// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "../LightTreeUtilities.hlsli"
#include "DefaultBlockSize.hlsli"

cbuffer CSConstants : register(b0)
{
	int numMeshLights;
	int numMeshLightInstances;
};

StructuredBuffer<BLASInstanceHeader> g_BLASHeaders : register(t0);
StructuredBuffer<Node> TLASLeafSrc : register(t1);
StructuredBuffer<int> meshlightIdBufferForInstances : register(t2);
RWStructuredBuffer<Node> TLASLeafs : register(u0);
RWStructuredBuffer<float4> boundMinBuffer : register(u1);
RWStructuredBuffer<float4> boundMaxBuffer : register(u2);

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int meshInstanceId = DTid.x;

	if (meshInstanceId < numMeshLightInstances)
	{
		int meshId = meshlightIdBufferForInstances[meshInstanceId];
		Node Leaf = TLASLeafSrc[meshId];
		BLASInstanceHeader header = g_BLASHeaders[meshInstanceId];

		float3 boundMin = Leaf.boundMin;
		float3 boundMax = Leaf.boundMax;
		float3 newBoundMin = 1e10, newBoundMax = -1e10;

		[unroll]
		for (int i = 0; i < 8; i++)
		{
			float3 newpoint = mul(header.rotation,
				header.scaling * float3(i % 2 == 0 ? boundMin.x : boundMax.x,
					   (i / 2) % 2 == 0 ? boundMin.y : boundMax.y,
					   i / 4 == 0 ? boundMin.z : boundMax.z));
			newBoundMin = min(newBoundMin, newpoint);
			newBoundMax = max(newBoundMax, newpoint);
		}
		newBoundMin += header.translation;
		newBoundMax += header.translation;

		Leaf.intensity *= header.scaling;
		Leaf.boundMin = newBoundMin;
		Leaf.boundMax = newBoundMax;
		Leaf.ID = meshInstanceId;
#ifdef LIGHT_CONE
		float4 cone = Leaf.cone;
		cone = float4(mul(header.rotation, cone.xyz), cone.w);
		Leaf.cone = cone;
#endif
		TLASLeafs[meshInstanceId] = Leaf;
		boundMinBuffer[meshInstanceId] = float4(newBoundMin, 0);
		boundMaxBuffer[meshInstanceId] = float4(newBoundMax, 0);
	}
}