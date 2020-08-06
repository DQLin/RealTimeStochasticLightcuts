// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

//NOTE: This file combines the code of MiniEngine TAA (Microsoft) and SVGF (NVIDIA)

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

/**********************************************************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
# following conditions are met:
#  * Redistributions of code must retain the copyright notice, this list of conditions and the following disclaimer.
#  * Neither the name of NVIDIA CORPORATION nor the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT
# SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************************************************************/

#include "SVGFCommon.hlsli"

RWTexture2D<float4> texOut[2] : register(u0);
RWTexture2D<float4> texOutM : register(u2);
RWTexture2D<uint> texHistoryLength : register(u3);
RWTexture2D<float> prevImaginaryLinearDepth : register(u4);

Texture2D<uint> VelocityBuffer : register(t0);
Texture2D<float> CurDepth : register(t1);
Texture2D<float> PreDepth : register(t2);
Texture2D<float4> texPrev[2] : register(t3);
Texture2D<float4> texPrevM : register(t5);
Texture2D<float4> texCur[2] : register(t6);
Texture2D<float> GradDepth : register(t8);

SamplerState LinearSampler : register(s1);

cbuffer CSConstants : register(b0)
{
	float4x4 CurToPrevXForm;
	float2 RcpBufferDim;    // 1 / width, 1 / height
	float RcpSpeedLimiter;
	float gAlpha;
	float gMomentsAlpha;
	int disableTAA;
	int numInputs;
	int maxHistoryLength;
}

static const uint kLdsPitch = 18;
static const uint kLdsRows = 18;

groupshared float ldsDepth[kLdsPitch * kLdsRows];
groupshared float ldsR[2][kLdsPitch * kLdsRows];
groupshared float ldsG[2][kLdsPitch * kLdsRows];
groupshared float ldsB[2][kLdsPitch * kLdsRows];

float UnpackXY(uint x)
{
	return f16tof32((x & 0x1FF) << 4 | (x >> 9) << 15) * 32768.0;
}

float UnpackZ(uint x)
{
	return f16tof32((x & 0x7FF) << 2 | (x >> 11) << 15) * 128.0;
}

float3 UnpackVelocity(uint Velocity)
{
	return float3(UnpackXY(Velocity & 0x3FF), UnpackXY((Velocity >> 10) & 0x3FF), UnpackZ(Velocity >> 20));
}

float2 STtoUV(float2 ST)
{
	return (ST + 0.5) * RcpBufferDim;
}

float MaxOf(float4 Depths) { return max(max(Depths.x, Depths.y), max(Depths.z, Depths.w)); }

int2 GetClosestPixel(uint Idx, out float ClosestDepth)
{
	float DepthO = ldsDepth[Idx];
	float DepthW = ldsDepth[Idx - 1];
	float DepthE = ldsDepth[Idx + 1];
	float DepthN = ldsDepth[Idx - kLdsPitch];
	float DepthS = ldsDepth[Idx + kLdsPitch];

	ClosestDepth = min(DepthO, min(min(DepthW, DepthE), min(DepthN, DepthS)));

	if (DepthN == ClosestDepth)
		return int2(0, -1);
	else if (DepthS == ClosestDepth)
		return int2(0, +1);
	else if (DepthW == ClosestDepth)
		return int2(-1, 0);
	else if (DepthE == ClosestDepth)
		return int2(+1, 0);

	return int2(0, 0);
}


[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
	const uint ldsHalfPitch = kLdsPitch / 2;

	// Prefetch an 16x16 tile of pixels (8x8 colors) including a 1 pixel border
   // 18x18 IDs with 4 IDs per thread = 81 threads
	for (uint i = GI; i < 81; i += 256)
	{
		uint X = (i % ldsHalfPitch) * 2;
		uint Y = (i / ldsHalfPitch) * 2;
		uint TopLeftIdx = X + Y * kLdsPitch;
		int2 TopLeftST = Gid.xy * uint2(16, 16) - 1 + uint2(X, Y);
		float2 UV = RcpBufferDim * (TopLeftST + float2(1, 1));
		float4 Depths = CurDepth.Gather(LinearSampler, UV);
		ldsDepth[TopLeftIdx + 0] = Depths.w;
		ldsDepth[TopLeftIdx + 1] = Depths.z;
		ldsDepth[TopLeftIdx + kLdsPitch] = Depths.x;
		ldsDepth[TopLeftIdx + 1 + kLdsPitch] = Depths.y;
	}
	GroupMemoryBarrierWithGroupSync();

	uint ldsIdx = GTid.x + GTid.y * kLdsPitch + kLdsPitch + 1;

	GroupMemoryBarrierWithGroupSync();

	uint2 ST = DTid.xy;

	float3 curS = texCur[0][ST].rgb;
#ifdef DOUBLE_INPUT
	float4 curU_ = numInputs > 1 ? texCur[1][ST] : 0;
	float3 curU = curU_.rgb;
	float imaginaryLinearDepth = curU_.a;
#endif
	float4 moments = 0;
	moments.r = luminance(curS);
#ifdef DOUBLE_INPUT
	if (numInputs > 1) moments.b = luminance(curU);
#endif
	moments.g = moments.r * moments.r;
#ifdef DOUBLE_INPUT
	if (numInputs > 1) moments.a = moments.b * moments.b;
#endif

	if (disableTAA)
	{
		texOutM[ST] = moments;
		texOut[0][ST] = float4(curS, 0);
#ifdef DOUBLE_INPUT
		if (numInputs > 1) texOut[1][ST] = float4(curU, 0);
#endif
		texHistoryLength[ST] = 0;
		return;
	}

	uint historyLength = texHistoryLength[ST];

	float CompareDepth;
	float3 Velocity = UnpackVelocity(VelocityBuffer[ST + GetClosestPixel(ldsIdx, CompareDepth)]);
	CompareDepth += Velocity.z;
	float TemporalDepth = MaxOf(PreDepth.Gather(LinearSampler, STtoUV(ST + Velocity.xy))) + 1e-4;
	bool success = TemporalDepth >= CompareDepth;

	if (success)
	{
		float SpeedFactor = saturate(1.0 - length(Velocity.xy) * RcpSpeedLimiter);

		historyLength++;
		historyLength = min(historyLength, maxHistoryLength);
		float4 prevS = texPrev[0].SampleLevel(LinearSampler, STtoUV(ST + Velocity.xy), 0);

		// reproject glossy reflection
#ifdef DOUBLE_INPUT
		float2 CurPixel = ST + 0.5;
		float4 HPos = float4(CurPixel * imaginaryLinearDepth, 1.0, imaginaryLinearDepth);
		float4 PrevHPos = mul(CurToPrevXForm, HPos);
		PrevHPos.xy /= PrevHPos.w;
		float2 imaginaryVelocity = PrevHPos.xy - CurPixel;

		float4 prevU = numInputs > 1 ? texPrev[1].SampleLevel(LinearSampler, STtoUV(ST + imaginaryVelocity), 0) : 0;
		if (numInputs > 1)
		{
			if (imaginaryLinearDepth == 0.0)
				prevU.rgb = curU;
		}

#endif
		float4 prevM = texPrevM.SampleLevel(LinearSampler, STtoUV(ST + Velocity.xy), 0);
		const float alpha = max(gAlpha, 1.0 / historyLength);
		const float alphaMoments = max(gMomentsAlpha, 1.0 / historyLength);
		float4 OutS;
#ifdef DOUBLE_INPUT
		float4 OutU;

		float outUBlendAlpha = alpha;
		if (numInputs > 1 && historyLength > 1)
		{
			float prevDepth = prevImaginaryLinearDepth[ST];
			outUBlendAlpha = lerp(alpha, 0.5, saturate(5 * abs(imaginaryLinearDepth - prevDepth)));
		}

#endif

		OutS = lerp(prevS, float4(curS, 0), alpha);
#ifdef DOUBLE_INPUT
		if (numInputs > 1) OutU = lerp(prevU, float4(curU, 0), outUBlendAlpha);
#endif

		moments = lerp(prevM, moments, alphaMoments);
		float2 variance = max(float2(0, 0), moments.ga - moments.rb * moments.rb);
		OutS.a = variance.r;
#ifdef DOUBLE_INPUT
		if (numInputs > 1) OutU.a = variance.g;
#endif

		texOutM[ST] = moments;
		texOut[0][ST] = OutS;
#ifdef DOUBLE_INPUT		
		if (numInputs > 1) texOut[1][ST] = OutU;
#endif
	}
	else
	{
		// temporal variance not available, need to use spatial variance
		historyLength = 0;
		texOut[0][ST] = float4(curS, 0);
#ifdef DOUBLE_INPUT
		if (numInputs > 1) texOut[1][ST] = float4(curU, 0);
#endif
		texOutM[ST] = moments;
	}

	texHistoryLength[ST] = historyLength;
	prevImaginaryLinearDepth[ST] = imaginaryLinearDepth;
}
