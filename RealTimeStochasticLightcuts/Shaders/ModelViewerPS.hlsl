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
// Author(s):	James Stanard
//				Alex Nankervis
//
// Thanks to Michal Drobot for his feedback.

// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)
// single-iteration loop
#pragma warning (disable: 3557)

struct VSOutput
{
    sample float4 position : SV_Position;
    sample float3 worldPos : WorldPos;
    sample float2 uv : TexCoord0;
    sample float3 normal : Normal;
    sample float3 tangent : Tangent;
    sample float3 bitangent : Bitangent;
	sample float4 curPosH : CurSVPos;
	sample float4 prevPosH : PreviousSVPos;
};

struct PSOutput
{
	float4 position : SV_TARGET0;
	float4 normal   : SV_TARGET1;
	float4 albedo   : SV_TARGET2;
	float4 specular : SV_TARGET3;
	float4 emission : SV_TARGET4;
	uint velocity : SV_TARGET5;
};

Texture2D texDiffuse		: register(t0);
Texture2D<float3> texSpecular		: register(t1);
Texture2D texNormal			: register(t2);
Texture2D<float3> texEmissive		: register(t3);

cbuffer PSConstants : register(b0)
{
    float3 SunDirection;
    float3 SunColor;
	float3 diffuseColor;
	float3 specularColor;
	float3 emissionColor;
	float3 ViewerPos;
	int pad;
	int hasEmissiveTexture;
	int textureFlags;
	float2 screenDimension;
	float ZMagic;
}

SamplerState sampler0 : register(s0);
SamplerComparisonState shadowSampler : register(s1);

void normalizeNormal( inout float3 texNormal )
{
    float normalLenSq = dot(texNormal, texNormal);
    float invNormalLen = rsqrt(normalLenSq);
    texNormal *= invNormalLen;
}

uint PackXY(float x)
{
	uint signbit = asuint(x) >> 31;
	x = clamp(abs(x / 32768.0), 0, asfloat(0x3BFFE000));
	return (f32tof16(x) + 8) >> 4 | signbit << 9;
}

// Designed to compress (-1.0, 1.0) with a signed 8e3 float
uint PackZ(float x)
{
	uint signbit = asuint(x) >> 31;
	x = clamp(abs(x / 128.0), 0, asfloat(0x3BFFE000));
	return (f32tof16(x) + 2) >> 2 | signbit << 11;
}

uint PackVelocity(float3 Velocity)
{
	return PackXY(Velocity.x) | PackXY(Velocity.y) << 10 | PackZ(Velocity.z) << 20;
}

PSOutput main(VSOutput vsOutput, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
	PSOutput output;
    float4 diffuseAlbedo = texDiffuse.Sample(sampler0, vsOutput.uv);

    float3 normal;

	const bool validTangentSpace = dot(vsOutput.bitangent, vsOutput.bitangent) > 0.f;

	if (!validTangentSpace || all(vsOutput.bitangent) == 0) //tangent space not defined
	{
		normal = normalize(vsOutput.normal);
	}
	else
    {
		vsOutput.bitangent = normalize(vsOutput.bitangent - vsOutput.normal * (dot(vsOutput.bitangent, vsOutput.normal)));
		vsOutput.tangent = normalize(cross(vsOutput.bitangent, vsOutput.normal));

		// two element normal
		if (textureFlags == 1)
		{
			float2 rg = texNormal.Sample(sampler0, vsOutput.uv).rg;
			normal.xy = rg * 2.0 - 1.0;
			normal.z = saturate(dot(rg, rg)); // z = r*r + g*g
			normal.z = sqrt(1 - normal.z);
		}
		else
		{
			normal = texNormal.Sample(sampler0, vsOutput.uv).rgb * 2.0 - 1.0;
		}
		normalizeNormal(normal); // normalized inside
        float3x3 tbn = float3x3(normalize(vsOutput.tangent), normalize(vsOutput.bitangent), normalize(vsOutput.normal));
        normal = normalize(mul(normal, tbn));
    }

	if (dot(normal, vsOutput.worldPos - ViewerPos) > 0) normal = -normal;
	
	// for metal-roughness workflow (g -- linear roughness, b -- metallic)
    float3 specParams = texSpecular.Sample(sampler0, vsOutput.uv);

	if (any(isnan(normal))) normal = float3(1.0, 0.0, 0.0);

	output.position = float4(vsOutput.worldPos, 1.0);
	output.normal = float4(normal, 1.0);
	output.albedo = float4(diffuseColor * diffuseAlbedo.rgb, 1.0);
	
	if (textureFlags == 2) // specular-glossy workflow
	{
		// for specular-glossy workflow
		output.specular = float4(specularColor * specParams, 1.0);
	}
	else
	{
		// derive F0 for dielectrics from specular value (From NVIDIA Falcor 4.0)
		// "Calculate the specular reflectance for dielectrics from the IoR, as in the Disney BSDF [Burley 2015].
		// UE4 uses 0.08 multiplied by a default specular value of 0.5, hence F0=0.04 as default. The default IoR=1.5 gives the same result."
		float F0 = 0.04;
		output.specular = float4(lerp(float3(F0, F0, F0), output.albedo, specParams.b), specParams.g);
		output.albedo = float4(lerp(output.albedo, float3(0, 0, 0), specParams.b), 1);
	}

	float3 emissiveColor = emissionColor;
	if (hasEmissiveTexture)
	{
		float3 emissiveTexColor = texEmissive.Sample(sampler0, vsOutput.uv);
		emissiveColor *= emissiveTexColor;
	}

	// pack emission color
	float emissionScale = min(255.0, max(0.0, ceil(max(emissiveColor.r, max(emissiveColor.g, emissiveColor.b)))));
	output.emission = float4(emissiveColor / emissionScale, emissionScale / 255.0);
	
	float3 prevPosH = vsOutput.prevPosH.xyz / vsOutput.prevPosH.w;
	prevPosH.xy = (float2(0.5,-0.5)*prevPosH.xy+0.5) * screenDimension;

	float3 curPosH = vsOutput.curPosH.xyz / vsOutput.curPosH.w;
	curPosH.xy = (float2(0.5, -0.5)*curPosH.xy + 0.5) * screenDimension;

	float curLinearDepth = 1.0 / (ZMagic * curPosH.z + 1.0);
	float prevLinearDepth = 1.0 / (ZMagic * prevPosH.z + 1.0);
	output.velocity = PackVelocity(float3(prevPosH.xy, prevLinearDepth) - float3(curPosH.xy, curLinearDepth));
    return output;
}
