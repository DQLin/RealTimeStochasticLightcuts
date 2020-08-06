Texture2D<float> LinearDepth : register(t32);

float main(float4 screenPos : SV_POSITION) : SV_TARGET0
{
    float depth = LinearDepth[int2(screenPos.xy)];
	return max(abs(ddx(depth)), abs(ddy(depth)));
}