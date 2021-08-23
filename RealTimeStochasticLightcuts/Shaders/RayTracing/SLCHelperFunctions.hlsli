// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "SLCCommonFunctions.hlsli"

bool firstChildWeight(float3 p, float3 N, float3 V, inout float prob0, int child0, int child1, int BLASOffset)
{
	Node c0;
	Node c1;
	if (BLASOffset >= 0)
	{
		c0 = BLAS[BLASOffset + child0];
		c1 = BLAS[BLASOffset + child1];
	}
	else
	{
		c0 = TLAS[child0];
		c1 = TLAS[child1];
	}

	float c0_intensity = c0.intensity;
	float c1_intensity = c1.intensity;

	if (c0_intensity == 0)
	{
		if (c1_intensity == 0) return false;
		prob0 = 0;
		return true;
	}
	else if (c1_intensity == 0)
	{
		prob0 = 1;
		return true;
	}

	float3 c0_boundMin = c0.boundMin;
	float3 c0_boundMax = c0.boundMax;
	float3 c1_boundMin = c1.boundMin;
	float3 c1_boundMax = c1.boundMax;

	// Compute the weights
	
	float geom0 = 1;
	float geom1 = 1;
	if (useApproximateCosineBound)
	{
		geom0 = GeomTermBoundApproximate(p, N, c0_boundMin, c0_boundMax);
		geom1 = GeomTermBoundApproximate(p, N, c1_boundMin, c1_boundMax);
	}
	else
	{
		geom0 = GeomTermBound(p, N, c0_boundMin, c0_boundMax);
		geom1 = GeomTermBound(p, N, c1_boundMin, c1_boundMax);
	}

#ifdef LIGHT_CONE
	float3 c0r_boundMin = 2 * p - c0_boundMax;
	float3 c0r_boundMax = 2 * p - c0_boundMin;
	float3 c1r_boundMin = 2 * p - c1_boundMax;
	float3 c1r_boundMax = 2 * p - c1_boundMin;

	float cos0 = 1;
	float cos1 = 1;

	if (useApproximateCosineBound)
	{
		cos0 = GeomTermBoundApproximate(p, c0.cone.xyz, c0r_boundMin, c0r_boundMax);
		cos1 = GeomTermBoundApproximate(p, c1.cone.xyz, c1r_boundMin, c1r_boundMax);
	}
	else
	{
		cos0 = GeomTermBound(p, c0.cone.xyz, c0r_boundMin, c0r_boundMax);
		cos1 = GeomTermBound(p, c1.cone.xyz, c1r_boundMin, c1r_boundMax);
	}
	geom0 *= max(0.f, cos(max(0.f, acos(cos0) - c0.cone.w)));
	geom1 *= max(0.f, cos(max(0.f, acos(cos1) - c1.cone.w)));
#endif

	if (geom0 + geom1 == 0) return false;

	if (geom0 == 0)
	{
		prob0 = 0;
		return true;
	}
	else if (geom1 == 0)
	{
		prob0 = 1;
		return true;
	}

	float intensGeom0 = c0_intensity * geom0;
	float intensGeom1 = c1_intensity * geom1;

	float l2_min0;
	float l2_min1;
	l2_min0 = SquaredDistanceToClosestPoint(p, c0_boundMin, c0_boundMax);
	l2_min1 = SquaredDistanceToClosestPoint(p, c1_boundMin, c1_boundMax);

#ifdef EXPLORE_DISTANCE_TYPE
	if (distanceType == 0)
	{
		if (l2_min0 < WidthSquared(c0_boundMin, c0_boundMax) || l2_min1 < WidthSquared(c1_boundMin, c1_boundMax))
		{
			prob0 = intensGeom0 / (intensGeom0 + intensGeom1);
		}
		else
		{
			float w_max0 = normalizedWeights(l2_min0, l2_min1, intensGeom0, intensGeom1);
			prob0 = w_max0;	// closest point
		}
	}
	else if (distanceType == 1)
	{
		float3 l0 = 0.5*(c0_boundMin + c0_boundMax) - p;
		float3 l1 = 0.5*(c1_boundMin + c1_boundMax) - p;
		float w_max0 = normalizedWeights(max(0.001, dot(l0, l0)), max(0.001, dot(l1, l1)), intensGeom0, intensGeom1);
		prob0 = w_max0;	// closest point
	}
	else if (distanceType == 2) //avg weight of minmax (used in the paper)
	{
#endif
		float l2_max0 = SquaredDistanceToFarthestPoint(p, c0_boundMin, c0_boundMax);
		float l2_max1 = SquaredDistanceToFarthestPoint(p, c1_boundMin, c1_boundMax);
		float w_max0 = l2_min0 == 0 && l2_min1 == 0 ? intensGeom0 / (intensGeom0 + intensGeom1) : normalizedWeights(l2_min0, l2_min1, intensGeom0, intensGeom1);
		float w_min0 = normalizedWeights(l2_max0, l2_max1, intensGeom0, intensGeom1);
		prob0 = 0.5 * (w_max0 + w_min0);
#ifdef EXPLORE_DISTANCE_TYPE
	}
#endif
	return true;
};

// return rays for deferred tracing
float3 attenFuncMeshLight(float3 p, float3 N, int indexOffset, int emitTexId, float3 scaling, float3x3 rotation, float3 translation, inout float4 rayDesc, inout RandomSequence rng, bool evaluateRay)
{
	// sample a triangle
	int v0 = g_MeshLightIndexBuffer[indexOffset];
	int v1 = g_MeshLightIndexBuffer[indexOffset + 1];
	int v2 = g_MeshLightIndexBuffer[indexOffset + 2];

	float3 p0 = g_MeshLightVertexBuffer[v0].position;
	float3 p1 = g_MeshLightVertexBuffer[v1].position;
	float3 p2 = g_MeshLightVertexBuffer[v2].position;

	p0 = mul(rotation, scaling * p0) + translation;
	p1 = mul(rotation, scaling * p1) + translation;
	p2 = mul(rotation, scaling * p2) + translation;

	float area = 0.5 * length(cross(p1 - p0, p2 - p0));

	float3 n0 = g_MeshLightVertexBuffer[v0].normal;
	float3 n1 = g_MeshLightVertexBuffer[v1].normal;
	float3 n2 = g_MeshLightVertexBuffer[v2].normal;

	//sample triangle
	float e1 = RandomSequence_GenerateSample1D(rng);
	float e2 = RandomSequence_GenerateSample1D(rng);
	float beta = e2 * sqrt(1 - e1);
	float gamma = 1 - sqrt(1 - e1);

	float3 lp = p0 + beta * (p1 - p0) + gamma * (p2 - p0);

	// shading normal
	float3 Ns = mul(rotation, n0 + beta * (n1 - n0) + gamma * (n2 - n0));

	float3 d = lp - p;
	float dSquared = dot(d, d);
	d *= rsqrt(dSquared); // normalize

	float dDotn = dot(-d, Ns);
	float pdf = dDotn < 0 ? 0 : dSquared / (area * dDotn);

	// cast shadow ray (add bias on two sides)
	float3 rayOrg = p + N * shadowBiasScale;
	d = lp + shadowBiasScale * Ns - rayOrg;
	float tmax = length(d);
	d /= tmax; // normalize
	rayDesc = float4(d, tmax);
	float3 output = 0;

	if (pdf == 0)
	{
		return 0;
	}

	if (evaluateRay)
	{
		RayDesc rd = {
			rayOrg,
			0,
			d,
			tmax
		};

		ShadowRayPayload payload;
		payload.RayHitT = tmax;

		TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
			~0, 0, 1, 0, rd, payload);

		if (payload.RayHitT == tmax)
		{
			output = max(dot(N, d), 0.0) / pdf;
		}
		else
		{
			output = 0;
		}
	}
	else
	{
		output = max(dot(N, d), 0.0) / pdf;
	}

	// sample emissive texture
	if (emitTexId >= 0)
	{
		float2 t0 = g_MeshLightVertexBuffer[v0].texCoord;
		float2 t1 = g_MeshLightVertexBuffer[v1].texCoord;
		float2 t2 = g_MeshLightVertexBuffer[v2].texCoord;
		float2 uv = t0 + beta * (t1 - t0) + gamma * (t2 - t0);
		output *= emissiveTextures[emitTexId].SampleLevel(g_s0, uv, 0).rgb;
	}

	if (any(isnan(output))) output = 0;
	return output;
}

float3 attenFuncVPL(float3 p, float3 N, int lightID, inout float4 rayDesc, bool evaluateRay)
{
	float3 lightPos = g_lightPositions[lightID].xyz;
	float3 lightNormal = g_lightNormals[lightID].xyz;

	float3 lightDir = lightPos + 0.01*lightNormal - p; //VPL surface offset
	float lightDist = length(lightDir);
	lightDir /= lightDist;
	// cast shadow ray
	rayDesc = float4(lightDir, lightDist);
	float3 output = 0;

	if (evaluateRay)
	{
		// cast shadow ray
		RayDesc rd = { p,
							0.001 * sceneRadius,
							lightDir,
							lightDist };

		ShadowRayPayload payload;
		payload.RayHitT = lightDist;

		TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
			~0, 0, 1, 0, rd, payload);

		if (payload.RayHitT != lightDist) // has occlusion
		{
			return 0;
		}
	}

	float bias = 0.01 * sceneRadius;
	bias *= bias;
	float cosineFactor = any(lightNormal) ? max(dot(lightNormal, -lightDir), 0.0) : 1.0; //point light
	output = max(dot(N, lightDir), 0.0) * cosineFactor / (lightDist*lightDist + bias);
	output *= invNumPaths; //stores invNumPaths here

	if (any(isnan(output))) output = 0;

	return output;
}

float3 computeRandomMeshLightSample(float3 p, float3 N, inout RandomSequence rng)
{
	float r = RandomSequence_GenerateSample1D(rng);
	int numLights = numMeshLightTriangles;
	int triID = int(numMeshLightTriangles * r);
	if (triID == numMeshLightTriangles) triID--;
	uint BLASId = g_MeshLightInstancePrimitiveBuffer[triID].instanceId;
	uint indexOffset = g_MeshLightInstancePrimitiveBuffer[triID].indexOffset;
	float3 emission = g_BLASHeaders[BLASId].emission;
	float4 dummy;
	float3 atten = attenFuncMeshLight(p, N, indexOffset, g_BLASHeaders[BLASId].emitTexId, g_BLASHeaders[BLASId].scaling, g_BLASHeaders[BLASId].rotation, g_BLASHeaders[BLASId].translation, dummy, rng, true);
	return emission * atten * numLights;
}

float3 computeRandomVPL(float3 p, float3 N, inout RandomSequence rng)
{
	float r = RandomSequence_GenerateSample1D(rng);
	int numLights = numMeshLightTriangles;
	int lightID = int(numLights * r);
	if (lightID == numLights) lightID--;
	float4 dummy;
	float3 atten = attenFuncVPL(p, N, lightID, dummy, true);
	float3 nodeColor = g_lightColors[lightID].xyz;
	return numLights * nodeColor * atten;
}

inline bool traverseLightTree(inout int nid,
	StructuredBuffer<Node> nodeBuffer,
	int LeafStartIndex,
	int BLASOffset, inout float r, inout double nprob, float3 p, float3 N, float3 V)
{
	bool deadBranch = false;
	while (nid < LeafStartIndex) {
#ifdef CPU_BUILDER
		int c0_id = nodeBuffer[nid + max(0, BLASOffset)].ID;
		if (c0_id >= LeafStartIndex) {
			break;
		}
#else
		int c0_id = nid << 1;
#endif
		int c1_id = c0_id + 1;
		float prob0;

		if (firstChildWeight(p, N, V, prob0, c0_id, c1_id, BLASOffset)) {
			if (r < prob0) {
				nid = c0_id;
				r /= prob0;
				nprob *= prob0;
			}
			else {
				nid = c1_id;
				r = (r - prob0) / (1 - prob0);
				nprob *= (1 - prob0);
			}
		}
		else {
			deadBranch = true;
			break;
		}
	}
	return deadBranch;
}

inline float4 computeNodeOneLevelHelper(float3 p, float3 N, float3 V, out int nid, out float3 color, int nodeID, inout RandomSequence rng, bool evaluateRay)
{
	float r = RandomSequence_GenerateSample1D(rng);
	double nprob = 1;	// probability of picking that node
	nid = nodeID;

	bool deadBranch = traverseLightTree(nid, BLAS, TLASLeafStartIndex, 0, r, nprob, p, N, V);
	float4 rayDesc;

	// for mesh slc this is triangleInstanceId
#ifdef CPU_BUILDER
	int lightIndexOffset = BLAS[nid].ID - TLASLeafStartIndex;
#else
	int lightIndexOffset = BLAS[nid].ID;
#endif

	float3 nodeColor, atten;

	if (gUseMeshLight)
	{
		uint BLASId = g_MeshLightInstancePrimitiveBuffer[lightIndexOffset].instanceId;
		lightIndexOffset = g_MeshLightInstancePrimitiveBuffer[lightIndexOffset].indexOffset;
		nodeColor = deadBranch ? 0 : g_BLASHeaders[BLASId].emission;
		atten = deadBranch ? 0 : attenFuncMeshLight(p, N, lightIndexOffset, g_BLASHeaders[BLASId].emitTexId, g_BLASHeaders[BLASId].scaling, g_BLASHeaders[BLASId].rotation, g_BLASHeaders[BLASId].translation, rayDesc, rng, evaluateRay);
	}
	else
	{
		nodeColor = g_lightColors[lightIndexOffset].xyz;
		atten = deadBranch ? 0 : attenFuncVPL(p, N, lightIndexOffset, rayDesc, evaluateRay);
	}

	double one_over_prob = nprob == 0.0 ? 0.0 : 1.0 / nprob;

	color = one_over_prob * nodeColor * atten;
	return rayDesc;
}

inline float4 computeNodeHelper(float3 p, float3 N, float3 V, inout int sampledTLASNodeId, inout int sampledBLASNodeId, out float error,
	out float3 color, int TLASNodeID, int BLASNodeID, inout RandomSequence rng, bool evaluateRay)
{
	float r = RandomSequence_GenerateSample1D(rng);
	double nprob = 1;	// probability of picking that node
	int nid = TLASNodeID;

	int lightIndexOffset;

	bool deadBranch = false;

	float4 rayDesc;

	if (BLASNodeID < 0) {

		deadBranch = traverseLightTree(nid, TLAS, TLASLeafStartIndex, -1, r, nprob, p, N, V);
		sampledTLASNodeId = nid;
	}

#ifdef CPU_BUILDER
	int BLASId = BLASNodeID < 0 ? TLAS[nid].ID - TLASLeafStartIndex : TLASNodeID; //sampled BLAS node will have TLASNodeID indicating BLASId
#else
	int BLASId = BLASNodeID < 0 ? TLAS[nid].ID : TLASNodeID; //sampled BLAS node will have TLASNodeID indicating BLASId
#endif

	float3 p_transformed = p;
	float3 N_transformed = N;
	float3 V_transformed = V;

	if (!deadBranch)
	{
		// sample BLAS
		const int BLASOffset = g_BLASHeaders[BLASId].nodeOffset;

#ifdef CPU_BUILDER
		const int BLASLeafStartIndex = 2 * g_BLASHeaders[BLASId].numTreeLeafs;
#else
		const int BLASLeafStartIndex = g_BLASHeaders[BLASId].numTreeLeafs;
#endif
		const float3 emission = g_BLASHeaders[BLASId].emission;

		float3x3 rotT = transpose(g_BLASHeaders[BLASId].rotation);
		p_transformed = (1.f / g_BLASHeaders[BLASId].scaling) * mul(rotT, p - g_BLASHeaders[BLASId].translation);
		N_transformed = mul(rotT, N);
		V_transformed = mul(rotT, V);

		nid = BLASNodeID >= 0 ? BLASNodeID : 1;

		deadBranch = traverseLightTree(nid, BLAS, BLASLeafStartIndex, BLASOffset, r, nprob, p_transformed, N_transformed, V_transformed);

		sampledBLASNodeId = nid;

#ifdef CPU_BUILDER
		lightIndexOffset = BLAS[BLASOffset + nid].ID - BLASLeafStartIndex;
#else
		lightIndexOffset = BLAS[BLASOffset + nid].ID;
#endif

		float3 atten = deadBranch ? 0 : attenFuncMeshLight(p, N, lightIndexOffset, g_BLASHeaders[BLASId].emitTexId, g_BLASHeaders[BLASId].scaling, g_BLASHeaders[BLASId].rotation, g_BLASHeaders[BLASId].translation, rayDesc, rng, evaluateRay);
		float3 nodeColor = deadBranch ? 0 : emission;
		double one_over_prob = nprob == 0.0 ? 0.0 : 1.0 / nprob;
		color = one_over_prob * nodeColor * atten;
	}
	else
	{
		color = 0;
	}

	if (evaluateRay)
	{
		if (BLASNodeID < 0)
		{
			error = errorFunction(TLASNodeID, -1, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
			);
		}
		else
		{
			error = errorFunction(BLASNodeID, BLASId, p_transformed, N_transformed, V_transformed, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
			);
		}
	}

	return rayDesc;
}

