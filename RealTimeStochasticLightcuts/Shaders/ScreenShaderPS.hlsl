#define HLSL
#define PI 3.141592654
#define INV_PI 0.318309886

#include "../Source/LightTreeMacros.h"
#define SLC

Texture2D<float3> DiffuseSampled : register(t64);
Texture2D<float3> FilteredCombined : register(t65);
Texture2D<float3> SpecularSampled : register(t66);
Texture2D<float> texShadow : register(t67);
Texture2D<float3> texViz : register(t68);

SamplerState sampler0 : register(s0);
SamplerComparisonState shadowSampler : register(s1);

Texture2D<float4> texPosition		: register(t32);
Texture2D<float4> texNormal		    : register(t33);
Texture2D<float4> texAlbedo			: register(t34);
Texture2D<float4> texSpecular		: register(t35);
Texture2D<float4> texEmission		: register(t36);
Texture2D<uint>	  texVelocity		: register(t37);

cbuffer PSConstants : register(b0)
{
	float4x4 WorldToShadow;
	float3 ViewerPos;
	float3 SunDirection;
	float3 SunColor;
	float4 ShadowTexelSize;
	int scrWidth;
	int scrHeight;
	int shadowRate;
	int debugMode;
	int enableFilter;
	int gUseMeshLight;
	int hasRayTracedReflection;
	int visualizeNodes;
}

// Apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, float3 lightDir, float3 halfVec)
{
	float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
	specular = lerp(specular, 1, fresnel);
}

float GetShadow(float3 ShadowCoord)
{
#ifdef SINGLE_SAMPLE
	float result = texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy, ShadowCoord.z);
#else
	const float Dilation = 2.0;
	float d1 = Dilation * ShadowTexelSize.x * 0.125;
	float d2 = Dilation * ShadowTexelSize.x * 0.875;
	float d3 = Dilation * ShadowTexelSize.x * 0.625;
	float d4 = Dilation * ShadowTexelSize.x * 0.375;
	float result = (
		2.0 * texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy, ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d2, d1), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d1, -d2), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d2, -d1), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d1, d2), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d4, d3), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d3, -d4), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d4, -d3), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d3, d4), ShadowCoord.z)
		) / 10.0;
#endif
	return result * result;
}

float3 ApplyLightCommon(
	float3	diffuseColor,	// Diffuse albedo
	float3	specularColor,	// Specular albedo
	float	specularMask,	// Where is it shiny or dingy?
	float	gloss,			// Specular power
	float3	normal,			// World-space normal
	float3	viewDir,		// World-space vector from eye to point
	float3	lightDir,		// World-space vector from point to light
	float3	lightColor		// Radiance of directional light
)
{
	float3 halfVec = normalize(lightDir - viewDir);
	float nDotH = saturate(dot(halfVec, normal));

	FSchlick(specularColor, lightDir, halfVec);

	float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;

	float nDotL = saturate(dot(normal, lightDir));

	return nDotL * lightColor * diffuseColor + lightColor * specularFactor * specularColor;
}

float3 ApplyDirectionalLight(
	float3	diffuseColor,	// Diffuse albedo
	float3	specularColor,	// Specular albedo
	float	specularMask,	// Where is it shiny or dingy?
	float	gloss,			// Specular power
	float3	normal,			// World-space normal
	float3	viewDir,		// World-space vector from eye to point
	float3	lightDir,		// World-space vector from point to light
	float3	lightColor,		// Radiance of directional light
	float3	shadowCoord		// Shadow coordinate (Shadow map UV & light-relative Z)
)
{
	float shadow = GetShadow(shadowCoord);

	return shadow * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor
	);
}

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


float3 main(float4 screenPos : SV_POSITION) : SV_TARGET0
{
	int2 pos = int2(screenPos.xy);
	if (debugMode > 0) return texViz[pos];
	float2 uv = screenPos.xy / float2(scrWidth, scrHeight);
	
	float3 worldPosition = texPosition[pos].xyz;
	float3 viewDir = worldPosition - ViewerPos;
	float3 shadowCoord = mul(WorldToShadow, float4(worldPosition, 1.0)).xyz;
	float3 diffuseAlbedo = texAlbedo[pos].rgb;
	float3 specularAlbedo = 1;
	float3 specularMask = texSpecular[pos].a;
	float gloss = 128.0;
	float3 normal = texNormal[pos].xyz;
	float3 colorSum = 0;

	// apply sunlight for VPL scenes
	if (!gUseMeshLight)
		colorSum += ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, gloss, normal, viewDir, SunDirection, SunColor, shadowCoord);

	if (enableFilter) colorSum += FilteredCombined[pos].rgb;
	else
	{
		colorSum += DiffuseSampled[pos].rgb*diffuseAlbedo*INV_PI;
		if (hasRayTracedReflection) colorSum += SpecularSampled[pos].rgb;
	}

	// add emission
	colorSum += 255.0 * texEmission[pos].a * texEmission[pos].rgb; 

	if (visualizeNodes) colorSum += texViz[pos].rgb;

	if (any(isnan(colorSum)) || any(isinf(colorSum))) colorSum = 0;
	return colorSum;
}