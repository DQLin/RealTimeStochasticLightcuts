// Modified from the NVIDIA SVGF sample code

#include "SVGFCommon.hlsli"

#define INV_PI 0.318309886

Texture2D<float4> texIn[2] : register(t0);
Texture2D<float4> texNormal : register(t3);
Texture2D<float4> texAlbedo : register(t4);
Texture2D<float> texDepth : register(t5);
Texture2D<float> GradDepth : register(t6);
RWTexture2D<float4> texOutput[2] : register(u0);
RWTexture2D<float3> resultRatio : register(u2);

cbuffer CSConstants : register(b0)
{
	float c_phi;
	float n_phi;
	float z_phi;
	float stepWidth;
	int iter;
	int maxIter;
	int numInputs;
}

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	const int2 center = DTid.xy;

	const float epsVariance = 1e-10;
	const float kernel[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };

	const float4 centerS = texIn[0][center];
#ifdef DOUBLE_INPUT
	const float4 centerU = numInputs > 1 ? texIn[1][center] : 0;
#endif
	const float centerSl = luminance(centerS.rgb);
#ifdef DOUBLE_INPUT
	const float centerUl = numInputs > 1 ? luminance(centerU.rgb) : 0;
#endif
	const float3 centerNormal = texNormal[center].xyz;
	const float centerLinearDepth = texDepth[center];
	const float depthGradient = GradDepth[center];

#ifdef DOUBLE_INPUT
	const float2 var = numInputs > 1 ? computeVarianceCenter(center, texIn[0], texIn[1]) : computeVarianceCenter(center, texIn[0]);
#else
	const float2 var = computeVarianceCenter(center, texIn[0]);
#endif

	const float phiSl = c_phi * sqrt(max(0.0, epsVariance + var.r));
#ifdef DOUBLE_INPUT
	const float phiUl = numInputs > 1 ? c_phi * sqrt(max(0.0, epsVariance + var.g)) : 0;
#endif

	float sumWeight_S = 1.0;
#ifdef DOUBLE_INPUT
	float sumWeight_U = 1.0;
#endif
	float4 sum_S = centerS;
#ifdef DOUBLE_INPUT
	float4 sum_U = centerU;
#endif

	for (int yOffset = -2; yOffset <= 2; yOffset++)
	{
		for (int xOffset = -2; xOffset <= 2; xOffset++)
		{
			if (xOffset != 0 || yOffset != 0)
			{
				int2 tap = center + stepWidth * int2(xOffset, yOffset);
				float4 tapS = texIn[0][tap];
#ifdef DOUBLE_INPUT
				float4 tapU = numInputs > 1 ? texIn[1][tap] : 0;
#endif
				float3 tapNormal = texNormal[tap].xyz;
				float tapLinearDepth = texDepth[tap];

				float tapSl = luminance(tapS.rgb);
#ifdef DOUBLE_INPUT
				float tapUl = numInputs > 1 ? luminance(tapU.rgb) :0;
#endif

				float kernelWeight = kernel[abs(xOffset)] * kernel[abs(yOffset)];

				float Sl_w = abs(centerSl - tapSl) / phiSl;
#ifdef DOUBLE_INPUT
				float Ul_w = numInputs > 1 ? abs(centerUl - tapUl) / phiUl : 0;
#endif

				float n_w = pow(max(0.0, dot(centerNormal, tapNormal)), n_phi);

				float z_w = abs(centerLinearDepth - tapLinearDepth) / (abs(depthGradient * length(float2(xOffset, yOffset))) * z_phi + 1e-4);

				float S_w = exp(0.0 - max(Sl_w, 0.0) - max(z_w, 0.0)) * n_w * kernelWeight;
#ifdef DOUBLE_INPUT
				float U_w = numInputs > 1 ? exp(0.0 - max(Ul_w, 0.0) - max(z_w, 0.0)) * n_w * kernelWeight : 0;
#endif

				sum_S += float4(S_w.xxx, S_w * S_w) * tapS;
#ifdef DOUBLE_INPUT
				sum_U += numInputs > 1 ? float4(U_w.xxx, U_w * U_w) * tapU : 0;
#endif

				sumWeight_S += S_w;
#ifdef DOUBLE_INPUT
				sumWeight_U += U_w;
#endif
			}
		}
	}

	sum_S = float4(sum_S / float4(sumWeight_S.xxx, sumWeight_S * sumWeight_S));
	texOutput[0][center] = sum_S;

#ifdef DOUBLE_INPUT
	if (numInputs > 1)
	{
		sum_U = float4(sum_U / float4(sumWeight_U.xxx, sumWeight_U * sumWeight_U));
		texOutput[1][center] = sum_U;
	}
#endif

	if (iter == maxIter - 1) //final pass
	{
#ifdef DOUBLE_INPUT
		if (numInputs > 1)
		{
			resultRatio[center] = INV_PI * texAlbedo[center].rgb * sum_S.xyz + sum_U.xyz;
		}
		else
#endif
			resultRatio[center] = INV_PI * texAlbedo[center].rgb * sum_S.xyz;
	}
}
