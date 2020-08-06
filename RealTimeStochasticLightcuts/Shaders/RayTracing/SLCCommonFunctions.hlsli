// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

inline float errorFunction(int nodeID, int BLASId, float3 p, float3 N, float3 V,
	StructuredBuffer<Node> TLAS,
	StructuredBuffer<Node> BLAS,
	StructuredBuffer<BLASInstanceHeader> g_BLASHeaders, int TLASLeafStartIndex
)
{
	bool IsBLASInTwoLevelTree = false;

	Node node;
	int c0 = nodeID < 0 ? BLASId : nodeID;

	if (nodeID < 0) // for one level tree
	{
#ifndef CPU_BUILDER
		if (BLASId >= TLASLeafStartIndex)
		{
			return 0;
		}
#endif
		node = BLAS[BLASId];
#ifdef CPU_BUILDER
		if (node.ID >= TLASLeafStartIndex)
		{
			return 0;
		}
#endif

	}
	else
	{
		if (BLASId >= 0)
		{
			IsBLASInTwoLevelTree = true;
			BLASInstanceHeader header = g_BLASHeaders[BLASId];

#ifndef CPU_BUILDER
			if (nodeID >= header.numTreeLeafs)
			{
				return 0;
			}
#endif
			node = BLAS[header.nodeOffset + nodeID];
#ifdef CPU_BUILDER
			if (node.ID >= 2 * header.numTreeLeafs)
			{
				return 0;
			}
#endif

		}
		else
		{
			node = TLAS[nodeID];
		}
	}

	float dlen2 = SquaredDistanceToClosestPoint(p, node.boundMin, node.boundMax);
	float SR2 = errorLimit * sceneLightBoundRadius;
	SR2 *= SR2;
	if (dlen2 < SR2) dlen2 = SR2; // bound the distance

	float atten = rcp(dlen2);

	atten *= GeomTermBound(p, N, node.boundMin, node.boundMax);

#ifdef LIGHT_CONE
	{
		float3 nr_boundMin = 2 * p - node.boundMax;
		float3 nr_boundMax = 2 * p - node.boundMin;
		float cos0 = GeomTermBound(p, node.cone.xyz, nr_boundMin, nr_boundMax);
		atten *= max(0.f, cos(max(0.f, acos(cos0) - node.cone.w)));
	}
#endif

	float colorIntens = node.intensity;

	float res = atten * colorIntens;
	return res;
};

inline void TwoLevelTreeGetChildrenInfo(inout int p_TLASNodeID, inout int p_BLASNodeID, inout int s_TLASNodeID, inout int s_BLASNodeID,
	inout int pChild, inout int sChild, inout int BLASOffset, inout int BLASId,
	StructuredBuffer<Node> TLAS,
	StructuredBuffer<Node> BLAS,
	StructuredBuffer<BLASInstanceHeader> g_BLASHeaders,
	int TLASLeafStartIndex,
	bool swapChildren,
	int sampledTLASNodeID,
	int sampledBLASNodeID
)
{
	if (p_BLASNodeID < 0)
	{
#ifdef CPU_BUILDER
		if (TLAS[p_TLASNodeID].ID < TLASLeafStartIndex)
#else
		if (p_TLASNodeID < TLASLeafStartIndex)
#endif
		{
#ifdef CPU_BUILDER
			pChild = TLAS[p_TLASNodeID].ID;
#else
			pChild = p_TLASNodeID << 1;
#endif
			sChild = pChild + 1;

			if (swapChildren)
			{
#ifdef CPU_BUILDER
				int sChildPrimaryChildID = TLAS[sChild].ID;
				if (sChildPrimaryChildID >= TLASLeafStartIndex)
				{
					if (sampledTLASNodeID == sChild) Swap(pChild, sChild);
				}
				else
				{
					if (sampledTLASNodeID >= sChildPrimaryChildID) Swap(pChild, sChild);
				}
#else
				int curTopDownLevel = uintLog2(pChild); //start from 0
				int sampledLevel = uintLog2(sampledTLASNodeID);
				int sChildLeafStart = sChild << (sampledLevel - curTopDownLevel);
				if (sampledTLASNodeID >= sChildLeafStart) Swap(pChild, sChild);
#endif
			}
			///
			p_TLASNodeID = pChild;
			s_TLASNodeID = sChild;

			// bogus light
			if (TLAS[s_TLASNodeID].intensity == 0) sChild = -1;
		}
		else
		{
			if (gUseMeshLight)
			{
#ifdef CPU_BUILDER
				BLASId = TLAS[p_TLASNodeID].ID - TLASLeafStartIndex;
#else
				BLASId = TLAS[p_TLASNodeID].ID;
#endif
			}
			// sample BLAS
			BLASOffset = g_BLASHeaders[BLASId].nodeOffset;
			pChild = 2;
			sChild = 3;

			if (swapChildren)
			{
#ifdef CPU_BUILDER
				int BLASLeafStartIndex = 2 * g_BLASHeaders[BLASId].numTreeLeafs;
				int sChildPrimaryChildID = BLAS[BLASOffset + sChild].ID;

				if (sChildPrimaryChildID >= BLASLeafStartIndex)
				{
					if (sampledBLASNodeID == sChild) Swap(pChild, sChild);
				}
				else
				{
					if (sampledBLASNodeID >= sChildPrimaryChildID) Swap(pChild, sChild);
				}
#else
				int curTopDownLevel = 1; //start from 0
				int sampledLevel = uintLog2(sampledBLASNodeID);
				int sChildLeafStart = sChild << (sampledLevel - curTopDownLevel);
				if (sampledBLASNodeID >= sChildLeafStart) Swap(pChild, sChild);
#endif
			}
			///
			s_TLASNodeID = BLASId;
			p_TLASNodeID = BLASId;
			s_BLASNodeID = sChild;
			p_BLASNodeID = pChild;

			// bogus light
			if (BLAS[BLASOffset + s_BLASNodeID].intensity == 0) sChild = -1;
		}
	}
	else
	{
		BLASId = p_TLASNodeID;
		BLASOffset = g_BLASHeaders[BLASId].nodeOffset;
#ifdef CPU_BUILDER
		pChild = BLAS[BLASOffset + p_BLASNodeID].ID;
#else
		pChild = p_BLASNodeID << 1;
#endif
		sChild = pChild + 1;

		if (swapChildren)
		{
#ifdef CPU_BUILDER
			int BLASLeafStartIndex = 2 * g_BLASHeaders[BLASId].numTreeLeafs;
			int sChildPrimaryChildID = BLAS[BLASOffset + sChild].ID;
			if (sChildPrimaryChildID >= BLASLeafStartIndex)
			{
				if (sampledBLASNodeID == sChild) Swap(pChild, sChild);
			}
			else
			{
				if (sampledBLASNodeID >= sChildPrimaryChildID) Swap(pChild, sChild);
			}
#else
			int curTopDownLevel = uintLog2(pChild); //start from 0
			int sampledLevel = uintLog2(sampledBLASNodeID);
			int sChildLeafStart = sChild << (sampledLevel - curTopDownLevel);
			if (sampledBLASNodeID >= sChildLeafStart) Swap(pChild, sChild);
#endif
		}
		///
		p_BLASNodeID = pChild;
		s_BLASNodeID = sChild;

		if (BLAS[BLASOffset + s_BLASNodeID].intensity == 0) sChild = -1;
	}
}

