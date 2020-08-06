// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "../LightTreeUtilities.hlsli"
#include "DefaultBlockSize.hlsli"

cbuffer CSConstants : register(b0)
{
	int numTotalTriangleInstances;
};

StructuredBuffer<BLASInstanceHeader> g_BLASHeaders : register(t0);
StructuredBuffer<uint> g_MeshLightIndexBuffer : register(t1);
StructuredBuffer<EmissiveVertex> g_MeshLightVertexBuffer : register(t2);
StructuredBuffer<MeshLightInstancePrimtive> g_MeshLightInstancePrimitiveBuffer : register(t3);
StructuredBuffer<float> g_MeshLightTriangleIntensityBuffer : register(t4);
#ifdef LIGHT_CONE
StructuredBuffer<float4> g_MeshLightPrecomputeBoundingCones: register(t5);
#endif
RWStructuredBuffer<Node> LeafNodes : register(u0);
RWStructuredBuffer<float4> boundMinBuffer : register(u1);
RWStructuredBuffer<float4> boundMaxBuffer : register(u2);

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int triId = DTid.x;

	if (triId < numTotalTriangleInstances)
	{
		MeshLightInstancePrimtive prim = g_MeshLightInstancePrimitiveBuffer[triId];
		int instID = prim.instanceId;

		uint v0 = g_MeshLightIndexBuffer[prim.indexOffset];
		uint v1 = g_MeshLightIndexBuffer[prim.indexOffset + 1];
		uint v2 = g_MeshLightIndexBuffer[prim.indexOffset + 2];

		float3 p0 = g_MeshLightVertexBuffer[v0].position;
		float3 p1 = g_MeshLightVertexBuffer[v1].position;
		float3 p2 = g_MeshLightVertexBuffer[v2].position;

		BLASInstanceHeader header = g_BLASHeaders[instID];

		p0 = mul(header.rotation, header.scaling * p0) + header.translation;
		p1 = mul(header.rotation, header.scaling * p1) + header.translation;
		p2 = mul(header.rotation, header.scaling * p2) + header.translation;

		float power = header.scaling * g_MeshLightTriangleIntensityBuffer[prim.indexOffset/3];

		float3 boundMin = p0;
		float3 boundMax = p0;

		boundMin = min(boundMin, p1);
		boundMin = min(boundMin, p2);
		boundMax = max(boundMax, p1);
		boundMax = max(boundMax, p2);

		Node node;
		node.boundMin = boundMin;
		node.boundMax = boundMax;
		node.intensity = power;
		node.ID = triId;
#ifdef LIGHT_CONE
		float4 cone = g_MeshLightPrecomputeBoundingCones[prim.indexOffset / 3];
		node.cone = float4(mul(header.rotation, cone.xyz), cone.w);
#endif
		LeafNodes[triId] = node;
		boundMinBuffer[triId] = float4(boundMin, 0);
		boundMaxBuffer[triId] = float4(boundMax, 0);
	}
}