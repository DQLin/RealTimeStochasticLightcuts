// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#define UE4_RANDOM

#ifdef UE4_RANDOM
#include "../RandomGenerator.hlsli"
#endif

#include "HitCommon.hlsli"

#define HLSL
#include "../../Source/LightTreeMacros.h"

struct ReflectionRayPayload
{
	float3 color;
	float hitDistance;
};

cbuffer RayTraceReflectionConstants : register(b0)
{
	float4x4 ViewProjMatrix;
	float3 viewerPos;
	int frameId;
	float sceneRadius;
	float shadowBiasScale;
	float ZMagic;
};

Texture2D<float4> texPosition : register(t32);
Texture2D<float4> texNormal : register(t33);
Texture2D<float4> texAlbedo : register(t34);
Texture2D<float4> texSpecular : register(t35);
Texture2D<float4> emissiveTextures[] : register(t0, space1);
StructuredBuffer<float4x4> globalMatrixBuffer : register(t9);
RWTexture2D<float4> Result: register(u12);

[shader("miss")]
void Miss(inout ReflectionRayPayload payload)
{
}


[shader("anyhit")]
void AnyHit(inout ReflectionRayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
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
void Hit(inout ReflectionRayPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
	uint instanceId = 0;
	RayTraceMeshInfo info = g_meshInfo[MeshInfoID];
	const uint3 ii = LoadIndices(instanceId, info.m_indexOffsetBytes, PrimitiveIndex());
	float2 uv0 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes);
	float2 uv1 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes);
	float2 uv2 = GetUVAttribute(instanceId, info.m_uvAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes);

	float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	float2 uv = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;

	const float3 normal0 = asfloat(g_attributes[instanceId].Load3(info.m_normalAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
	const float3 normal1 = asfloat(g_attributes[instanceId].Load3(info.m_normalAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
	const float3 normal2 = asfloat(g_attributes[instanceId].Load3(info.m_normalAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
	float3 vsNormal = normalize(normal0 * bary.x + normal1 * bary.y + normal2 * bary.z);

	float4x4 globalMatrix = globalMatrixBuffer[InstanceID()];
	float3x3 globalMatrix33 = float3x3(globalMatrix._m00_m01_m02, globalMatrix._m10_m11_m12, globalMatrix._m20_m21_m22);

	vsNormal = mul(globalMatrix33, vsNormal);

	float3 ret = 0.f;

	if (dot(WorldRayDirection(), vsNormal) < 0)
	{
		ret = info.m_emissiveColor;
		if (info.m_emitTexId != -1)  ret *= emissiveTextures[info.m_emitTexId].SampleLevel(g_s0, uv, 0).rgb;
	}

	if (any(ret) != 0) rayPayload.hitDistance = RayTCurrent();

	rayPayload.color = ret;
}

[shader("raygeneration")]
void RayGen()
{
	int2 pixelPos = DispatchRaysIndex().xy;

	float3 p = texPosition[pixelPos].xyz;
	float3 N = texNormal[pixelPos].xyz;
	float3 V = normalize(viewerPos - p);
	float4 specularRoughness = texSpecular[pixelPos];

	// sample glossy reflection

	RandomSequence rng;

#ifdef UE4_RANDOM
	RandomSequence_Initialize(rng, DispatchRaysDimensions().x * (pixelPos.y + DispatchRaysDimensions().y) + pixelPos.x, frameId);
	rng.Type = 2; // scrambled halton
#endif

	float3 T, B;
	CoordinateSystem(N, T, B);

	float ggxAlpha = specularRoughness.a*specularRoughness.a;
	float3 accumColor = 0;
	float accumDistance = 1e20;
	float imaginaryLinearizedDepth = 0.0;

	const int sampleCount = 1;

	float3 V_local = mul(float3x3(T, B, N), V);

	for (int sampleId = 0; sampleId < sampleCount; sampleId++)
	{
		float pdf;

#ifdef GROUND_TRUTH
		float2 Xi = RandomSequence_GenerateSample2D(rng);
#else
		float2 Xi = Rand1SPPDenoiserInput(pixelPos, frameId);
#endif

		float3 H_local = sampleGGXNdf(ggxAlpha, Xi);

		float3 H = H_local.x * T + H_local.y * B + H_local.z * N;
		float3 L = reflect(-V, H);

		// evalute f and pdf

		float NdotH = saturate(H_local.z);
		float VdotH = saturate(dot(V, H));

		float NdotV = saturate(dot(N, V));
		float NdotL = saturate(dot(N, L));

		float D = evalGGXNdf(ggxAlpha, NdotH);
		float G = evalSmithGGXPreDivided(NdotL, NdotV, ggxAlpha);
		float3 F = fresnelSchlick(specularRoughness.rgb, 1, VdotH);

		// sampleGGXNdf
		float3 weight = G * F * VdotH * NdotL * 4.f / NdotH;

		RayDesc rayDesc = { p,
			shadowBiasScale * sceneRadius,
			L,
			FLT_MAX };
		ReflectionRayPayload payload;
		payload.color = 0;
		payload.hitDistance = 1e20; // no hit or hit non-emissive object
		TraceRay(g_accel, RAY_FLAG_FORCE_NON_OPAQUE, ~0, 0, 1, 0, rayDesc, payload);

		accumColor += weight * payload.color;
		accumDistance = min(payload.hitDistance, accumDistance);
	}

	if (accumDistance != 1e20)
	{
		float4 clipPos = mul(ViewProjMatrix, p - V * accumDistance);
		float imaginaryDepth = clipPos.z / clipPos.w;
		imaginaryLinearizedDepth = 1.0 / (ZMagic * imaginaryDepth + 1.0);
	}

	if (any(isnan(accumColor)) || any(isinf(accumColor))) accumColor = 0;

#ifdef GROUND_TRUTH
	Result[pixelPos] = float4(frameId == 0 ? accumColor / sampleCount : Result[pixelPos] * frameId / (frameId + 1.f) + accumColor / sampleCount / (frameId + 1.f), 0.f);
#else
	Result[pixelPos] = float4(accumColor / sampleCount, imaginaryLinearizedDepth);
#endif
}
