#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <algorithm>
#define PI 3.141592654f

typedef std::default_random_engine sampler;
extern std::uniform_real_distribution<float> distribution;

inline float clamp(float val, float lower, float upper)
{
	return val < lower ? lower : val > upper ? upper : val;
}

inline float saturate(float val)
{
	return clamp(val, 0.f, 1.f);
}

inline float mod(float x, float y)
{
	return x - y * floor(x / y);
}

inline float Halton(int index, int base)
{
	float r = 0;
	float f = 1.0f / (float)base;
	for (int i = index; i > 0; i /= base) {
		r += f * (i%base);
		f /= (float)base;
	}
	return r;
}

inline float getUniform1D(sampler& state)
{
	return distribution(state);
}

inline glm::vec2 getUniform2D(sampler& state)
{
	return glm::vec2(distribution(state), distribution(state));
}

inline glm::vec3 getUniform3D(sampler& state)
{
	return glm::vec3(distribution(state), distribution(state), distribution(state));
}

// this function is from PBRT-V3
inline void CoordinateSystem(const glm::vec3 &v1, glm::vec3 *v2, glm::vec3 *v3) {
	if (std::abs(v1.x) > std::abs(v1.y))
		*v2 = glm::vec3(-v1.z, 0, v1.x) / std::sqrt(v1.x * v1.x + v1.z * v1.z);
	else
		*v2 = glm::vec3(0, v1.z, -v1.y) / std::sqrt(v1.y * v1.y + v1.z * v1.z);
	*v3 = cross(v1, *v2);
}

inline glm::vec2 getUnitDiskSample(sampler& state)
{
	float r1 = getUniform1D(state);
	float r2 = getUniform1D(state);
	float R = sqrt(r1);
	float theta = 2 * PI * r2;

	glm::vec2 p = glm::vec2(R*cos(theta), R*sin(theta));
	return p;
}

inline glm::vec3 getTriangleSample(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, sampler& state)
{
	float e1 = getUniform1D(state);
	float e2 = getUniform1D(state);
	float beta = e2 * sqrt(1 - e1);
	float gamma = 1 - sqrt(1 - e1);

	glm::vec3 A = v0;
	glm::vec3 B = v1;
	glm::vec3 C = v2;
	glm::vec3 p = A + beta * (B - A) + gamma * (C - A);
	return p;
}

inline glm::vec3 getHemisphereSample(const glm::vec3 &N, sampler& state)
{
	glm::vec3 u = normalize(cross(fabs(N.x) > 0.1 ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), N));
	glm::vec3 v = cross(N, u);
	float phi = 2 * PI * getUniform1D(state); // pick random number on unit circle (radius = 1, circumference = 2*Pi) for azimuth
	float z = getUniform1D(state);  // pick random number for elevation [0,1]
	float r = sqrt(std::max(1 - z * z, 0.f));

	glm::vec3 d = u * cos(phi)*r + v * sin(phi)*r + N * z;
	return normalize(d);
}

inline float signed_sqrt(float val)
{
	if (val < 0) return -sqrt(val);
	else return sqrt(val);
}

inline glm::vec3 getSphereSample(const glm::vec3 &N, sampler& state)
{
	glm::vec3 u = normalize(cross(fabs(N.x) > 0.1 ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), N));
	glm::vec3 v = cross(N, u);
	float phi = 2 * PI * getUniform1D(state); // pick random number on unit circle (radius = 1, circumference = 2*Pi) for azimuth
	float z = 1 - 2 * getUniform1D(state);  // pick random number for elevation [-1,1]
	float r = sqrt(std::max(1 - z * z, 0.f));

	glm::vec3 d = u * cos(phi)*r + v * sin(phi)*r + N * z;
	return normalize(d);
}

inline glm::vec3 WorldToLocal(const glm::vec3 &v, const glm::vec3 &X, const glm::vec3 &Y, const glm::vec3 &Z)
{
	return glm::vec3(dot(v, X), dot(v, Y), dot(v, Z));
}
inline glm::vec3 LocalToWorld(const glm::vec3 &v, const glm::vec3 &X, const glm::vec3 &Y, const glm::vec3 &Z)
{
	return glm::vec3(X.x * v.x + Y.x * v.y + Z.x * v.z,
		X.y * v.x + Y.y * v.y + Z.y * v.z,
		X.z * v.x + Y.z * v.y + Z.z * v.z);
}

inline unsigned BitExpansion(unsigned x)
{
	x = (x | x << 16) & 0x30000ff;
	x = (x | x << 8) & 0x300f00f;
	x = (x | x << 4) & 0x30c30c3;
	x = (x | x << 2) & 0x9249249;
	return x;
};
