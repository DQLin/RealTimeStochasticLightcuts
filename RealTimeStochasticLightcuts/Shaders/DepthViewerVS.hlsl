//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  James Stanard
//             Alex Nankervis
//

cbuffer VSConstants : register(b0)
{
	float4x4 modelMatrix;
	float4x4 viewProjectionMatrix;
	float3 ViewerPos;
};

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
	uint instanceid : INSTANCEID;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv : TexCoord0;
};

StructuredBuffer<float4x4> globalMatrixBuffer : register(t16);
StructuredBuffer<float4x4> globalInvTransposeMatrixBuffer : register(t17);

VSOutput main(VSInput vsInput)
{
    VSOutput vsOutput;

	float4x4 globalMatrix = mul(globalMatrixBuffer[vsInput.instanceid], modelMatrix);
	float4 worldPosition = mul(globalMatrix, float4(vsInput.position, 1.0));
	vsOutput.pos = mul(viewProjectionMatrix, worldPosition);
    vsOutput.uv = vsInput.texcoord0;
    return vsOutput;
}
