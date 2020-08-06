#define groupDim_x 1024

struct SceneBound
{
	float4 corner;
	float4 dimension;
};

StructuredBuffer<float4> g_data : register(t0);
RWStructuredBuffer<float4> g_odata : register(u0);
RWStructuredBuffer<SceneBound> SceneBoundBuffer : register(u1);

cbuffer consts {
	uint n;
	uint isLastPass;
};

groupshared float3 sdata[groupDim_x];

[numthreads(groupDim_x, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
	int tid = GTid.x;
	int i = Gid.x * 2 * groupDim_x + tid;

	sdata[tid] = (i + groupDim_x >= n) ? g_data[i] : min(g_data[i], g_data[i + groupDim_x]);

	int numGroupActives = n - 2 * groupDim_x * Gid.x;
	if (numGroupActives >= 1024) numGroupActives = 1024;

	GroupMemoryBarrierWithGroupSync();

	if (numGroupActives > 512) { if (tid < numGroupActives - 512) { sdata[tid] = min(sdata[tid], sdata[tid + 512]); } numGroupActives = 512; GroupMemoryBarrierWithGroupSync(); }
	if (numGroupActives > 256) { if (tid < numGroupActives - 256) { sdata[tid] = min(sdata[tid], sdata[tid + 256]); } numGroupActives = 256; GroupMemoryBarrierWithGroupSync(); }
	if (numGroupActives > 128) { if (tid < numGroupActives - 128) { sdata[tid] = min(sdata[tid], sdata[tid + 128]); } numGroupActives = 128; GroupMemoryBarrierWithGroupSync(); }
	if (numGroupActives > 64) { if (tid < numGroupActives - 64) { sdata[tid] = min(sdata[tid], sdata[tid + 64]); } numGroupActives = 64; GroupMemoryBarrierWithGroupSync(); }
	if (tid < 32) {
		if (numGroupActives > 32) { if (tid < numGroupActives - 32) { sdata[tid] = min(sdata[tid], sdata[tid + 32]); } numGroupActives = 32; }
		if (numGroupActives > 16) { if (tid < numGroupActives - 16) { sdata[tid] = min(sdata[tid], sdata[tid + 16]); } numGroupActives = 16; }
		if (numGroupActives > 8) { if (tid < numGroupActives - 8) { sdata[tid] = min(sdata[tid], sdata[tid + 8]); } numGroupActives = 8; }
		if (numGroupActives > 4) { if (tid < numGroupActives - 4) { sdata[tid] = min(sdata[tid], sdata[tid + 4]); } numGroupActives = 4; }
		if (numGroupActives > 2) { if (tid < numGroupActives - 2) { sdata[tid] = min(sdata[tid], sdata[tid + 2]); } numGroupActives = 2; }
		if (numGroupActives > 1) { if (tid < numGroupActives - 1) { sdata[tid] = min(sdata[tid], sdata[tid + 1]); } numGroupActives = 1; }
	}

	if (tid == 0)
	{
		float4 output = float4(sdata[0], 0);
		if (isLastPass)
		{
			SceneBoundBuffer[0].corner = output;
			SceneBoundBuffer[0].dimension -= output;
			SceneBoundBuffer[0].dimension.w = length(SceneBoundBuffer[0].dimension.xyz);
		}
		else g_odata[Gid.x] = output;
	}
}