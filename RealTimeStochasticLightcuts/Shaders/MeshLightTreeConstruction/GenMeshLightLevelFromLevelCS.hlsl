// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "DefaultBlockSize.hlsli"
#include "../LightTreeUtilities.hlsli"

cbuffer CSConstants : register(b0)
{
	int srcLevel;
	int dstLevel;
	int numDstLevelLights;
	int numLevels;
};

RWStructuredBuffer<Node> nodes : register(u0);

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int levelNodeId = DTid.x;
	if (levelNodeId < numDstLevelLights)
	{
		int dstNodeArr = (1 << (numLevels - dstLevel - 1)) + levelNodeId;
		int startNodeId = dstNodeArr << (dstLevel - srcLevel);
		int endNodeId = startNodeId + (1 << (dstLevel - srcLevel));

		Node node = nodes[startNodeId];

		for (int nodeId = startNodeId + 1; nodeId < endNodeId; nodeId++)
		{
			Node srcNode = nodes[nodeId];
			if (srcNode.intensity > 0) //actual light
			{
				node.intensity += srcNode.intensity;
				node.boundMin = min(srcNode.boundMin, node.boundMin);
				node.boundMax = max(srcNode.boundMax, node.boundMax);
			}

#ifdef LIGHT_CONE
			if (srcNode.intensity > 0) //actual light
			{
				node.cone = MergeCones(node.cone, srcNode.cone);
			}
#endif
		}

		nodes[dstNodeArr] = node;
	}
}

