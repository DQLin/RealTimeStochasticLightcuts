// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

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
	float4x4 prevViewProjectionMatrix;
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
    float4 position : SV_Position;
    float3 worldPos : WorldPos;
    float2 texCoord : TexCoord0;
    float3 normal : Normal;
    float3 tangent : Tangent;
    float3 bitangent : Bitangent;
	float4 curPosH : CurSVPos;
	float4 prevPosH : PreviousSVPos;
};

StructuredBuffer<float4x4> globalMatrixBuffer : register(t16);
StructuredBuffer<float4x4> globalInvTransposeMatrixBuffer : register(t17);
StructuredBuffer<float4x4> previousGlobalMatrixBuffer : register(t18);

VSOutput main(VSInput vsInput)
{
    VSOutput vsOutput;

	float4x4 globalMatrix = mul(globalMatrixBuffer[vsInput.instanceid], modelMatrix);
	float4 worldPosition = mul(globalMatrix, float4(vsInput.position, 1.0));
    vsOutput.position = mul(viewProjectionMatrix, worldPosition);
    vsOutput.worldPos = worldPosition;
    vsOutput.texCoord = vsInput.texcoord0;

	float4x4 prevGlobalMatrix = previousGlobalMatrixBuffer[vsInput.instanceid];
	float4 prevWorldPosition = mul(prevGlobalMatrix, float4(vsInput.position, 1.0));
	vsOutput.prevPosH = mul(prevViewProjectionMatrix, prevWorldPosition);
	vsOutput.curPosH = vsOutput.position;

	float4x4 invTransposeMatrix = globalInvTransposeMatrixBuffer[vsInput.instanceid];
	float3x3 normalMatrix = float3x3(invTransposeMatrix._m00_m01_m02, invTransposeMatrix._m10_m11_m12, invTransposeMatrix._m20_m21_m22);
	float3x3 globalMatrix33 = float3x3(globalMatrix._m00_m01_m02, globalMatrix._m10_m11_m12, globalMatrix._m20_m21_m22);

    vsOutput.normal = mul(normalMatrix, vsInput.normal);
    vsOutput.tangent = mul(normalMatrix, vsInput.tangent);
    vsOutput.bitangent = mul(globalMatrix33, vsInput.bitangent);

    return vsOutput;
}
