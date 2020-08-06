#include "RayTracingCommon.hlsli"

float3 fresnelSchlick(float3 f0, float3 f90, float u)
{
	return f0 + (f90 - f0) * pow(1 - u, 5);
}

float evalGGXNdf(float ggxAlpha, float NdotH)
{
	float a2 = ggxAlpha * ggxAlpha;
	float d = ((NdotH * a2 - NdotH) * NdotH + 1);
	return INV_PI * a2 / (d * d);
}

float3 sampleGGXNdf(float ggxAlpha, float2 Xi)
{
	float a2 = ggxAlpha * ggxAlpha;
	float Phi = 2 * PI * Xi.x;
	float CosTheta2 = min(1.0, (1 - Xi.y) / (1 + (a2 - 1) * Xi.y));
	float CosTheta = sqrt(CosTheta2);
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	float3 wh; // in tangent frame
	wh.x = SinTheta * cos(Phi);
	wh.y = SinTheta * sin(Phi);
	wh.z = CosTheta;

	return wh;
}

// From NVIDIA Falcor 4.0
float evalSmithGGXPreDivided(float NdotL, float NdotV, float ggxAlpha)
{
	// height-correlated
	// Optimized version of Smith, already taking into account the division by (4 * N.V * N.L)
	float a2 = ggxAlpha * ggxAlpha;
	// `NdotV *` and `NdotL *` are inversed. It's not a mistake.
	float ggxv = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
	float ggxl = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
	return 0.5f / (ggxv + ggxl);
}
