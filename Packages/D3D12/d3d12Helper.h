///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// D3D12RaytracingFallback.h                                                 //
//                                                                           //
// Provides a simplified interface for the DX12 Ray Tracing interface that   //
// will use native DX12 ray tracing when available. For drivers that do not  //
// support ray tracing, a fallback compute-shader based solution will be     //
// used instead.                                                             //    
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#pragma once
#include "d3d12_1.h"

struct EMULATED_GPU_POINTER
{
    UINT32 OffsetInBytes;
    UINT32 DescriptorHeapIndex;
};

struct WRAPPED_GPU_POINTER
{
    union
    {
        EMULATED_GPU_POINTER EmulatedGpuPtr;
        D3D12_GPU_VIRTUAL_ADDRESS GpuVA;
    };

    WRAPPED_GPU_POINTER operator+(UINT64 offset)
    {
        WRAPPED_GPU_POINTER pointer = *this;
        pointer.GpuVA += offset;
        return pointer;
    }
};
