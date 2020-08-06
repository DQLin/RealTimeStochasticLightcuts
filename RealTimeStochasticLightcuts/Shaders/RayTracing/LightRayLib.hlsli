#define UE4_RANDOM

#ifdef UE4_RANDOM
#include "../RandomGenerator.hlsli"
#endif

#include "HitCommon.hlsli"

RWStructuredBuffer<float4> g_vplPositions : register(u2);
RWStructuredBuffer<float4> g_vplNormals : register(u3);
RWStructuredBuffer<float4> g_vplColors : register(u4);

static const float EPS = 0.1;

cbuffer RayGenShaderConstants : register(b0)
{
	float3 SunDirection;
	float3 SunColor;
	float4 SceneSphere; //pos in xyz, radius in w
	int DispatchOffset;
	int maxDepth;
	int frameId;
}

struct RayPayload
{
	float3 color;
	uint recursionDepth;
#ifdef UE4_RANDOM
	uint seed;
#endif
};

//hash function
float hash(float2 seed) 
{
	return frac(1.0e4 * sin(17.0*seed.x + 0.1*seed.y) *
		(0.1 + abs(sin(13.0*seed.y + seed.x)))
	);
}
