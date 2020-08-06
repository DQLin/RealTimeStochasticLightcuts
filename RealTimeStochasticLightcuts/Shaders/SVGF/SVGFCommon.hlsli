// Taken from the NVIDIA SVGF sample code

// computes a 3x3 gaussian blur of the variance, centered around
// the current pixel

#define DOUBLE_INPUT

inline float luminance(float3 rgb)
{
	return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float2 computeVarianceCenter(int2 center, Texture2D texInS)
{
	float2 sum = float2(0.0, 0.0);

	const float kernel[2][2] = {
		{ 1.0 / 4.0, 1.0 / 8.0  },
		{ 1.0 / 8.0, 1.0 / 16.0 }
	};

	const int radius = 1;
	for (int yy = -radius; yy <= radius; yy++)
	{
		for (int xx = -radius; xx <= radius; xx++)
		{
			int2 p = center + int2(xx, yy);

			float k = kernel[abs(xx)][abs(yy)];

			sum.r += texInS[p].a * k;
		}
	}

	return sum;
}

float2 computeVarianceCenter(int2 center, Texture2D texInS, Texture2D texInU)
{
	float2 sum = float2(0.0, 0.0);

	const float kernel[2][2] = {
		{ 1.0 / 4.0, 1.0 / 8.0  },
		{ 1.0 / 8.0, 1.0 / 16.0 }
	};

	const int radius = 1;
	for (int yy = -radius; yy <= radius; yy++)
	{
		for (int xx = -radius; xx <= radius; xx++)
		{
			int2 p = center + int2(xx, yy);

			float k = kernel[abs(xx)][abs(yy)];

			sum.r += texInS[p].a * k;
			sum.g += texInU[p].a * k;
		}
	}

	return sum;
}