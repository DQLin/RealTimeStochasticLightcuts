#pragma once
#include "VectorMath.h"
#include "GpuBuffer.h"

using namespace Math;

class Quad
{
public:
	Quad() {};
	~Quad() {};

	void Init()
	{
		uint32_t m_VertexStride = 3 * sizeof(float);
		indicesPerInstance = 4;
		__declspec(align(16)) float vertices[] = {
			-1.0f,  1.0f, 0.0f,
			-1.0f, -1.0f, 0.0f,
			1.0f,  1.0f, 0.0f,
			1.0f, -1.0f, 0.0f
		};
		__declspec(align(16)) unsigned int indices[] = { 0,1,2,3 };

		m_VertexBuffer.Create(L"QuadVertexBuffer", sizeof(vertices) / m_VertexStride, m_VertexStride, vertices);
		m_IndexBuffer.Create(L"QuadIndexBuffer", sizeof(indices) / sizeof(unsigned int), sizeof(unsigned int), indices);
	}

	StructuredBuffer m_VertexBuffer;
	ByteAddressBuffer m_IndexBuffer;
	unsigned indicesPerInstance;

};
