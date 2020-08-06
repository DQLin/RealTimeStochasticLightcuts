// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "LightTreeUtilities.hlsli"

StructuredBuffer<BLASInstanceHeader> g_BLASHeaders : register(t64);

struct VSInput
{
	float4 position : POSITION;
	float3 lower : LOWER;
	float3 upper : UPPER;
	int level : LEVEL; // -1 -> bogus light
	int index : INDEX; // we store BLAS ID here, -1 -> no BLAS Id 
	uint instanceId : SV_InstanceID;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	nointerpolation int level : LEVEL;
};

cbuffer VSConstants : register(b0)
{
	float4x4 modelToProjection;
};

VSOutput main(VSInput input)
{
	VSOutput output;

	float3 center = 0.5 * (input.lower + input.upper);
	float3 dimension = 0.5 * (input.upper - input.lower);

	float4x4 model = {
		dimension.x, 0, 0, center.x,
		0, dimension.y, 0, center.y,
		0, 0, dimension.z, center.z,
		0, 0, 0, 1
	};

	// tranformation
	if (input.index >= 0)
	{
		BLASInstanceHeader header = g_BLASHeaders[input.index];
		float3x3 rot = header.rotation;
		float3 trans = header.translation;

		float4x4 rotTrans = {
			rot._m00, rot._m01, rot._m02, trans.x,
			rot._m10, rot._m11, rot._m12, trans.y,
			rot._m20, rot._m21, rot._m22, trans.z,
			0, 0, 0, 1
		};
		model = mul(rotTrans, model);
	}

	output.position = mul(model, float4(input.position.xyz, 1.0));
	output.position = mul(modelToProjection, output.position);
	output.level = input.level;
	return output;
}