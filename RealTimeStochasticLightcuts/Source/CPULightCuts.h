// Copyright (c) 2020, Cem Yuksel <cem@cemyuksel.com>
// All rights reserved.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files (the "Software"), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all 
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.
// 

// Author: Cem Yuksel, Daqi Lin

#pragma once
#include <vector>
#include "CPUColor.h"
#include "CPUaabb.h"
#include "cyPointCloud.h"
#include "LightTreeMacros.h"
//-------------------------------------------------------------------------------

#define LIGHTCUTS_STOCHASTIC // great idea! When evaluating, randomly picks a light from a subtree as the representative. Needs reordering the tree.

//-------------------------------------------------------------------------------

#ifdef LIGHTCUTS_STOCHASTIC

# define LIGHTCUTS_HIERARCHICAL 0.0001f // instead of picking a random light, traverses the hierarchy by randomly picking child nodes
#else

# define LIGHTCUTS_REP_COUNT 32	// multiple representative lights (as suggested by multidimensional lightcuts)

#endif

//-------------------------------------------------------------------------------

#define LIGHTCUTS_MIN_INTENSITY 0.000001f
#define LIGHTCUTS_BIGFLOAT 1e30f

//-------------------------------------------------------------------------------

class LightCuts
{
public:

	enum class LightType
	{
		POINT,
		REAL
	};

	LightType lightType;
	
	float globalBoundDiag;

	struct Node
	{
#ifdef LIGHTCUTS_STOCHASTIC
		float probStart;	// the total intensity of the nodes up to this node
		float probTree;		// the total intensity within this subtree
#endif

		int    primaryChild;	// primary child must have the same position. If negative, no child
		int    secondaryChild;
		int    lightID;
		CPUColor  color;
		aabb   boundBox;

#ifdef LIGHT_CONE
		glm::vec4   boundingCone;
#endif
#ifdef LIGHTCUTS_REP_COUNT
		std::vector<int>   nodeLights;
		std::vector<float> nodeLightCDF;
#endif
	};

	void SetLightType(LightType lightType) { this->lightType = lightType; }

	template <typename LightColorFunc, typename LightPosFunc, typename LightConeFunc, typename BoundingBoxFunc, typename RandFunc>
	void Build(int numLights, LightColorFunc lightColorFunc, LightPosFunc lightPosFunc, LightConeFunc lightConeFunc, BoundingBoxFunc boundingBoxFunc, RandFunc randFunc)
	{
		// Initialize the light cut data
		nodes.clear();
		nodes.resize(numLights * 2 - 1);
		for (int i = 0; i < numLights; i++) {
			glm::vec3 p = lightPosFunc(i);
			CPUColor c = lightColorFunc(i);
			nodes[i + numLights - 1].lightID = i;
			nodes[i + numLights - 1].color = c;
			nodes[i + numLights - 1].boundBox = boundingBoxFunc(i);

			nodes[i + numLights - 1].primaryChild = -1;
			nodes[i + numLights - 1].secondaryChild = -1;
#ifdef LIGHT_CONE
			nodes[i + numLights - 1].boundingCone = lightConeFunc(i);
#endif
#ifdef LIGHTCUTS_STOCHASTIC
			nodes[i + numLights - 1].probTree = SumVal(c);
			nodes[i + numLights - 1].probStart = nodes[i + numLights - 1].probTree; // temporarily storing the light intensity here
#endif
		}
		// Create a point cloud of light positions
		cy::PointCloud<glm::vec3, float, 3, int> pointCloud;
		pointCloud.BuildWithFunc(numLights, lightPosFunc);

		float globalBoundDiag2 = 0.0f;
		globalBoundDiag = 0.f;
		if (numLights > 0) {
			aabb gbound;
			for (int i = 0; i < numLights; ++i) {
				aabb bbox = boundingBoxFunc(i);
				glm::vec3 p = lightPosFunc(i);
				gbound.Union(bbox);
			}
			globalBoundDiag = gbound.diagonal_length();
			globalBoundDiag2 = globalBoundDiag * globalBoundDiag;
		}

		// Create an array of closest light id and its distance
		struct ClosestLight
		{
			int   id;
			float weight;
			float dist;
		};
		std::vector<ClosestLight> closestLights;
		closestLights.resize(numLights);

		// For each light, search the point cloud and find the closest light
		float searchRadius = LIGHTCUTS_BIGFLOAT;
		for (int i = 0; i < numLights; i++) {
			int closestLightID = -1;
			float distanceSquaredToClosestLight = LIGHTCUTS_BIGFLOAT;

			for (int searchIter = 0; distanceSquaredToClosestLight == LIGHTCUTS_BIGFLOAT && searchIter < 100; searchIter++, searchRadius *= 2) {
				pointCloud.GetPoints(lightPosFunc(i), searchRadius,
					[&](int lightID, const glm::vec3 &pos, float distanceSquared, float &radiusSquared)
					{
						if (lightID != i) {
							if (distanceSquared < distanceSquaredToClosestLight) {
								closestLightID = lightID;
								distanceSquaredToClosestLight = distanceSquared;
								radiusSquared = distanceSquared; // This says "do not send me lights that are further away."
							}
						}
					}
				);
			}
			float dist = sqrtf(distanceSquaredToClosestLight);

			searchRadius = dist * 2;
			if (searchRadius == 0.f) searchRadius = 10.f;

			// The closest light is found, we must compute the weight
			float intensity0 = SumVal(nodes[i + numLights - 1].color);
			float intensity1 = SumVal(nodes[closestLightID + numLights - 1].color);
			float intensity = intensity0 + intensity1;
#ifdef LIGHT_CONE
			glm::vec4 boundingCone = MergeCones(nodes[i + numLights - 1].boundingCone, nodes[closestLightID + numLights - 1].boundingCone);
			float coneAngleWeight = 1.0f - cosf(boundingCone.w);
			distanceSquaredToClosestLight += coneAngleWeight * coneAngleWeight * globalBoundDiag2;
#endif
			float weight = distanceSquaredToClosestLight * intensity;
			closestLights[i].id = closestLightID;
			closestLights[i].weight = weight;
			closestLights[i].dist = dist;
		}

		class BuilderHeap
		{
		public:
			struct Data
			{
				int lightID;
				int closestLightID;
				float weight;
				float closestLightDist;
			};
			BuilderHeap(std::vector<ClosestLight> &closestLights, int N)
			{
				heap.resize(N + 1);
				for (int i = 1; i <= N; i++) {
					heap[i].lightID = i - 1;
					heap[i].closestLightID = closestLights[i - 1].id;
					heap[i].weight = closestLights[i - 1].weight;
					heap[i].closestLightDist = closestLights[i - 1].dist;
				}
				if (N <= 1) return;
				for (int i = N / 2; i > 0; i--) MoveDown(i, N);
			}
			void MoveHeadDown() { MoveDown(1, (int)heap.size() - 1); }

			Data& Head() { return heap[1]; }
		private:
			std::vector<Data> heap;
			void SwapItems(int ix1, int ix2) { Data tmp = heap[ix1]; heap[ix1] = heap[ix2]; heap[ix2] = tmp; }
			void MoveDown(int ix, int N)
			{
				int child = ix * 2;
				while (child + 1 <= N) {
					if (heap[child + 1].weight < heap[child].weight) child++;
					if (heap[ix].weight <= heap[child].weight) return;
					SwapItems(ix, child);
					ix = child;
					child = ix * 2;
				}
				if (child <= N) {
					if (heap[child].weight < heap[ix].weight) {
						SwapItems(ix, child);
					}
				}
			}
		};

		// Build a heap for the closest light distances, so we can quickly find the closest pair
		BuilderHeap heap(closestLights, numLights);

		// Create an array of light indices
		std::vector<int> nodeIndex;
		nodeIndex.resize(numLights);
		for (int i = 0; i < numLights; i++) nodeIndex[i] = i + numLights - 1;

		// Tree rebuild
		int pointCloudSize = 0;
		int nextPointCloudBuild = numLights > 8 ? numLights / 2 : -1;
		std::vector<glm::vec3> rebuildPos;
		std::vector<int> rebuildIndex;

		// Take the elements from the heap one by one
		int nextNodeIndex = numLights - 2;
		while (nextNodeIndex >= 0) {
			// Check if the light has already been used
			int thisLightID = heap.Head().lightID;
			if (nodeIndex[thisLightID] < 0) {
				// The light has already been used, skip
				heap.Head().weight = LIGHTCUTS_BIGFLOAT;
				heap.MoveHeadDown();
			}
			else {
				// Check if its pair has already been used
				int closestLightID = heap.Head().closestLightID;
				if (nodeIndex[closestLightID] < 0) {
					// The closest one has already been used,
					// we must find another one
					closestLightID = -1;
					float distanceSquaredToClosestLight = LIGHTCUTS_BIGFLOAT;
					float searchRadius = heap.Head().closestLightDist * 2;
					if (searchRadius == 0.f) searchRadius = 0.1f;
					for (int searchIter = 0; distanceSquaredToClosestLight == LIGHTCUTS_BIGFLOAT && searchIter < 100; searchIter++, searchRadius *= 2) {
						pointCloud.GetPoints(
							lightPosFunc(thisLightID),
							searchRadius,
							[&](int lightID, const glm::vec3 &pos, float distanceSquared, float &radiusSquared)
							{
								if (lightID != thisLightID) {
									if (distanceSquared < distanceSquaredToClosestLight) {
										// Check if the light was removed
										if (nodeIndex[lightID] < 0) return;
										closestLightID = lightID;
										distanceSquaredToClosestLight = distanceSquared;
										radiusSquared = distanceSquared; // This says "do not send me lights that are further away."
									}
								}
							}
						);
					}
					assert(closestLightID >= 0);
					// The new closest light is found, we must recompute the weight
					Node node0 = nodes[nodeIndex[thisLightID]];
					Node node1 = nodes[nodeIndex[closestLightID]];
					float intensity0 = SumVal(node0.color);
					float intensity1 = SumVal(node1.color);
					float intensity = intensity0 + intensity1;
					aabb  boundBox = NodeBound(node0, node1);
					float diag2 = dot(boundBox.end - boundBox.pos, boundBox.end - boundBox.pos);
#ifdef LIGHT_CONE
					glm::vec4 boundingCone = MergeCones(node0.boundingCone, node1.boundingCone);
					float coneAngleWeight = 1.0f - cosf(boundingCone.w);
					diag2 += coneAngleWeight * coneAngleWeight * globalBoundDiag2;
#endif
					float weight = diag2 * intensity;
					heap.Head().closestLightID = closestLightID;
					heap.Head().weight = weight;
					heap.Head().closestLightDist = sqrtf(distanceSquaredToClosestLight);
					heap.MoveHeadDown();
				}
				else {
					// The light is in the heap, so we can merge with it
					Node node0 = nodes[nodeIndex[thisLightID]];
					Node node1 = nodes[nodeIndex[closestLightID]];
					nodes[nextNodeIndex].color = node0.color + node1.color;
					nodes[nextNodeIndex].boundBox = NodeBound(node0, node1);
#ifdef LIGHT_CONE
					nodes[nextNodeIndex].boundingCone = MergeCones(node0.boundingCone, node1.boundingCone);
#endif
					// pick the position randomly
					float intensity0 = SumVal(node0.color);
					float intensity1 = SumVal(node1.color);
					float intensity = intensity0 + intensity1;
					float r = randFunc() * intensity;

					if (r < intensity0) {
						// pick the first light
						nodes[nextNodeIndex].lightID = node0.lightID;
						nodes[nextNodeIndex].primaryChild = nodeIndex[thisLightID];
						nodes[nextNodeIndex].secondaryChild = nodeIndex[closestLightID];
						nodeIndex[closestLightID] = -1; // removed from consideration
						nodeIndex[thisLightID] = nextNodeIndex;
#ifdef LIGHTCUTS_STOCHASTIC
						nodes[nextNodeIndex].probStart = 0;
#endif
					}
					else {
						// pick the second light
						nodes[nextNodeIndex].lightID = node1.lightID;
						nodes[nextNodeIndex].primaryChild = nodeIndex[closestLightID];
						nodes[nextNodeIndex].secondaryChild = nodeIndex[thisLightID];
						nodeIndex[thisLightID] = -1; // removed from consideration
						nodeIndex[closestLightID] = nextNodeIndex;
#ifdef LIGHTCUTS_STOCHASTIC
						nodes[nextNodeIndex].probStart = 0;
#endif
					}
#ifdef LIGHTCUTS_STOCHASTIC
					nodes[nextNodeIndex].probTree = node0.probTree + node1.probTree;
#endif
					nextNodeIndex--;

					if (nextNodeIndex < nextPointCloudBuild) {
						int j = 0;
						if (pointCloudSize == 0) {
							rebuildPos.resize(numLights / 2 + 2);
							rebuildIndex.resize(numLights / 2 + 2);
							for (int i = 0; i < numLights; i++) {
								// Check if the light was removed
								if (nodeIndex[i] >= 0) {
									rebuildPos[j] = lightPosFunc(i);
									rebuildIndex[j] = i;
									j++;
								}
							}
						}
						else {
							for (int i = 0; i < pointCloudSize; i++) {
								int ix = rebuildIndex[i];
								// Check if the light was removed
								if (nodeIndex[ix] >= 0) {
									rebuildPos[j] = lightPosFunc(ix);
									rebuildIndex[j] = ix;
									j++;
								}
							}
						}
						pointCloudSize = j;
						pointCloud.Build(pointCloudSize, rebuildPos.data(), rebuildIndex.data());
						nextPointCloudBuild /= 2;
						if (nextPointCloudBuild <= 4) nextPointCloudBuild = -1;
					}
				}
			}
		}

#ifdef LIGHTCUTS_STOCHASTIC
		// Reorder
		std::vector<int> oldIndices;
		oldIndices.resize(2 * numLights - 1);
		std::vector<int> stack;
		stack.resize(numLights);
		int stackPos = 0;
		stack[0] = 0;
		int index = 0;
		oldIndices[index++] = 0;
		while (stackPos >= 0) {
			int ix = stack[stackPos--];
			if (nodes[ix].primaryChild >= 0) {	// internal node
				oldIndices[index++] = nodes[ix].primaryChild;
				oldIndices[index++] = nodes[ix].secondaryChild;
				stack[++stackPos] = nodes[ix].secondaryChild;
				stack[++stackPos] = nodes[ix].primaryChild;
			}
		}

		float probStart = 0;
		std::vector<int> newIndices;
		newIndices.resize(2 * numLights - 1);
		{	// order nodes
			std::vector<Node> lightcutOrdered;
			lightcutOrdered.resize(2 * numLights - 1);
			for (int i = 0; i < 2 * numLights - 1; i++) {
				int ix = oldIndices[i];
				newIndices[ix] = i;
				lightcutOrdered[i] = nodes[ix];
				float prob = lightcutOrdered[i].probStart;
				lightcutOrdered[i].probStart = probStart;
				probStart += prob;
			}
			nodes.swap(lightcutOrdered);
		}
		// fix child node indices
		for (int i = 0; i < 2 * numLights - 1; i++) {
			if (nodes[i].primaryChild >= 0) {
				nodes[i].primaryChild = (newIndices[nodes[i].primaryChild] + 1);
				nodes[i].secondaryChild = (newIndices[nodes[i].secondaryChild] + 1);
				assert(nodes[i].secondaryChild == nodes[i].primaryChild + 1);
			}
			else
			{
				nodes[i].primaryChild = 2 * numLights + nodes[i].lightID;
			}
		}
#endif
		InitNodeLights(randFunc);
	}

	static float SquaredDistanceToClosestPoint(const glm::vec3 &p, const aabb &box)
	{
		glm::vec3 d = ClosestPoint(p, box) - p;
		return dot(d, d);
	}

	// Returns the closest point to a box
	static glm::vec3 ClosestPoint(glm::vec3 const &p, aabb const &box)
	{
		glm::vec3 cp;
		cp.x = p.x <= box.pos.x ? box.pos.x : (p.x >= box.end.x ? box.end.x : p.x);
		cp.y = p.y <= box.pos.y ? box.pos.y : (p.y >= box.end.y ? box.end.y : p.y);
		cp.z = p.z <= box.pos.z ? box.pos.z : (p.z >= box.end.z ? box.end.z : p.z);
		return cp;
	}

	// Returns the maximum distance to the box along the given direction
	static float MaxDistAlong(glm::vec3 const &p, glm::vec3 const &dir, aabb const &box)
	{
		float dmax = dot(dir, (box.pos - p));
		for (int i = 1; i < 8; ++i) {
			float d = dot(dir, (box[i] - p));
			if (dmax < d) dmax = d;
		}
		return dmax;
	}
	// Returns the absolute minimum distance to the box along the given direction
	static float AbsMinDistAlong(glm::vec3 const &p, glm::vec3 const &dir, aabb const &box)
	{
		float prd = 1.f;
		float dmin = dot(dir, (box.pos - p));
		prd *= dmin;
		dmin = abs(dmin);
		for (int i = 1; i < 8; ++i) {
			float d = dot(dir, (box[i] - p));
			prd *= d;
			d = abs(d);
			if (dmin > d) dmin = d;
		}
		return prd < 0 ? 0.f : dmin;
	}

	// Geometry term bound (actually cosine term for the shaded point)
	static float GeomTermBound(glm::vec3 const &p, glm::vec3 const &N, aabb const &box)
	{
		float nrm_max = MaxDistAlong(p, N, box);
		if (nrm_max <= 0) return 0.0f;

		glm::vec3 T, B;
		CoordinateSystem(N, &T, &B);
		float y_amin = AbsMinDistAlong(p, T, box);
		float z_amin = AbsMinDistAlong(p, B, box);
		float hyp = sqrtf(y_amin * y_amin + z_amin * z_amin + nrm_max * nrm_max);

		return nrm_max / hyp;
	}

	struct LightHeapData
	{
		int    nodeID;
		float  error;			// temporarily stores the error
		double one_over_prob;	// and then the light probability
		CPUColor  color;
		CPUColor  atten; // light color, including attenuation and visibility
#ifdef LIGHTCUTS_STOCHASTIC
		int    sampledNodeID;
#elif defined(LIGHTCUTS_REP_COUNT)
		int    sampledLightID;
#endif
	};

	template <typename AttenFunction, typename RandFunc>
	CPUColor Eval(const glm::vec3 &p, const glm::vec3 &N, const glm::vec3 &T, const glm::vec3 &B, const glm::vec3 &wo,
		float errorLimit, AttenFunction attenFunc, RandFunc nrandom) const
	{
		LightHeapData heap[101]; //this allows 1000 light samples
		Eval(heap, 101, p, N, T, B, wo, bxdf, errorLimit, attenFunc, nrandom);
		return heap[0].color;
	}

	template <typename HeapDataType, typename AttenFunc, typename RandFunc>
	int Eval(HeapDataType *heap, int heapArraySize, const glm::vec3 &p, const glm::vec3 &N, const glm::vec3 &T, const glm::vec3 &B, const glm::vec3 &wo,
		float errorLimit, AttenFunc attenFunc, RandFunc nrandom) const
	{
		return Eval(heap, heapArraySize, p, N, T, B, wo, bxdf, errorLimit, attenFunc,
			[](const glm::vec3 &p, const glm::vec3 &N, int lightID, const CPUColor &color, const aabb &boundBox) {
				float dlen2 = LightCuts::SquaredDistanceToClosestPoint(p, boundBox);
				if (dlen2 < 1) dlen2 = 1; // bound the distance
				float atten = 1 / dlen2;

				float colorIntens = GetColorIntensity(color);
				return atten * colorIntens;
			}, nrandom);
	}

	// The first element of the heap array is not used.
	template <typename HeapDataType, typename AttenFunc, typename ErrorFunc, typename RandFunc>
	int Eval(HeapDataType *heap, int heapArraySize, const glm::vec3 &p, const glm::vec3 &N, const glm::vec3 &T, const glm::vec3 &B, const glm::vec3 &wo,
		float errorLimit, AttenFunc attenFunc, ErrorFunc errorFunc, RandFunc nrandom) const
	{
		auto errorFunction = [&](const glm::vec3 &p, const glm::vec3 &N, const Node &node)
		{
			if (node.primaryChild < 0) return 0.0f;
			else return errorFunc(p, N, node.lightID, node.color, node.boundBox);
		};

#ifdef LIGHTCUTS_HIERARCHICAL
		auto normalizedWeights = [](float l2_0, float l2_1, float intensGeom0, float intensGeom1)
		{
			float ww0 = l2_1 * intensGeom0;
			float ww1 = l2_0 * intensGeom1;
			return ww0 / (ww0 + ww1);
		};
		auto normalizedWeightsSafe = [&normalizedWeights](float l2_0, float l2_1, float intensGeom0, float intensGeom1)
		{
			if (l2_0 + l2_1 < LIGHTCUTS_HIERARCHICAL) return intensGeom0 / (intensGeom0 + intensGeom1);
			return normalizedWeights(l2_0, l2_1, intensGeom0, intensGeom1);
		};
		auto firstChildWeight = [&](float &prob0, int child0, int child1)
		{
			Node const &c0 = nodes[child0];
			Node const &c1 = nodes[child1];
			// Compute the weights
			float geom0 = LightCuts::GeomTermBound(p, N, c0.boundBox);
			float geom1 = LightCuts::GeomTermBound(p, N, c1.boundBox);

			if (geom0 + geom1 == 0) return false;
			float intensGeom0 = c0.probTree*geom0;
			float intensGeom1 = c1.probTree*geom1;
			float l2_min0 = LightCuts::SquaredDistanceToClosestPoint(p, c0.boundBox);
			float l2_min1 = LightCuts::SquaredDistanceToClosestPoint(p, c1.boundBox);

			if (l2_min0 < c0.boundBox.WidthSquared() || l2_min1 < c1.boundBox.WidthSquared())
			{
				prob0 = intensGeom0 / (intensGeom0 + intensGeom1);
			}
			else
			{
				float w_max0 = normalizedWeights(l2_min0, l2_min1, intensGeom0, intensGeom1);
				prob0 = w_max0;	// closest point
			}
			return true;
		};
#endif

		auto computeNode = [&](HeapDataType &hd, int nodeID)
		{
			int id = nodeID;
#ifdef LIGHTCUTS_STOCHASTIC
			hd.sampledNodeID = nodeID;
# ifdef LIGHTCUTS_HIERARCHICAL
			hd.one_over_prob = 1;
			bool deadBranch = false;
# endif
			if (nodes[nodeID].primaryChild >= 0) {
				float r = nrandom();
# ifdef LIGHTCUTS_HIERARCHICAL
				double nprob = 1;	// probability of picking that node
# else
				r = r * nodes[nodeID].probTree + nodes[nodeID].probStart;
# endif
				int nid = nodeID;

				while (nodes[nid].secondaryChild >= 0) {
					int c0 = nodes[nid].primaryChild;
					int c1 = nodes[nid].secondaryChild;
# ifdef LIGHTCUTS_HIERARCHICAL
					float prob0;
					if (firstChildWeight(prob0, c0, c1)) {
						if (r < prob0) {
							nid = c0;
							r /= prob0;
							nprob *= prob0;
						}
						else {
							nid = c1;
							r = (r - prob0) / (1 - prob0);
							nprob *= (1 - prob0);
						}
					}
					else {
						deadBranch = true;
						break;
					}
# else
					nid = (nodes[c1].probStart <= r) ? c1 : c0;
# endif
				}
				hd.sampledNodeID = nid;
				id = hd.sampledNodeID;
# ifdef LIGHTCUTS_HIERARCHICAL
				hd.one_over_prob = nprob == 0.f ? 0.f : 1.0f / nprob;
# endif
			}
#endif
			int lightID = nodes[id].lightID;
#ifdef LIGHTCUTS_REP_COUNT
			if (nodes[nodeID].nodeLights.size() > 0) {
				float r = nrandom();
				size_t i = 0;
				for (; i < nodes[nodeID].nodeLights.size() - 1; ++i) {
					if (r <= nodes[nodeID].nodeLightCDF[i]) break;
				}
				lightID = nodes[nodeID].nodeLights[i];
			}
			hd.sampledLightID = lightID;
#endif
			hd.nodeID = nodeID;
#ifdef LIGHTCUTS_HIERARCHICAL
			hd.atten = deadBranch ? CPUColor(0, 0, 0) : attenFunc(lightID, hd);
			CPUColor nodeColor = lightType == LightType::POINT ? nodes[id].color : 1.f;
			hd.color = hd.one_over_prob * nodeColor * hd.atten;

#else
			hd.atten = attenFunc(lightID, hd);
			CPUColor nodeColor = lightType == LightType::POINT ? nodes[nodeID].color : 1.f;
			hd.color = nodeColor * hd.atten;
#endif
			hd.error = errorFunction(p, N, nodes[nodeID]);

			return true;
		};

		auto heapSwapItems = [&heap](int parent, int child)
		{
			HeapDataType p = heap[parent];
			heap[parent] = heap[child];
			heap[child] = p;
		};

		auto heapMoveUp = [&heapSwapItems, &heap](int numLights)
		{
			int ix = numLights;
			while (ix >= 2) {
				int parent = ix / 2;
				if (heap[parent].error >= heap[ix].error) break;
				heapSwapItems(parent, ix);
				ix = parent;
			}
		};

		auto heapMoveDown = [&heapSwapItems, &heap](int ix, int numLights)
		{
			int child = ix * 2;
			while (child + 1 <= numLights) {
				if (heap[child].error < heap[child + 1].error) child++;
				if (heap[ix].error >= heap[child].error) return;
				heapSwapItems(ix, child);
				ix = child;
				child = ix * 2;
			}
			if (child <= numLights) {
				if (heap[ix].error < heap[child].error) {
					heapSwapItems(ix, child);
				}
			}
		};

		int numLights = 0;
		CPUColor color(0, 0, 0);

		if (computeNode(heap[1], 0)) {
			numLights = 1;
			color = heap[1].color;
			float colorIntens = GetColorIntensity(color);

			while (heap[1].error > (errorLimit * colorIntens) && numLights < heapArraySize - 1) {
				int id = 1;
				int nodeID = heap[id].nodeID;
				int pChild = nodes[nodeID].primaryChild;
				int sChild = nodes[nodeID].secondaryChild;
				assert(pChild >= 0);
				color = color - heap[id].color;

#ifdef LIGHTCUTS_STOCHASTIC
				if (heap[id].sampledNodeID >= sChild) Swap(pChild, sChild);
#elif defined(LIGHTCUTS_REP_COUNT)
				if (nodes[sChild].nodeLights.size() > 0) {
					for (int lightID : nodes[sChild].nodeLights) {
						if (lightID == heap[id].sampledLightID) {
							Swap(pChild, sChild);
							break;
						}
					}
				}
				else {
					if (nodes[sChild].lightID == heap[id].sampledLightID) Swap(pChild, sChild);
				}
#endif
#ifdef LIGHTCUTS_HIERARCHICAL
				float prob0;
				bool liveBranch = firstChildWeight(prob0, pChild, sChild);
				assert(liveBranch);	// we should not have a dead node in the heap

				heap[id].one_over_prob *= prob0;
				CPUColor nodeColor = lightType == LightType::POINT ? nodes[heap[id].sampledNodeID].color : 1.f;
				heap[id].color = heap[id].one_over_prob * nodeColor * heap[id].atten;

#else
				CPUColor nodeColor = lightType == LightType::POINT ? nodes[pChild].color : 1.f;
				heap[id].color = nodeColor * heap[id].atten;
#endif
				heap[id].nodeID = pChild;
				heap[id].error = errorFunction(p, N, nodes[pChild]);
				color = color + heap[id].color;
				heapMoveDown(id, numLights);
				HeapDataType &child_hd = heap[numLights + 1];
				if (computeNode(child_hd, sChild)) {
					numLights++;
					color = color + child_hd.color;
					heapMoveUp(numLights);
				}
				colorIntens = GetColorIntensity(color);
			}
		}

		heap[0].nodeID = numLights;
		heap[0].color = color;

#if defined(LIGHTCUTS_STOCHASTIC) && !defined(LIGHTCUTS_HIERARCHICAL)
		heap[0].one_over_prob = nodes[0].probTree;
		for (int i = 1; i <= numLights; i++) {
			heap[i].one_over_prob = nodes[heap[i].nodeID].probTree / nodes[heap[i].sampledNodeID].probTree;
		}
#endif
		return numLights;
	}

	Node const & GetNode(int id) const { return nodes[id]; }

	int GetNumOfNodes() {
		return nodes.size();
	}

private:
	std::vector<Node> nodes;

	static float MaxVal(const CPUColor  &c) { return c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b); }
	static float SumVal(const CPUColor  &c) { return c.r + c.g + c.b; }

	template <typename T> static void Swap(T &a, T &b) { T t = a; a = b; b = t; }

	aabb NodeBound(Node &node0, Node &node1)
	{
		glm::vec3 boundMin = node0.boundBox.pos;
		if (boundMin.x > node1.boundBox.pos.x) boundMin.x = node1.boundBox.pos.x;
		if (boundMin.y > node1.boundBox.pos.y) boundMin.y = node1.boundBox.pos.y;
		if (boundMin.z > node1.boundBox.pos.z) boundMin.z = node1.boundBox.pos.z;
		glm::vec3 boundMax = node0.boundBox.end;
		if (boundMax.x < node1.boundBox.end.x) boundMax.x = node1.boundBox.end.x;
		if (boundMax.y < node1.boundBox.end.y) boundMax.y = node1.boundBox.end.y;
		if (boundMax.z < node1.boundBox.end.z) boundMax.z = node1.boundBox.end.z;
		return aabb(boundMin, boundMax);
	}

	static float GetColorIntensity(const CPUColor &color)
	{
		float intens = SumVal(color) / 3.0f;
		if (intens < LIGHTCUTS_MIN_INTENSITY) intens = LIGHTCUTS_MIN_INTENSITY;
		return intens;
	}

	template <typename RandFunc>
	void InitNodeLights(RandFunc randFunc)
	{
#ifdef LIGHTCUTS_REP_COUNT
		InitNodeLights(0, randFunc);
		// normalize CDFs
		for (Node &node : nodes) {
			if (node.nodeLightCDF.size() > 0) {
				float total = node.nodeLightCDF.back();
				for (float &cdf : node.nodeLightCDF) cdf /= total;
			}
		}
	}
	template <typename RandFunc>
	void InitNodeLights(int nodeID, RandFunc randFunc)
	{
		Node &node = nodes[nodeID];
		if (node.primaryChild >= 0) {
			InitNodeLights(node.primaryChild, randFunc);
			InitNodeLights(node.secondaryChild, randFunc);
			Node const &child1 = nodes[node.primaryChild];
			Node const &child2 = nodes[node.secondaryChild];
			auto addChildLights = [](Node &node, Node const &child, float &probOffset) {
				if (child.nodeLights.size() == 0) {
					node.nodeLights.push_back(child.lightID);
					node.nodeLightCDF.push_back(child.color.r + child.color.g + child.color.b + probOffset);
				}
				else {
					node.nodeLights.reserve(node.nodeLights.size() + child.nodeLights.size());
					node.nodeLightCDF.reserve(node.nodeLightCDF.size() + child.nodeLights.size());
					for (size_t i = 0; i < child.nodeLights.size(); ++i) {
						node.nodeLights.push_back(child.nodeLights[i]);
						node.nodeLightCDF.push_back(child.nodeLightCDF[i] + probOffset);
					}
				}
				probOffset = node.nodeLightCDF.back();
			};
			float prob = 0.0f;
			addChildLights(node, child1, prob);
			addChildLights(node, child2, prob);
			if (node.nodeLights.size() > LIGHTCUTS_REP_COUNT) {
				std::vector<int> lights;
				std::vector<float> cdf;
				for (int i = 0; i < LIGHTCUTS_REP_COUNT; ++i) {
					// pick a random light
					float r = randFunc() * node.nodeLightCDF.back();
					size_t j = 0;
					for (; j < node.nodeLights.size() - 1; ++j) {
						if (r <= node.nodeLightCDF[j]) break;
					}
					// add the selected light
					lights.push_back(node.nodeLights[j]);
					float p = node.nodeLightCDF[j];
					if (j > 0) p -= node.nodeLightCDF[j - 1];
					// remove the selected light
					for (size_t k = j; k < node.nodeLights.size() - 1; ++k) {
						node.nodeLights[k] = node.nodeLights[k + 1];
						node.nodeLightCDF[k] = node.nodeLightCDF[k + 1] - p;
					}
					node.nodeLights.pop_back();
					node.nodeLightCDF.pop_back();
					// add the cdf
					if (cdf.size() > 0) p += cdf.back();
					cdf.push_back(p);
				}
				node.nodeLights.swap(lights);
				node.nodeLightCDF.swap(cdf);
			}
		}
#endif
	}
};

//-------------------------------------------------------------------------------
