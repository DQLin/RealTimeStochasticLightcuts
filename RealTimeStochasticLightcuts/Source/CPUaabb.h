#ifndef _AABB_H_
#define _AABB_H_

#include "CPUMath.h"
#include <queue>
#include <functional>
#include "GameCore.h"

class GeneralBoundingBox
{
public:
	glm::vec3 pos;
	glm::vec3 vec_u, vec_v, vec_w;
	GeneralBoundingBox() {};
	GeneralBoundingBox(const glm::vec3& pos, const glm::vec3& vec_u, const glm::vec3& vec_v, const glm::vec3& vec_w) : pos(pos), vec_u(vec_u), vec_v(vec_v), vec_w(vec_w) {};

	GeneralBoundingBox TransformToLocalCoordinates(const glm::vec3& p, const glm::vec3& T, const glm::vec3& B, const glm::vec3& N) const
	{
		glm::vec3 pos_local = pos - p;
		pos_local = WorldToLocal(pos_local, T, B, N);

		glm::vec3 vec_u_new = WorldToLocal(vec_u, T, B, N);
		glm::vec3 vec_v_new = WorldToLocal(vec_v, T, B, N);
		glm::vec3 vec_w_new = WorldToLocal(vec_w, T, B, N);
		return GeneralBoundingBox(pos_local, vec_u_new, vec_v_new, vec_w_new);
	}

	glm::vec3 operator[](int i) const
	{
		glm::vec3 ret = pos;
		if (i % 2) ret += vec_u;
		if ((i % 4) / 2) ret += vec_v;
		if (i / 4) ret += vec_w;
		return ret;
	}
};

class aabb
{
public:
	glm::vec3 pos, end;

	aabb() : pos(glm::vec3(FLT_MAX, FLT_MAX, FLT_MAX)), end(glm::vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX)) {};
	aabb(const glm::vec3& in_pos) : pos(in_pos), end(in_pos) {}; // single point bounding box
	aabb(const glm::vec3& in_pos, const glm::vec3& in_end) : pos(in_pos), end(in_end) {};
	aabb(Math::Vector3& in_pos) { pos.x = (float)in_pos.GetX(); pos.y = (float)in_pos.GetY(); pos.z = (float)in_pos.GetZ();
	end.x = pos.x; end.y = pos.y; end.z = pos.z; };

	glm::vec3 operator[](int i) const
	{
		return glm::vec3((i % 2) ? pos.x : end.x,
			((i % 4) / 2) ? pos.y : end.y,
			(i / 4) ? pos.z : end.z);
	}

	glm::vec3 dimension() const
	{
		return end - pos;
	}

	float SA() //surface area
	{
		return 2 * (w() * d() + w() * h() + d() * h());
	}

	bool Intersect(aabb& other)
	{
		return ((end.x >= other.pos.x) && (pos.x <= other.end.x) && // x-axis overlap
			(end.y >= other.pos.y) && (pos.y <= other.end.y) && // y-axis overlap
			(end.z >= other.pos.z) && (pos.z <= other.end.z));   // z-axis overlap
	}

	bool Intersection(aabb& other, aabb& inter)
	{
		if (!Intersect(other)) return false;
		inter = aabb(glm::vec3(std::max(pos.x, other.pos.x), std::max(pos.y, other.pos.y), std::max(pos.z, other.pos.z)),
			glm::vec3(std::min(end.x, other.end.x), std::min(end.y, other.end.y), std::min(end.z, other.end.z)));
		return true;
	}

	bool Contains(glm::vec3 in)
	{
		glm::vec3 v1 = pos, v2 = end;
		return ((in.x >= v1.x) && (in.x <= v2.x) &&
			(in.y >= v1.y) && (in.y <= v2.y) &&
			(in.z >= v1.z) && (in.z <= v2.z));
	}

	bool Contains(aabb& other)
	{
		return (other.pos.x >= pos.x && other.pos.y >= pos.y && other.pos.z >= pos.z) && (end.x >= other.end.x && end.y >= other.end.y && end.z >= other.end.z);
	}


	bool Borders(aabb& other, int taxis) //border in axis
	{
		for (int i = 0; i < 3; i++) //zyx
			if ((taxis >> i) | 1)
				if (pos[i] == other.pos[i] || end[i] == other.end[i]) return true;
		return false;
	}

	aabb expand(float rate)
	{
		float half = rate / 2.0;
		glm::vec3 oldsize = dimension();
		pos = pos - half * oldsize;
		end = pos + (1 + rate)*oldsize;
		return (*this);
	}

	void Union(glm::vec3 point)
	{
		for (int i = 0; i < 3; i++)
		{
			if (point[i] < pos[i])  pos[i] = point[i];
			if (point[i] > end[i])  end[i] = point[i];
		}
	}

	aabb& Union(aabb& other)
	{
		for (int i = 0; i < 3; i++)
		{
			if (other.pos[i] < pos[i])  pos[i] = other.pos[i];
			if (other.end[i] > end[i])  end[i] = other.end[i];
		}
		return (*this);
	}

	void Refit(aabb& child1, aabb& child2)
	{
		pos = child1.pos;
		end = child1.end;
		Union(child2);
	}

	void Translate(glm::vec3& dir)
	{
		pos += dir;
		end += dir;
	}

	void Update(glm::vec3 newpos, glm::vec3 newend)
	{
		pos = newpos;
		end = newend;
	}

	bool isSingular()
	{
		int count = 0;
		for (int i = 0; i < 3; i++)
		{
			if ((end - pos)[i] == 0.f) count++;
		}
		if (count >= 2) return true;
		else return false;
	}

	GeneralBoundingBox TransformToLocalCoordinates(const glm::vec3& p, const glm::vec3& T, const glm::vec3& B, const glm::vec3& N) const
	{
		glm::vec3 pos_local = pos - p;
		pos_local = WorldToLocal(pos_local, T, B, N);
		glm::vec3 vec_u = w() * glm::vec3(T.x, B.x, N.x);
		glm::vec3 vec_v = h() * glm::vec3(T.y, B.y, N.y);
		glm::vec3 vec_w = d() * glm::vec3(T.z, B.z, N.z);
		return GeneralBoundingBox(pos_local, vec_u, vec_v, vec_w);
	}

	float w() const { return (end - pos).x; }
	float h() const { return (end - pos).y; }
	float d() const { return (end - pos).z; }
	float x() const { return pos.x; }
	float y() const { return pos.y; }
	float z() const { return pos.z; }

	glm::vec3 centroid() const { return (pos + end) / 2.f; }
	float diagonal_length() { return sqrt(w()*w() + h()*h() + d()*d()); }

	float WidthSquared() const { return w()*w() + h()*h() + d()*d(); } //squared_diagonal_length for cyLightCuts

	int MaxDim()
	{
		glm::vec3 dims = dimension();

		if (dims.x > dims.y && dims.x > dims.z) return 0;
		else if (dims.y > dims.z) return 1;
		else return 2;
	}
};



#endif
