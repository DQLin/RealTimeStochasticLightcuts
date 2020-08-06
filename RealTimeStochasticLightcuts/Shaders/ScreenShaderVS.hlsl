float4 main( float3 position : POSITION ) : SV_POSITION
{
	return float4(position.xy, 0, 1);
}