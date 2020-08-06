// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

struct VSOutput
{
	sample float4 position : SV_Position;
	sample float2 uv : TexCoord0;
	nointerpolation int triOffset : TRIANGLEID;
};

cbuffer PSConstants : register(b0)
{
	float3 emissionColor;
}

Texture2D<float3> texEmissive		: register(t3);
RWStructuredBuffer<uint> triangleIntensityList : register(u2);
RWStructuredBuffer<uint> triangleNumOfTexels : register(u3);

SamplerState sampler0 : register(s0);


float4 main(VSOutput vsOutput, uint primID : SV_PrimitiveID) : SV_TARGET0 
{
	float3 emissiveTexColor = texEmissive.Sample(sampler0, vsOutput.uv);
	emissiveTexColor *= emissionColor;
	int triId = vsOutput.triOffset + primID;
	float power = emissiveTexColor.r + emissiveTexColor.g + emissiveTexColor.b;
	uint org;
	if (power > 0)  InterlockedAdd(triangleIntensityList[triId], uint(clamp(255 * power, 0.f, 765.f)), org);
	InterlockedAdd(triangleNumOfTexels[triId], 1, org);

	return float4(emissiveTexColor,1); //for visualization
}
