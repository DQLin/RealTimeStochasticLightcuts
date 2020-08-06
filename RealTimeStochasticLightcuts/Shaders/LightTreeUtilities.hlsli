
#define HLSL
#define PI 3.141592654
#define INV_PI 0.318309886

#include "../Source/LightTreeMacros.h"

#undef PI
#undef INV_PI

void Swap(inout int first, inout int second)
{
	int temp = first;
	first = second;
	second = temp;
}

inline float MaxDistAlong(float3 p, float3 dir, float3 boundMin, float3 boundMax)
{
	float3 dir_p = dir * p;
	float3 mx0 = dir * boundMin - dir_p;
	float3 mx1 = dir * boundMax - dir_p;
	return max(mx0[0], mx1[0]) + max(mx0[1], mx1[1]) + max(mx0[2], mx1[2]);
}

inline float GeomTermBound(float3 p, float3 N, float3 boundMin, float3 boundMax)
{
	float nrm_max = MaxDistAlong(p, N, boundMin, boundMax);
	if (nrm_max <= 0) return 0.0f;
	float3 d = min(max(p, boundMin), boundMax) - p;
	float3 tng = d - dot(d, N) * N;
	float hyp2 = dot(tng, tng) + nrm_max * nrm_max;
	return nrm_max * rsqrt(hyp2);
}

inline float SquaredDistanceToClosestPoint(float3 p, float3 boundMin, float3 boundMax)
{
	float3 d = min(max(p, boundMin), boundMax) - p;
	return dot(d, d);
}

inline float SquaredDistanceToFarthestPoint(float3 p, float3 boundMin, float3 boundMax)
{
	float3 d = max(abs(boundMin - p), abs(boundMax - p));
	return dot(d, d);
}


inline float WidthSquared(float3 boundMin, float3 boundMax)
{
	float3 d = boundMax - boundMin;
	return d.x*d.x + d.y*d.y + d.z*d.z;
}

inline float normalizedWeights(float l2_0, float l2_1, float intensGeom0, float intensGeom1)
{
	float ww0 = l2_1 * intensGeom0;
	float ww1 = l2_0 * intensGeom1;
	return ww0 / (ww0 + ww1);
};

//https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
inline uint uintLog2(uint v)
{
	uint r; // result of log2(v) will go here
	uint shift;
	r = (v > 0xFFFF) << 4; v >>= r;
	shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
	shift = (v > 0xF) << 2; v >>= shift; r |= shift;
	shift = (v > 0x3) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);
	return r;
}
