#include "LightRayLib.hlsli"

#ifdef UE4_RANDOM
inline float2 GetUnitDiskSample(inout uint seed)
{
	float2 rnd = float2(Rand(seed), Rand(seed));
#else
inline float2 GetUnitDiskSample(uint2 seed)
{
	float2 rnd = float2(hash(seed + float2(0.5, 0.5)), hash(seed.yx + float2(0.5, 0.5)));
#endif
	float R = sqrt(rnd.x);
	float theta = 2 * PI * rnd.y;
	float2 p = float2(R*cos(theta), R*sin(theta));
	return p;
}

#ifdef UE4_RANDOM
inline void GenerateDirectionalLightRay(inout uint seed, out float3 origin, out float pdf)
#else
inline void GenerateDirectionalLightRay(uint2 seed, out float3 origin, out float pdf)
#endif
{
	float3 v1, v2;
	CoordinateSystem(SunDirection, v1, v2);
#ifdef UE4_RANDOM
	float2 cd = GetUnitDiskSample(seed);
#else
	float2 cd = GetUnitDiskSample(seed + DispatchOffset);
#endif
	float3 pDisk = SceneSphere.xyz - SceneSphere.w * SunDirection + SceneSphere.w * (cd.x * v1 + cd.y * v2);
	origin = pDisk;
	pdf = 1 / (PI * SceneSphere.w * SceneSphere.w);
}

[shader("raygeneration")]
void RayGen()
{
	float3 origin;
	float pdf;

#ifdef UE4_RANDOM
	uint seed = RandInit(DispatchRaysDimensions().x * DispatchRaysIndex().y + DispatchRaysIndex().x, 1234);
	GenerateDirectionalLightRay(seed, origin, pdf);
#else
	GenerateDirectionalLightRay(DispatchRaysIndex().xy, origin, pdf);
#endif

	float3 alpha = SunColor / pdf;

	RayDesc rayDesc = { origin,
		0.0f,
		SunDirection,
		FLT_MAX };
	RayPayload payload;
	payload.color = alpha;
	payload.recursionDepth = 0;
#ifdef UE4_RANDOM
	payload.seed = seed;
#endif
	TraceRay(g_accel, RAY_FLAG_FORCE_NON_OPAQUE, ~0, 0, 1, 0, rayDesc, payload);
}
