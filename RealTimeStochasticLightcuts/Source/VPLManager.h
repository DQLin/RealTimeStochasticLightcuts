#pragma once
#include "CommandContext.h"
#include "RTXHelper.h"
#include "ModelLoader.h"
#include "BufferManager.h"
#include "LightRayGen.h"
#include "LightHit.h"
#include "ShadowHit.h"
#include "VPLConstants.h"

using Microsoft::WRL::ComPtr;

enum RaytracingTypes
{
	LightTracing = 0,
	SLCSampling,
	RayTraceReflection,
	NumRaytracingTypes
};

__declspec(align(16)) struct LightTracingConstants
{
	Math::Vector3 sunDirection;
	Math::Vector3 sunLight;
	Math::Vector4 sceneSphere;
	int DispatchOffset;
	int maxDepth;
	int frameId;
};


__declspec(align(16)) struct IRTracingConstants
{
	unsigned int currentVPL;
	float invNumPaths;
	float sceneRadius;
};

__declspec(align(16)) struct LGHShadowTracingConstants
{
	Vector4 halton[4];
	float invNumPaths;
	int shadowRate;
	int numLevels;
	int minLevel;
	float baseRadius;
	float devScale;
	float alpha;
	int temporalRandom;
	int frameId;
	float sceneRadius;
};


class VPLManager
{
public:

	VPLManager() {};
	//change this if you want to generate more than 10M vpls

	UINT numVPLs, numPaths;
	std::vector<StructuredBuffer> VPLBuffers;
	unsigned maxRayRecursion;
	int numFramesUpdated;
	Vector4 sceneBoundingSphere;

	void Initialize(Model1* _model, int numModels, int _maxUpdateFrames = 1, int _maxRayRecursion = 30);
	bool GenerateVPLs(GraphicsContext & context, float VPLEmissionLevel, const Vector3& lightDirection, float lightIntensity, int frameId, int maxDepth = 3, bool hasRegenRequest=false);

	void InitializeGBufferSrvs();

	void SetRayTracingDescriptorHeaps(ID3D12GraphicsCommandList * pCommandList);

	void DispatchRays(RaytracingTypes raytracingType, int dispatchWidth, int dispatchHeight);

	//
	void UpdateAccelerationStructure();

	void BindRootSignatureAndScene(ID3D12GraphicsCommandList * pCommandList);

	void BindGBuffer(ID3D12GraphicsCommandList* pCommandList);

	std::unique_ptr<DescriptorHeapStack> m_pRaytracingDescriptorHeap;

	D3D12_GPU_DESCRIPTOR_HANDLE m_GpuSceneEmissiveTextureSrvs;

private:

	enum VPLAttributes
	{
		POSITION = 0,
		NORMAL,
		COLOR
	};

	void BuildAccelerationStructures();
	void InitializeViews(const Model1& model);
	void InitializeSceneInfo();
	void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC & desc, ComPtr<ID3D12RootSignature>* rootSig);
	void InitializeRaytracingRootSignatures();
	void InitializeRaytracingStateObjects();
	void InitializeRaytracingShaderTable();
	void MergeBoundingSpheres(Vector4& base, Vector4 in);


	// Root signatures
	ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
	ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;

	ByteAddressBuffer          m_lightConstantBuffer;

	//scene related
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> m_GpuSceneMaterialSrvs;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SceneMeshInfo;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_SceneIndices;

	//AS
	std::vector<ComPtr<ID3D12Resource>>   m_bvh_bottomLevelAccelerationStructures;
	ComPtr<ID3D12Resource>   m_bvh_topLevelAccelerationStructure;
	WRAPPED_GPU_POINTER m_bvh_topLevelAccelerationStructurePointer;
	ID3D12Resource* pInstanceDataBuffer;
	ByteAddressBuffer scratchBuffer;
	std::vector<UINT> BLASDescriptorIndex;

	//// Hardware DXR
	ComPtr<ID3D12Device5> m_dxrDevice;
	ComPtr<ID3D12GraphicsCommandList5> m_dxrCommandList;
	std::vector<ComPtr<ID3D12StateObject>> m_dxrStateObjects;

	Model1* m_Models;
	int numModels;
	int numMeshTotal;

	// shared
	D3D12_GPU_DESCRIPTOR_HANDLE m_VPLUavs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SceneSrvs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_GBufferSrvs;

	// for LGH shadow tracing
	D3D12_GPU_DESCRIPTOR_HANDLE m_LGHSamplingSrvs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SUUavs;
	D3D12_CPU_DESCRIPTOR_HANDLE m_PositionDescriptorHeapHandle, m_NormalDescriptorHeapHandle; //for switching between interleaved and normal
	D3D12_CPU_DESCRIPTOR_HANDLE m_LGHDescriptorHeapHandle[4];

	// for IR shadow tracing
	D3D12_GPU_DESCRIPTOR_HANDLE m_LGHSrvs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_BlueNoiseSrvs;
	ColorBuffer* IRResultBuffer;
	D3D12_GPU_DESCRIPTOR_HANDLE m_IRResultUav;


	RaytracingDispatchRayInputs m_RaytracingInputs[NumRaytracingTypes];
	StructuredBuffer    m_hitShaderMeshInfoBuffer;

	int maxUpdateFrames;

	Vector3 lastLightDirection;
	float lastLightIntensity;

	bool Use16BitIndex;
};