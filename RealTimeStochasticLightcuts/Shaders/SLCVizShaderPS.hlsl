// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

struct PSInput
{
	float4 position : SV_POSITION;
	nointerpolation int level : LEVEL;
};

cbuffer PSConstants : register(b0)
{
	int showLevel;
}

// color coding
static const float levelColor[32][3] = {
{1,0.0,0.0},
{1,0.375,0.0},
{1,0.75,0.0},
{0.875,1,0.0},
{0.5,1,0.0},
{0.125,1,0.0},
{0.0,1,0.25},
{0.0,1,0.625},
{0.0,1.0,1},
{0.0,0.625,1},
{0.0,0.25,1},
{0.125,0.0,1},
{0.5,0.0,1},
{0.875,0.0,1},
{1,0.0,0.75},
{1,0.0,0.375},
{1,0.0,0.0},
{1,0.0625,0.0625},
{1,0.125,0.125},
{1,0.1875,0.1875},
{1,0.25,0.25},
{1,0.3125,0.3125},
{1,0.375,0.375},
{1,0.4375,0.4375},
{1,0.5,0.5},
{1,0.5625,0.5625},
{1,0.625,0.625},
{1,0.6875,0.6875},
{1,0.75,0.75},
{1,0.8125,0.8125},
{1,0.875,0.875},
{1,0.9375,0.9375}
};

float4 main(PSInput input) : SV_TARGET0
{
	int level = input.level;
	if (level == -1 || (showLevel != -1 && showLevel != level)) discard;
	return level >= 32 ? 1 : float4(levelColor[level], 1);
}