// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 texcoord0 : TEXCOORD;
};

struct VSOutput
{
	float4 position : SV_Position;
	float2 texCoord : TexCoord0;
	nointerpolation int triOffset : TRIANGLEID;
};

cbuffer Constants : register(b1)
{
	uint2 k;
};

VSOutput main(VSInput vsInput, uint vertId : SV_VertexID)
{
	VSOutput vsOutput;
	float u = vsInput.texcoord0.x, v = vsInput.texcoord0.y;	

	// clamp negative u,v ( ideally we should use a target texture that covers some negative uv range )
	if (u < 0.0) u = 0.0;
	if (v < 0.0) v = 0.0;

	while (u > 2.0) u -= 2.0;
	while (v > 2.0) v -= 2.0;

	vsOutput.position = float4(u - 1, v - 1, 0, 1);
	vsOutput.texCoord = vsInput.texcoord0;
	vsOutput.triOffset = k.x;
	return vsOutput;
}