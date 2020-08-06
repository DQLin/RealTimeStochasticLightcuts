#include "HitCommon.hlsli"

[shader("miss")]
void Miss(inout ShadowRayPayload payload)
{
}

[shader("anyhit")]
void AnyHit(inout ShadowRayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	uint instanceId = InstanceID();
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
void Hit(inout ShadowRayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	payload.RayHitT = RayTCurrent();
}
