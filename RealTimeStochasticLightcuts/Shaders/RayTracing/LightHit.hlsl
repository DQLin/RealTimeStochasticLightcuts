//NOTE: Some part of this file is modified from DiffuseHitShaderLib.hlsl in the MiniEngine RayTracing demo

// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):    James Stanard, Christopher Wallis
//

#include "LightRayLib.hlsli"
void TraceNextRay(float3 org, float3 dir, inout RayPayload rayPayload)
{
	RayDesc rayDesc;
	rayDesc.Origin = org;
	rayDesc.Direction = dir;
	rayDesc.TMin = 0;
	rayDesc.TMax = 10000;
	rayPayload.recursionDepth += 1;

	TraceRay(g_accel,
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
		~0, 0, 1, 0,
		rayDesc, rayPayload);
}

#ifdef UE4_RANDOM
inline float3 GetCosineWeightedHemisphereSample(inout uint seed, float3 N)
#else
inline float3 GetCosineWeightedHemisphereSample(uint2 seed, float3 N)
#endif
{
	float3 u = normalize(cross(abs(N.x) > 0.1 ? float3(0, 1, 0) : float3(1, 0, 0), N));
	float3 v = cross(N, u);
#ifdef UE4_RANDOM
	float2 rnd = float2(Rand(seed), Rand(seed));
#else
	float2 rnd = float2(hash(seed), hash(seed.yx));
#endif
	float phi = 2 * PI * rnd.x;
	float xi = rnd.y;
	float z = sqrt(1 - xi);
	float r = sqrt(xi);

	float3 d = u * cos(phi)*r + v * sin(phi)*r + N * z;
	return normalize(d);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	uint instanceId = 0;
	RayTraceMeshInfo info = g_meshInfo[MeshInfoID];
	const uint3 ii = LoadIndices(instanceId, info.m_indexOffsetBytes, PrimitiveIndex());
	const float2 uv0 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes);
	const float2 uv1 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes);
	const float2 uv2 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes);
	float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	float2 uv = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;
	float alpha = g_localTexture.SampleLevel(g_s0, uv, 0).a;
	if (alpha < 0.5) IgnoreHit();
}

[shader("closesthit")]
void Hit(inout RayPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
	uint instanceId = 0;
	RayTraceMeshInfo info = g_meshInfo[MeshInfoID];
	const uint3 ii = LoadIndices(instanceId, info.m_indexOffsetBytes, PrimitiveIndex());
	const float2 uv0 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes);
	const float2 uv1 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes);
	const float2 uv2 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes);

	float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	float2 uv = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;

	const float3 normal0 = asfloat(g_attributes[instanceId].Load3(info.m_normalAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
	const float3 normal1 = asfloat(g_attributes[instanceId].Load3(info.m_normalAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
	const float3 normal2 = asfloat(g_attributes[instanceId].Load3(info.m_normalAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
	float3 vsNormal = normalize(normal0 * bary.x + normal1 * bary.y + normal2 * bary.z);

	const float3 tangent0 = asfloat(g_attributes[instanceId].Load3(info.m_tangentAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
	const float3 tangent1 = asfloat(g_attributes[instanceId].Load3(info.m_tangentAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
	const float3 tangent2 = asfloat(g_attributes[instanceId].Load3(info.m_tangentAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
	float3 vsTangent = normalize(tangent0 * bary.x + tangent1 * bary.y + tangent2 * bary.z);

	float3 normal;

	if (all(vsTangent) == 0) //tangent space not defined
	{
		normal = vsNormal;
	}
	else
	{
		const float3 bitangent0 = asfloat(g_attributes[instanceId].Load3(info.m_bitangentAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
		const float3 bitangent1 = asfloat(g_attributes[instanceId].Load3(info.m_bitangentAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
		const float3 bitangent2 = asfloat(g_attributes[instanceId].Load3(info.m_bitangentAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
		float3 vsBitangent = normalize(bitangent0 * bary.x + bitangent1 * bary.y + bitangent2 * bary.z);
		normal = g_localNormal.SampleLevel(g_s0, uv, 0).rgb * 2.0 - 1.0;
		float3x3 tbn = float3x3(vsTangent, vsBitangent, vsNormal);
		normal = normalize(mul(normal, tbn));
	}

	float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
	const float3 diffuseColor = info.m_materialAlbedo * g_localTexture.SampleLevel(g_s0, uv, 0).rgb;

#ifdef UE4_RANDOM
	float3 R = GetCosineWeightedHemisphereSample(rayPayload.seed, normal);
#else
	uint2 threadID = DispatchRaysIndex().xy + DispatchOffset;
	float3 R = GetCosineWeightedHemisphereSample(threadID * (rayPayload.recursionDepth + 1), normal);
#endif

	rayPayload.color *= diffuseColor;
	if (!isnan(normal.x) && !isnan(R.x) && any(rayPayload.color))
	{
		// count number of paths
		if (rayPayload.recursionDepth == 0) g_vplNormals.IncrementCounter();
		uint VPLid = g_vplPositions.IncrementCounter();
		g_vplPositions[VPLid] = float4(worldPosition, 0.0);
		g_vplNormals[VPLid] = float4(normal, 0.0);
		g_vplColors[VPLid] = float4(rayPayload.color, 0.0);
		//push to vpl storage buffer
		if (rayPayload.recursionDepth + 1 < maxDepth)
		{
			TraceNextRay(worldPosition + EPS * R, R, rayPayload);
		}
	}
}
