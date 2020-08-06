// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "DefaultBlockSize.hlsli"
#include "../LightTreeUtilities.hlsli"

RWStructuredBuffer<VizNode> vizNodes : register(u0);
StructuredBuffer<Node> nodes : register(t0);
StructuredBuffer<BLASInstanceHeader> BLASHeaders : register(t1);
#ifdef CPU_BUILDER
StructuredBuffer<int> LevelIds : register(t2);
#endif
StructuredBuffer<uint2> nodeBLASId : register(t3);

cbuffer CSConstants : register(b0)
{
	int numNodes;
	int isBLASInTwoLevel;
	int needLevelIds; //true when CPU_BUILDER is enabled
};

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x < numNodes)
	{
		VizNode vn;

		int nodeID;

		if (isBLASInTwoLevel)
		{
			int BLASInstanceId = nodeBLASId[DTid.x].x;
			nodeID = nodeBLASId[DTid.x].y;
			BLASInstanceHeader header = BLASHeaders[BLASInstanceId];
			vn.index = BLASInstanceId;

			if (!needLevelIds)
			{
				int relID = nodeID - header.nodeOffset;
				vn.level = uintLog2(relID);
				if (relID == 0)
				{
					vn.level = -1;
					vn.boundMin = 0;
					vn.boundMax = 0;
				}
			}
		}
		else
		{
			nodeID = DTid.x;
			vn.index = -1;
			if (!needLevelIds) vn.level = uintLog2(DTid.x);
		}

		if (needLevelIds)
		{
#ifdef CPU_BUILDER
			vn.level = LevelIds[nodeID];
#endif
		}

		Node node = nodes[nodeID];
		if (node.intensity == 0)
		{
			vn.boundMin = 0;
			vn.boundMax = 0;
			vn.level = -1;
			vn.index = -1;
		}
		else
		{
			vn.boundMin = node.boundMin;
			vn.boundMax = node.boundMax;
		}

		vizNodes[DTid.x] = vn;
	}
}