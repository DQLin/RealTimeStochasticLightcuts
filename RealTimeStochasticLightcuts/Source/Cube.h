#pragma once
#include "VectorMath.h"
#include "GpuBuffer.h"

using namespace Math;

class Cube
{
public:
	Cube() {};
	~Cube() {};

	void Init()
	{
		uint32_t m_VertexStride = 16;
		indicesPerInstance = 14;
		lineStripIndicesPerInstance = 16;
		float vertices[] = {
			-1,-1,1,1,
			1,-1,1,1,
			-1,-1,-1,1,
			1,-1,-1,1,
			-1,1,1,1,
			1,1,1,1,
			-1,1,-1,1,
			1,1,-1,1
		};
		__declspec(align(16)) unsigned int indices[] = { 3,2,7,6,4,2,0,3,1,7,5,4,1,0 };
		__declspec(align(16)) unsigned int indicesLineStrip[] = { 0,1,3,7,6,2,3,7,5,4,6,2,0,4,5,1 };

		m_VertexBuffer.Create(L"CubeVertexBuffer", sizeof(vertices) / m_VertexStride, m_VertexStride, vertices);
		m_IndexBuffer.Create(L"CubeIndexBuffer", sizeof(indices) / sizeof(unsigned int), sizeof(unsigned int), indices);
		m_LineStripIndexBuffer.Create(L"CubeLineStripIndexBuffer", sizeof(indicesLineStrip) / sizeof(unsigned int), sizeof(unsigned int), indicesLineStrip);
	}

	StructuredBuffer m_VertexBuffer;
	ByteAddressBuffer m_IndexBuffer;
	ByteAddressBuffer m_LineStripIndexBuffer;
	unsigned indicesPerInstance;
	unsigned lineStripIndicesPerInstance;
};
