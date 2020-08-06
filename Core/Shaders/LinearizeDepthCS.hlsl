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
// Author:  James Stanard 
//

#define GROUPSIZE_X 16
#define GROUPSIZE_Y 16

#include "SSAORS.hlsli"

RWTexture2D<float> LinearZ : register(u0);
Texture2D<float> Depth : register(t0);

groupshared float lds[GROUPSIZE_X * GROUPSIZE_Y];

cbuffer CB0 : register(b0)
{
    float ZMagic;                // (zFar - zNear) / zNear
}

[RootSignature(SSAO_RootSig)]
[numthreads(GROUPSIZE_X, GROUPSIZE_Y, 1 )]
void main( uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID )
{
	LinearZ[DTid.xy] = 1.0 / (ZMagic * Depth[DTid.xy] + 1.0);
}
