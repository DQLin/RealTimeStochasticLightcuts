// Modified from the NVIDIA SVGF sample code

#include "SVGFCommon.hlsli"

Texture2D<float4> texIn[2] : register(t0);
Texture2D<float4> texInM : register(t2);
Texture2D<float4> texNormal : register(t4);
Texture2D<uint> texHistoryLength : register(t5);
Texture2D<float> texDepth : register(t6);
Texture2D<float> GradDepth : register(t7);
RWTexture2D<float4> texOutput[2] : register(u0);

cbuffer CSConstants : register(b0)
{
	float c_phi;
	float n_phi;
	float z_phi;
	int numInputs;
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	const int2 center = DTid.xy;

	const float epsVariance = 1e-10;

	uint historyLength = texHistoryLength[center];

	if (historyLength < 4)
	{
		const float4 centerS = texIn[0][center];
#ifdef DOUBLE_INPUT
		const float4 centerU = numInputs > 1 ? texIn[1][center] : 0;
#endif
		const float4 centerM = texInM[center];
		const float centerSl = luminance(centerS.rgb);
#ifdef DOUBLE_INPUT
		const float centerUl = numInputs > 1 ? luminance(centerU.rgb) : 0;
#endif

		const float3 centerNormal = texNormal[center].xyz;
		const float centerLinearDepth = texDepth[center];
		const float depthGradient = GradDepth[center];

		// variance for direct and indirect, filtered using 3x3 gaussian blur

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

		float3 sum_S = centerS.rgb;
#ifdef DOUBLE_INPUT
		float3 sum_U = centerU.rgb;
#endif
		float4 sum_M = centerM;

		for (int yOffset = -3; yOffset <= 3; yOffset++)
		{
			for (int xOffset = -3; xOffset <= 3; xOffset++)
			{
				if (xOffset != 0 || yOffset != 0)
				{
					int2 tap = center + int2(xOffset, yOffset);
					float4 tapS = texIn[0][tap];
					float4 tapU = numInputs > 1 ? texIn[1][tap] : 0;
					float4 tapM = texInM[tap];

					float3 tapNormal = texNormal[tap].xyz;
					float tapLinearDepth = texDepth[tap];

					float n_w = pow(max(0.0, dot(centerNormal, tapNormal)), n_phi);

					float z_w = abs(centerLinearDepth - tapLinearDepth) / (abs(depthGradient * length(float2(xOffset, yOffset))) * z_phi + 1e-4);

					float S_w = exp(0.0 - max(z_w, 0.0)) * n_w;
#ifdef DOUBLE_INPUT
					float U_w = numInputs > 1 ? exp(0.0 - max(z_w, 0.0)) * n_w : 0;
#endif

					sum_S += S_w * tapS;
#ifdef DOUBLE_INPUT
					sum_U += U_w * tapU;
#endif
					sumWeight_S += S_w;
#ifdef DOUBLE_INPUT
					sumWeight_U += U_w;
#endif

#ifdef DOUBLE_INPUT
					sum_M += tapM * float4(S_w.xx, U_w.xx);
#else
					sum_M += tapM * float4(S_w.xx, 0, 0);
#endif
				}
			}
		}

		// Clamp sums to >0 to avoid NaNs.
		sumWeight_S = max(sumWeight_S, 1e-6f);
		sum_S /= sumWeight_S;
		
#ifdef DOUBLE_INPUT
		if (numInputs > 1)
		{
			sumWeight_U = max(sumWeight_U, 1e-6f);
			sum_U /= sumWeight_U;
			sum_M /= float4(sumWeight_S.xx, sumWeight_U.xx);
		}
		else
#endif
			sum_M.rg /= sumWeight_S.xx;

		// compute variance for direct and indirect illumination using first and second moments
		float2 variance = sum_M.ga - sum_M.rb * sum_M.rb;

		// give the variance a boost for the first frames
		variance *= 4.0 / max(1.0,historyLength);

		texOutput[0][center] = float4(sum_S, variance.r);
#ifdef DOUBLE_INPUT
		if (numInputs > 1)
		{
			texOutput[1][center] = float4(sum_U, variance.g);
		}
#endif
	}
	else
	{
		texOutput[0][center] = texIn[0][center];
#ifdef DOUBLE_INPUT
		if (numInputs > 1)
		{
			texOutput[1][center] = texIn[1][center];
		}
#endif
	}
}

