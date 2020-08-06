#include "BRDF.hlsli"

struct RayTraceMeshInfo
{
	uint  m_indexOffsetBytes;
	uint  m_uvAttributeOffsetBytes;
	uint  m_normalAttributeOffsetBytes;
	uint  m_tangentAttributeOffsetBytes;
	uint  m_bitangentAttributeOffsetBytes;
	uint  m_positionAttributeOffsetBytes;
	uint  m_attributeStrideBytes;
	uint  m_materialInstanceId;
	int   m_emitTexId; // -1 for no emission texture
	float3 m_emissiveColor;
	float3 m_materialAlbedo;
};

cbuffer Material : register(b3)
{
	uint MeshInfoID;
	uint Use16bitIndex;
}

struct ShadowRayPayload
{
	float RayHitT;
};

RaytracingAccelerationStructure g_accel : register(t0);
StructuredBuffer<RayTraceMeshInfo> g_meshInfo : register(t1);
ByteAddressBuffer g_indices[2] : register(t2);
ByteAddressBuffer g_attributes[2] : register(t4);
Texture2D<float4> g_localTexture : register(t6);
Texture2D<float4> g_localNormal : register(t7);

SamplerState g_s0 : register(s0);

float2 GetUVAttribute(uint instanceId, uint byteOffset)
{
	return asfloat(g_attributes[instanceId].Load2(byteOffset));
}

uint3 Load3x16BitIndices(
	uint instanceId,
	uint offsetBytes)
{
	const uint dwordAlignedOffset = offsetBytes & ~3;

	const uint2 four16BitIndices = g_indices[instanceId].Load2(dwordAlignedOffset);

	uint3 indices;

	if (dwordAlignedOffset == offsetBytes)
	{
		indices.x = four16BitIndices.x & 0xffff;
		indices.y = (four16BitIndices.x >> 16) & 0xffff;
		indices.z = four16BitIndices.y & 0xffff;
	}
	else
	{
		indices.x = (four16BitIndices.x >> 16) & 0xffff;
		indices.y = four16BitIndices.y & 0xffff;
		indices.z = (four16BitIndices.y >> 16) & 0xffff;
	}

	return indices;
}

uint3 Load3x32BitIndices(
	uint instanceId,
	uint offsetBytes)
{
	const uint4 four32BitIndices = g_indices[instanceId].Load4(offsetBytes);
	return four32BitIndices.xyz;
}

uint3 LoadIndices(
	uint instanceId,
	uint indexOffsetBytes,
	uint primIndex)
{
	if (Use16bitIndex && instanceId == 0) return Load3x16BitIndices(instanceId, indexOffsetBytes + primIndex * 3 * 2);
	else return Load3x32BitIndices(instanceId, indexOffsetBytes + primIndex * 3 * 4);
}
