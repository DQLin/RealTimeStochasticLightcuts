static const float FLT_MAX = asfloat(0x7F7FFFFF);
static const float PI = 3.141592654;
static const float INV_PI = 0.318309886;

inline void CoordinateSystem(float3 v1, out float3 v2, out float3 v3)
{
	if (abs(v1.x) > abs(v1.y)) v2 = float3(-v1.z, 0, v1.x) / sqrt(v1.x * v1.x + v1.z * v1.z);
	else v2 = float3(0, v1.z, -v1.y) / sqrt(v1.y * v1.y + v1.z * v1.z);
	v3 = normalize(cross(v1, v2));
}

float3x3 GetTangentBasis(float3 N)
{
	float3 T, B;
	CoordinateSystem(N, T, B);
	return float3x3(T, B, N);
}

float2 UniformSampleDisk(float2 Xi)
{
	float Theta = 2 * PI * Xi.x;
	float Radius = sqrt(Xi.y);
	return Radius * float2(cos(Theta), sin(Theta));
}


// From UE 4.22
uint3 Rand3DPCG16(int3 p)
{
	uint3 v = uint3(p);
	v = v * 1664525u + 1013904223u;
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;
	return v >> 16u;
}

// From UE 4.22
float2 Rand1SPPDenoiserInput(uint2 PixelPos, uint frameId)
{
	float2 E;

	{
		uint2 Random = Rand3DPCG16(int3(PixelPos, frameId % 8)).xy;
		E = float2(Random)* rcp(65536.0); // equivalent to Hammersley16(0, 1, Random).
	}

	return E;
}

