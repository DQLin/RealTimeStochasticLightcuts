#pragma once
#include "CPUMath.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include "Math/Vector.h"

struct CPUColor4
{
	union
	{
		float c[4];
		struct {
			float r, g, b, a;
		};
	};

	CPUColor4() { r = 0.f; g = 0.f; b = 0.f; a = 0.f; };
	CPUColor4 operator*(float scalar) { return CPUColor4(r*scalar, g*scalar, b*scalar, a*scalar); };
	CPUColor4 operator*(const CPUColor4& other) { return CPUColor4(r * other.r, g * other.g, b * other.b, a * other.a); };
	CPUColor4 operator+(const CPUColor4& other) { return CPUColor4(r + other.r, g + other.g, b + other.b, a + other.a); };
	CPUColor4(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {};
};

class CPUColor : public glm::vec3
{
public:
	// Other functions.
	CPUColor() : glm::vec3(0.0) {};
	CPUColor(float val) : glm::vec3(val) {};
	CPUColor(float r, float g, float b) : glm::vec3(r, g, b) {};
	CPUColor(const CPUColor &c) : CPUColor(c.r, c.g, c.b) {};
	CPUColor(const glm::vec3 &c) : CPUColor(c.r, c.g, c.b) {};
	CPUColor(const CPUColor4& c) : CPUColor(c.r, c.g, c.b) {};
	CPUColor(const aiColor3D& c) : CPUColor(c.r, c.g, c.b) {};

	Math::Vector3 GetVector3() const { return Math::Vector3(r, g, b); }

	CPUColor4 GetColor4() { return CPUColor4(r, g, b, 1); };

	CPUColor &clamp(float low = 0.0f, float high = 1.0f)
	{
		if (r > high) r = high;
		else if (r < low) r = low;
		if (g > high) g = high;
		else if (g < low) g = low;
		if (b > high) b = high;
		else if (b < low) b = low;
		return (*this);
	}

	CPUColor &nclamp(float low = 0.0f, float high = 1.0f)
	{
		if (r == NAN) r = 0;
		if (g == NAN) g = 0;
		if (b == NAN) b = 0;
		if (r > high) r = high;
		else if (r < low) r = low;
		if (g > high) g = high;
		else if (g < low) g = low;
		if (b > high) b = high;
		else if (b < low) b = low;
		return (*this);
	}


	CPUColor &gammaCorrect(float gamma = 2.2f)
	{
		float power = 1.0f / gamma;
		r = pow(r, power);
		g = pow(g, power);
		b = pow(b, power);
		return (*this);
	}

	bool isSaturate()
	{
		return r >= 1.0 && g >= 1.0 && b >= 1.0;
	}

	float lumi()
	{
		return fmaxf(r, fmaxf(g, b));
	}

	float value()
	{
		//return 0.299f*r + 0.587f*g + 0.114f*b;
		//return 0.212671f * r + 0.715160f * g + 0.072169f * b;
		return fmaxf(r, fmaxf(g, b));
	}

	bool isZero()
	{
		return r == 0.0 && g == 0.0 && b == 0.0;
	}

	bool isNonPositive()
	{
		return r <= 0.0 || g <= 0.0 || b <= 0.0;
	}

	bool isNAN()
	{
		return r != r || g != g || b != b;
	}

	bool isINF()
	{
		return isinf(r) || isinf(g) || isinf(b);
	}

	bool isNEG()
	{
		return r < 0.f || g < 0.f || b < 0.f;
	}

	CPUColor expDecay()
	{
		return CPUColor(expf(-r), expf(-g), expf(-b));
	}
};

