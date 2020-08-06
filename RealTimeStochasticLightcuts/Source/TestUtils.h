#pragma once
#include "GpuBuffer.h"
#include "CommandContext.h"
#include "ReadbackBuffer.h"

extern BoolVar m_EnableNodeViz;

class TestUtils
{
public:
	template <typename T>
	static std::vector<T> ReadBackCPUVector(ComputeContext& cptContext, GpuBuffer& gpuBuffer, int numElements)
	{
		ReadbackBuffer ReadbackList;
		ReadbackList.Create(L"readbacklist", numElements, sizeof(T));
		cptContext.CopyBuffer(ReadbackList, gpuBuffer);
		cptContext.Flush(true);
		void* BufferPtr = ReadbackList.Map();
		std::vector<T> Buffer(numElements);
		memcpy(Buffer.data(), BufferPtr, numElements * sizeof(T));
		return Buffer;
	}

	template <typename T>
	static std::vector<T> ReadBackCPUVectorPartial(ComputeContext& cptContext, GpuBuffer& gpuBuffer, int srcOffset, int numElements)
	{
		ReadbackBuffer ReadbackList;
		ReadbackList.Create(L"readbacklist", numElements, sizeof(T));
		cptContext.TransitionResource(gpuBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		cptContext.TransitionResource(ReadbackList, D3D12_RESOURCE_STATE_COPY_DEST);
		cptContext.CopyBufferRegion(ReadbackList, 0, gpuBuffer, srcOffset, numElements * sizeof(T));
		cptContext.Flush(true);
		void* BufferPtr = ReadbackList.Map();
		std::vector<T> Buffer(numElements);
		memcpy(Buffer.data(), BufferPtr, numElements * sizeof(T));
		return Buffer;
	}

	template <typename T>
	static std::vector<T> ReadBackCPUVector(GraphicsContext& gfxContext, GpuBuffer& gpuBuffer, int numElements)
	{
		ReadbackBuffer ReadbackList;
		ReadbackList.Create(L"readbacklist", numElements, sizeof(T));
		gfxContext.CopyBuffer(ReadbackList, gpuBuffer);
		gfxContext.Flush(true);
		void* BufferPtr = ReadbackList.Map();
		std::vector<T> Buffer(numElements);
		memcpy(Buffer.data(), BufferPtr, numElements * sizeof(T));
		return Buffer;
	}


	template <typename T>
	static std::vector<T> ReadBackTexture1D(GraphicsContext& gfxContext, ColorBuffer& texture, int numElements)
	{
		ReadbackBuffer ReadbackList;
		ReadbackList.Create(L"readbacklist", numElements, sizeof(T));
		gfxContext.TransitionResource(texture, D3D12_RESOURCE_STATE_COPY_SOURCE);
		gfxContext.TransitionResource(ReadbackList, D3D12_RESOURCE_STATE_COPY_DEST);
		gfxContext.ReadbackTexture2D(ReadbackList, texture);
		gfxContext.Flush(true);
		void* BufferPtr = ReadbackList.Map();
		std::vector<T> Buffer(numElements);
		memcpy(Buffer.data(), BufferPtr, numElements * sizeof(T));
		return Buffer;
	}

	template <typename T>
	static std::vector<T> ReadBackTexture2D(GraphicsContext& gfxContext, ColorBuffer& texture, int numElements)
	{
		ReadbackBuffer ReadbackList;
		ReadbackList.Create(L"readbacklist", numElements, sizeof(T));
		gfxContext.TransitionResource(texture, D3D12_RESOURCE_STATE_COPY_SOURCE);
		gfxContext.TransitionResource(ReadbackList, D3D12_RESOURCE_STATE_COPY_DEST);
		gfxContext.ReadbackTexture2D(ReadbackList, texture);
		gfxContext.Flush(true);
		void* BufferPtr = ReadbackList.Map();
		std::vector<T> Buffer(numElements);
		memcpy(Buffer.data(), BufferPtr, numElements * sizeof(T));
		return Buffer;
	}

	template <typename T>
	static inline void VerifySort(T* List, uint32_t ListLength, bool bAscending)
	{
		const T IndexMask = Math::AlignPowerOfTwo(ListLength) - 1;

		for (uint32_t i = 0; i < ListLength - 1; ++i)
		{
			for (int j = 0; j <= 63; j++)
			{
				printf("%d", List[i] >> 63 - j & 1);
			}
			printf("\n");

			ASSERT((List[i] & IndexMask) < ListLength, "Corrupted list index detected");

			if (bAscending)
			{
				ASSERT((List[i] & ~IndexMask) <= (List[i + 1] & ~IndexMask), "Invalid sort order:  non-ascending");
			}
			else
			{
				ASSERT((List[i] & ~IndexMask) >= (List[i + 1] & ~IndexMask), "Invalid sort order:  non-descending");
			}
		}

		ASSERT((List[ListLength - 1] & IndexMask) < ListLength, "Corrupted list index detected");
	}
};

