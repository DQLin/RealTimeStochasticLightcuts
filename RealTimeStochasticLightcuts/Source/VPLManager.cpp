//NOTE: Some functions for DXR environment intialization are modified from the Microsoft D3D12 Raytracing Samples
#include "VPLManager.h"
#include "ReadbackBuffer.h"
#include "RaytracingHlslCompat.h"
#include "SLCRayGen.h"
#include "RayTraceReflection.h"
#include "CommandContext.h"
#include <D3D12RaytracingHelpers.hpp>
#include <intsafe.h>

void VPLManager::MergeBoundingSpheres(Vector4& base, Vector4 in)
{
	float baseRadius = base.GetW();
	float inRadius = in.GetW();
	Vector3 c1, c2;
	float R, r;

	if (baseRadius >= inRadius)
	{
		c1 = Vector3(base);
		c2 = Vector3(in);
		R = baseRadius;
		r = inRadius;
	}
	else
	{
		c1 = Vector3(in);
		c2 = Vector3(base);
		R = inRadius;
		r = baseRadius;
	}

	Vector3 d = c2 - c1;
	float dMag = sqrt(d.GetX()*d.GetX() + d.GetY()*d.GetY() + d.GetZ()*d.GetZ());
	d = d / dMag;

	float deltaRadius = 0.5f * (std::max(R, dMag + r) - R);
	Vector3 center = c1 + deltaRadius * d;
	float radius = R + deltaRadius;
	base = Vector4(center, radius);
}


void VPLManager::Initialize(Model1* _model, int _numModels, int _maxRayRecursion /*= 30*/)
{
	m_Models = _model;
	numModels = _numModels;

	if (numModels > 0)
		if (m_Models[0].indexSize == 2) Use16BitIndex = true;
		else Use16BitIndex = false;

	sceneBoundingSphere = m_Models[0].m_SceneBoundingSphere;
	for (int modelId = 1; modelId < numModels; modelId++)
		MergeBoundingSpheres(sceneBoundingSphere, m_Models[modelId].m_SceneBoundingSphere);

	numMeshTotal = 0;

	for (int modelId = 0; modelId < numModels; modelId++)
		numMeshTotal += m_Models[modelId].m_Header.meshCount;

	lastLightIntensity = -1;

	ThrowIfFailed(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");

	using namespace Math;

	VPLBuffers.resize(3);
	VPLBuffers[POSITION].Create(L"VPL Position Buffer", MAXIMUM_NUM_VPLS, sizeof(Vector3));
	VPLBuffers[NORMAL].Create(L"VPL Normal Buffer", MAXIMUM_NUM_VPLS, sizeof(Vector3));
	VPLBuffers[COLOR].Create(L"VPL Color Buffer", MAXIMUM_NUM_VPLS, sizeof(Vector3));

	// allocate dedicated descriptor heap for ray tracing
	m_pRaytracingDescriptorHeap = std::unique_ptr<DescriptorHeapStack>(
		new DescriptorHeapStack(*Graphics::g_Device, 65536 * 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0));
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
	HRESULT hr = Graphics::g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

	m_lightConstantBuffer.Create(L"Hit Constant Buffer", 1, sizeof(LightTracingConstants));

	m_LGHDescriptorHeapHandle[0].ptr = 0;
	m_LGHDescriptorHeapHandle[1].ptr = 0;
	m_LGHDescriptorHeapHandle[2].ptr = 0;
	m_LGHDescriptorHeapHandle[3].ptr = 0;


	InitializeSceneInfo();
	InitializeViews(*_model);
	needRegenerateVPLs = true;
	maxRayRecursion = _maxRayRecursion;
	BuildAccelerationStructures();
	InitializeRaytracingRootSignatures();
	InitializeRaytracingStateObjects();
	InitializeRaytracingShaderTable();

	InitializeGBufferSrvs();
}

void VPLManager::UpdateAccelerationStructure()
{
	const UINT numBLASInstances = m_Models[0].m_NumBLASInstancesTotal;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelAccelerationStructureDesc = {};
	topLevelAccelerationStructureDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelAccelerationStructureDesc.Inputs.NumDescs = numBLASInstances;
	topLevelAccelerationStructureDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	topLevelAccelerationStructureDesc.Inputs.pGeometryDescs = nullptr;
	topLevelAccelerationStructureDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;

	pInstanceDataBuffer->Map(0, nullptr, reinterpret_cast<void**>(&instanceDescs));

	int meshIdOffset = 0;

	auto setTransform = [&](auto* instanceDesc, int matrixId)
	{
		glm::mat4 temp = m_Models[0].m_CPUGlobalMatrices[matrixId];

		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
			{
				instanceDesc->Transform[r][c] = temp[c][r];
			}
	};

	for (UINT i = 0; i < numBLASInstances; i++)
	{
		int matrixId = m_Models[0].m_BLASInstanceMatrixId[i];

		D3D12_RAYTRACING_INSTANCE_DESC &instanceDesc = instanceDescs[i];
		setTransform(&instanceDesc, matrixId);
	}
	pInstanceDataBuffer->Unmap(0, 0);

	topLevelAccelerationStructureDesc.Inputs.InstanceDescs = pInstanceDataBuffer->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Create Acceleration Structure");
	ID3D12GraphicsCommandList *pCommandList = gfxContext.GetCommandList();

	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = m_bvh_topLevelAccelerationStructure.Get();
		pCommandList->ResourceBarrier(1, &uavBarrier);
		gfxContext.FlushResourceBarriers();
	}

	topLevelAccelerationStructureDesc.SourceAccelerationStructureData = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.DestAccelerationStructureData = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();

	auto UpdateAccelerationStructure = [&](auto* raytracingCommandList)
	{
		raytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelAccelerationStructureDesc, 0, nullptr);
	};

	m_dxrCommandList = reinterpret_cast<ID3D12GraphicsCommandList5*>(pCommandList);
	UpdateAccelerationStructure(m_dxrCommandList.Get());
	m_bvh_topLevelAccelerationStructurePointer.GpuVA = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();

	gfxContext.Finish(true);
}

void VPLManager::BindRootSignatureAndScene(ID3D12GraphicsCommandList * pCommandList)
{
	pCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());
	pCommandList->SetComputeRootDescriptorTable(0, m_SceneSrvs);
}

void VPLManager::BindGBuffer(ID3D12GraphicsCommandList* pCommandList)
{
	pCommandList->SetComputeRootDescriptorTable(6, m_GBufferSrvs);
}

void VPLManager::BuildAccelerationStructures()
{
	const UINT numBLASes = m_Models[0].m_MeshGroups.size();
	const UINT numBLASInstances = m_Models[0].m_NumBLASInstancesTotal;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelAccelerationStructureDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &topLevelInputs = topLevelAccelerationStructureDesc.Inputs;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.NumDescs = numBLASInstances;
	topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	topLevelInputs.pGeometryDescs = nullptr;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);

	std::vector<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> geometryDescs(numBLASes);


	for (int modelId = 0; modelId < numModels; modelId++)
	{
		int numMeshes = m_Models[modelId].m_Header.meshCount;
		int numMeshGroups = m_Models[modelId].m_MeshGroups.size();
		for (int blasId = 0; blasId < numMeshGroups; blasId++)
		{
			auto& group = m_Models[modelId].m_MeshGroups[blasId];
			int numGeoms = group.size();

			for (int geomId = 0; geomId < numGeoms; geomId++)
			{
				int meshId = group[geomId];
				auto &mesh = m_Models[modelId].m_pMesh[meshId];
				geometryDescs[blasId].push_back(D3D12_RAYTRACING_GEOMETRY_DESC());
				D3D12_RAYTRACING_GEOMETRY_DESC &desc = geometryDescs[blasId].back();
				desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
				D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC &trianglesDesc = desc.Triangles;
				trianglesDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
				trianglesDesc.VertexCount = mesh.vertexCount;
				trianglesDesc.VertexBuffer.StartAddress = m_Models[modelId].m_VertexBuffer.GetGpuVirtualAddress() +
					(mesh.vertexDataByteOffset + mesh.attrib[Model1::attrib_position].offset);
				trianglesDesc.IndexBuffer = m_Models[modelId].m_IndexBuffer.GetGpuVirtualAddress() + mesh.indexDataByteOffset;
				trianglesDesc.VertexBuffer.StrideInBytes = mesh.vertexStride;
				trianglesDesc.IndexCount = mesh.indexCount;
				trianglesDesc.IndexFormat = (Use16BitIndex && modelId == 0) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
				trianglesDesc.Transform3x4 = 0;
				assert(trianglesDesc.IndexCount % 3 == 0);
			}
		}
	}

	UINT64 scratchBufferSizeNeeded = topLevelPrebuildInfo.ScratchDataSizeInBytes;

	std::vector<UINT64> bottomLevelAccelerationStructureSize(numBLASes);
	std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> bottomLevelAccelerationStructureDescs(numBLASes);
	for (UINT i = 0; i < numBLASes; i++)
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC &bottomLevelAccelerationStructureDesc = bottomLevelAccelerationStructureDescs[i];
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &bottomLevelInputs = bottomLevelAccelerationStructureDesc.Inputs;
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.NumDescs = geometryDescs[i].size();
		bottomLevelInputs.pGeometryDescs = geometryDescs[i].data();
		bottomLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelprebuildInfo;
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelprebuildInfo);

		bottomLevelAccelerationStructureSize[i] = bottomLevelprebuildInfo.ResultDataMaxSizeInBytes;
		scratchBufferSizeNeeded = std::max(bottomLevelprebuildInfo.ScratchDataSizeInBytes, scratchBufferSizeNeeded);
	}

	scratchBuffer.Create(L"Acceleration Structure Scratch Buffer", (UINT)scratchBufferSizeNeeded, 1);

	D3D12_RESOURCE_STATES initialResourceState;
	initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

	D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto topLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	Graphics::g_Device->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&topLevelDesc,
		initialResourceState,
		nullptr,
		IID_PPV_ARGS(&m_bvh_topLevelAccelerationStructure));

	topLevelAccelerationStructureDesc.DestAccelerationStructureData = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();

	D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = nullptr;

	auto AllocateUploadBuffer = [&](ID3D12Device* pDevice, UINT64 datasize, ID3D12Resource **ppResource, void** pMappedData, const wchar_t* resourceName = nullptr)
	{
		auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(datasize);
		ThrowIfFailed(pDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(ppResource)));
		if (resourceName)
		{
			(*ppResource)->SetName(resourceName);
		}
		(*ppResource)->Map(0, nullptr, pMappedData);
		memset(*pMappedData, 0, datasize);
	};

	AllocateUploadBuffer(Graphics::g_Device, numBLASInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
		&pInstanceDataBuffer, (void**)&instanceDescs, L"InstanceDescs");

	m_bvh_bottomLevelAccelerationStructures.resize(numBLASes);

	BLASDescriptorIndex.resize(numBLASes);

	auto setTransform = [&](auto* instanceDesc, int globalMatrixId)
	{
		// Identity matrix
		ZeroMemory(instanceDesc->Transform, sizeof(instanceDesc->Transform));
		glm::mat4 temp = m_Models[0].m_CPUGlobalMatrices[globalMatrixId];

		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
			{
				instanceDesc->Transform[r][c] = temp[c][r];
			}
	};
	int meshIdOffset = 0;
	int instanceCouter = 0;
	m_Models[0].m_BLASInstanceMatrixId.clear();
	for (UINT blasId = 0; blasId < numBLASes; blasId++)
	{
		auto &bottomLevelStructure = m_bvh_bottomLevelAccelerationStructures[blasId];

		initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

		auto bottomLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(bottomLevelAccelerationStructureSize[blasId], D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		Graphics::g_Device->CreateCommittedResource(
			&defaultHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&bottomLevelDesc,
			initialResourceState,
			nullptr,
			IID_PPV_ARGS(&bottomLevelStructure));

		bottomLevelAccelerationStructureDescs[blasId].DestAccelerationStructureData = bottomLevelStructure->GetGPUVirtualAddress();
		bottomLevelAccelerationStructureDescs[blasId].ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();
		BLASDescriptorIndex[blasId] = m_pRaytracingDescriptorHeap->AllocateBufferUav(*bottomLevelStructure.Get());


		// fill instance data
		auto& group = m_Models[0].m_MeshGroups[blasId];
		int numGeoms = group.size();
		auto& mesh = m_Models[0].m_pMesh[group[0]];
		int numInstances = numGeoms == 1 ? mesh.instanceCount : 1;
		
		for (int instId = 0; instId < numInstances; instId++)
		{
			int matrixId = m_Models[0].m_CPUInstanceBuffer[mesh.instanceListOffset + instId];
			m_Models[0].m_BLASInstanceMatrixId.push_back(matrixId);

			D3D12_RAYTRACING_INSTANCE_DESC &instanceDesc = instanceDescs[instanceCouter];
			setTransform(&instanceDesc, matrixId);
			instanceDesc.AccelerationStructure = m_bvh_bottomLevelAccelerationStructures[blasId]->GetGPUVirtualAddress();
			instanceDesc.Flags = 0;
			instanceDesc.InstanceID = matrixId;
			instanceDesc.InstanceMask = m_Models[0].m_pMaterialIsTransparent[mesh.materialIndex] ? 0 : 1;
			instanceDesc.InstanceContributionToHitGroupIndex = meshIdOffset;
			instanceCouter++;
		}
		meshIdOffset += numGeoms;
	}
	
	assert(instanceCouter == numBLASInstances);

	pInstanceDataBuffer->Unmap(0, 0);

	topLevelInputs.InstanceDescs = pInstanceDataBuffer->GetGPUVirtualAddress();
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Build Acceleration Structures");
	ID3D12GraphicsCommandList *pCommandList = gfxContext.GetCommandList();

	auto BuildAccelerationStructure = [&](auto* raytracingCommandList)
	{
		for (UINT i = 0; i < bottomLevelAccelerationStructureDescs.size(); i++)
		{
			raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelAccelerationStructureDescs[i], 0, nullptr);
			pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_bvh_bottomLevelAccelerationStructures[i].Get()));
		}
		raytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelAccelerationStructureDesc, 0, nullptr);
	};

	// Build acceleration structure.
	m_dxrCommandList = reinterpret_cast<ID3D12GraphicsCommandList5*>(pCommandList);
	BuildAccelerationStructure(m_dxrCommandList.Get());
	m_bvh_topLevelAccelerationStructurePointer.GpuVA = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();

	gfxContext.Finish(true);
}

void VPLManager::InitializeGBufferSrvs()
{
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
	UINT srvDescriptorIndex;
	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_ScenePositionBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_GBufferSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_SceneNormalBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_SceneAlbedoBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_SceneSpecularBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void VPLManager::InitializeViews(const Model1 & model)
{
	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
	UINT uavDescriptorIndex;
	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, VPLBuffers[0].GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_VPLUavs = m_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, VPLBuffers[1].GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, VPLBuffers[2].GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
	UINT srvDescriptorIndex;
	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_SceneMeshInfo, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_SceneSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

	UINT unused;

	for (int modelId = 0; modelId < numModels; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_SceneIndices[modelId], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	//fill empty
	for (int modelId = numModels; modelId < 2; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
	}

	for (int modelId = 0; modelId < numModels; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateBufferSrv(*const_cast<ID3D12Resource*>(m_Models[modelId].m_VertexBuffer.GetResource()));
	}
	//fill empty
	for (int modelId = numModels; modelId < 2; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
	}

	for (int modelId = 0; modelId < numModels; modelId++)
		for (UINT i = 0; i < m_Models[modelId].m_Header.materialCount; i++)
		{
			UINT slot;
			m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, slot);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, *m_Models[modelId].GetSRVs(i), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_Models[modelId].GetSRVs(i)[2], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_GpuSceneMaterialSrvs.push_back(m_pRaytracingDescriptorHeap->GetGpuHandle(slot));
		}

	m_GpuSceneEmissiveTextureSrvs.ptr = 0;
	// emissive textures (now only support one model)
	for (int modelId = 0; modelId < numModels; modelId++)
		for (int texId = 0; texId < m_Models[modelId].m_emissiveSRVs.size(); texId++)
		{
			UINT slot;
			m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, slot);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_Models[modelId].m_emissiveSRVs[texId], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			if (texId == 0 && modelId == 0) m_GpuSceneEmissiveTextureSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(slot);
		}
}

void VPLManager::InitializeSceneInfo()
{
	//
	// Mesh info (rearrange meshes to the shader table order)
	//
	int meshIdOffset = 0;
	std::vector<RayTraceMeshInfo>  meshInfoData;

	for (int modelId = 0; modelId < numModels; modelId++)
	{
		Model1& model = m_Models[modelId];
		int numMeshes = model.m_Header.meshCount;

		for (UINT blasId = 0; blasId < m_Models[modelId].m_MeshGroups.size(); blasId++)
		{
			auto& group = m_Models[modelId].m_MeshGroups[blasId];
			int numGoems = group.size();

			for (UINT i = 0; i < numGoems; i++)
			{
				auto& mesh = model.m_pMesh[group[i]];
				RayTraceMeshInfo meshInfo;
				meshInfo.m_indexOffsetBytes = mesh.indexDataByteOffset;
				meshInfo.m_uvAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[Model1::attrib_texcoord0].offset;
				meshInfo.m_normalAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[Model1::attrib_normal].offset;
				meshInfo.m_positionAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[Model1::attrib_position].offset;
				meshInfo.m_tangentAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[Model1::attrib_tangent].offset;
				meshInfo.m_bitangentAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[Model1::attrib_bitangent].offset;
				meshInfo.m_attributeStrideBytes = mesh.vertexStride;
				meshInfo.m_materialInstanceId = mesh.materialIndex;
				const auto& material = model.m_pMaterial[meshInfo.m_materialInstanceId];
				meshInfo.diffuse[0] = material.diffuse.GetX();
				meshInfo.diffuse[1] = material.diffuse.GetY();
				meshInfo.diffuse[2] = material.diffuse.GetZ();
				meshInfo.m_emissiveColor[0] = material.emissive.GetX();
				meshInfo.m_emissiveColor[1] = material.emissive.GetY();
				meshInfo.m_emissiveColor[2] = material.emissive.GetZ();
				meshInfo.m_emitTexId = model.m_emissiveTexId[meshInfo.m_materialInstanceId];
				meshInfoData.push_back(meshInfo);
			}
		}
	}

	m_hitShaderMeshInfoBuffer.Create(L"RayTraceMeshInfo",
		(UINT)meshInfoData.size(),
		sizeof(meshInfoData[0]),
		meshInfoData.data());

	m_SceneIndices.resize(numModels);
	for (int modelId = 0; modelId < numModels; modelId++)
		m_SceneIndices[modelId] = m_Models[modelId].m_IndexBuffer.GetSRV();
	m_SceneMeshInfo = m_hitShaderMeshInfoBuffer.GetSRV();
}


	void VPLManager::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
	{
		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> error;

		ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
		ThrowIfFailed(Graphics::g_Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
	}

	void VPLManager::InitializeRaytracingRootSignatures()
	{
		// Global Root Signature
	// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
		{
			CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc;
			staticSamplerDesc.Init(0, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.f, 8U,
				D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0.f, D3D12_FLOAT32_MAX,
				D3D12_SHADER_VISIBILITY_ALL);

			CD3DX12_DESCRIPTOR_RANGE ranges[10]; // Perfomance TIP: Order from most frequent to least frequent.
			ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1);
			ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
			ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 2);
			ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 64);
			ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 12);
			ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 32);
			ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 9);
			ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 13);
			ranges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 256, 0, 1); // emissive textures
			ranges[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 16);

			CD3DX12_ROOT_PARAMETER rootParameters[13];
			rootParameters[0].InitAsDescriptorTable(1, ranges);
			rootParameters[1].InitAsConstantBufferView(0);
			rootParameters[2].InitAsDescriptorTable(1, ranges + 1);
			rootParameters[3].InitAsDescriptorTable(1, ranges + 2);
			rootParameters[4].InitAsDescriptorTable(1, ranges + 3);
			rootParameters[5].InitAsDescriptorTable(1, ranges + 4);
			rootParameters[6].InitAsDescriptorTable(1, ranges + 5);
			rootParameters[7].InitAsShaderResourceView(0);
			rootParameters[8].InitAsDescriptorTable(1, ranges + 6);
			rootParameters[9].InitAsDescriptorTable(1, ranges + 7);
			rootParameters[10].InitAsDescriptorTable(1, ranges + 8);
			rootParameters[11].InitAsConstantBufferView(1);
			rootParameters[12].InitAsDescriptorTable(1, ranges + 9);

			CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters, 1,
				&staticSamplerDesc);
			SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
		}


		// Local Root Signature
		{
			CD3DX12_ROOT_PARAMETER rootParameters[2];
			CD3DX12_DESCRIPTOR_RANGE range; // Perfomance TIP: Order from most frequent to least frequent.
			UINT sizeOfRootConstantInDwords = (sizeof(MaterialRootConstant) - 1) / sizeof(DWORD) + 1;
			range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 6, 0);
			rootParameters[0].InitAsDescriptorTable(1, &range);
			rootParameters[1].InitAsConstants(sizeOfRootConstantInDwords, 3);
			CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
			localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
			SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
		}
	}

	void VPLManager::InitializeRaytracingStateObjects()
	{
		// Create 7 subobjects that combine into a RTPSO:
		// Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
		// Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
		// This simple sample utilizes default shader association except for local root signature subobject
		// which has an explicit association specified purely for demonstration purposes.
		// 1 - DXIL library
		// 1 - Triangle hit group
		// 1 - Shader config
		// 2 - Local root signature and association
		// 1 - Global root signature
		// 1 - Pipeline config
		m_dxrStateObjects.resize(NumRaytracingTypes);

		CD3D12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

		// DXIL library
		// This contains the shaders and their entrypoints for the state object.
		// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.

		CD3D12_DXIL_LIBRARY_SUBOBJECT* rayGenLibSubobject;
		CD3D12_DXIL_LIBRARY_SUBOBJECT* anyHitLibSubobject;
		CD3D12_DXIL_LIBRARY_SUBOBJECT* closestHitLibSubobject;
		CD3D12_DXIL_LIBRARY_SUBOBJECT* missLibSubobject;

		////// Shadow Tracing

		rayGenLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
		rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pSLCRayGen, ARRAYSIZE(g_pSLCRayGen)));
		rayGenLibSubobject->DefineExport(L"RayGen");

		anyHitLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
		anyHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pShadowHit, ARRAYSIZE(g_pShadowHit)));
		anyHitLibSubobject->DefineExport(L"AnyHit");

		closestHitLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
		closestHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pShadowHit, ARRAYSIZE(g_pShadowHit)));
		closestHitLibSubobject->DefineExport(L"Hit");

		missLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
		missLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pShadowHit, ARRAYSIZE(g_pShadowHit)));
		missLibSubobject->DefineExport(L"Miss");

		auto hitGroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
		hitGroup->SetClosestHitShaderImport(L"Hit");
		hitGroup->SetAnyHitShaderImport(L"AnyHit");
		hitGroup->SetHitGroupExport(L"HitGroup");
		hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

		// Shader config
		// Defines the maximum sizes in bytes for the ray payload and attribute structure.
		auto shaderConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
		UINT attributeSize = sizeof(XMFLOAT2);  // float2 barycentrics
		shaderConfig->Config(4, attributeSize);

		// create local root signature subobjects
		auto localRootSignature = raytracingPipeline.CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
		localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
		// Define explicit shader association for the local root signature. 
		{
			auto rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
			rootSignatureAssociation->AddExport(L"HitGroup");
		}

		// Global root signature
		// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
		auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3D12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
		globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

		// Pipeline config
		// Defines the maximum TraceRay() recursion depth.
		auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
		// PERFOMANCE TIP: Set max recursion depth as low as needed 
		// as drivers may apply optimization strategies for low recursion depths.
		pipelineConfig->Config(maxRayRecursion);

		// Create the state object.
		ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObjects[SLCSampling])),
			L"Couldn't create DirectX Raytracing state object.\n");

		shaderConfig->Config(16, attributeSize);
		rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pRayTraceReflection, ARRAYSIZE(g_pRayTraceReflection)));
		anyHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pRayTraceReflection, ARRAYSIZE(g_pRayTraceReflection)));
		closestHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pRayTraceReflection, ARRAYSIZE(g_pRayTraceReflection)));
		missLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pRayTraceReflection, ARRAYSIZE(g_pRayTraceReflection)));
		ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObjects[RayTraceReflection])),
			L"Couldn't create DirectX Raytracing state object.\n");

		shaderConfig->Config(20, attributeSize);
		rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightRayGen, ARRAYSIZE(g_pLightRayGen)));
		anyHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		closestHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		missLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObjects[LightTracing])),
			L"Couldn't create DirectX Raytracing state object.\n");
	}


	void VPLManager::InitializeRaytracingShaderTable()
	{
		const UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
#define ALIGN(alignment, num) ((((num) + alignment - 1) / alignment) * alignment)
		const UINT offsetToDescriptorHandle = ALIGN(sizeof(D3D12_GPU_DESCRIPTOR_HANDLE), shaderIdentifierSize);
		const UINT offsetToMaterialConstants = ALIGN(sizeof(UINT32), offsetToDescriptorHandle + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
		const UINT shaderRecordSizeInBytes = ALIGN(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, offsetToMaterialConstants + sizeof(MaterialRootConstant));

		std::vector<byte> pHitShaderTable(shaderRecordSizeInBytes * numMeshTotal);

		auto GetShaderTable = [=](auto *pPSO, byte *pShaderTable)
		{
			void *pHitGroupIdentifierData = pPSO->GetShaderIdentifier(L"HitGroup");

			int meshOffset = 0;
			int materialOffset = 0;
			for (int modelId = 0; modelId < numModels; modelId++)
			{
				int numMaterials = m_Models[modelId].m_Header.materialCount;

				for (UINT blasId = 0; blasId < m_Models[modelId].m_MeshGroups.size(); blasId++)
				{
					auto& group = m_Models[modelId].m_MeshGroups[blasId];
					int numGeoms = group.size();
					for (UINT i = 0; i < numGeoms; i++)
					{
						int meshId = group[i];
						byte *pShaderRecord = (meshOffset + i) * shaderRecordSizeInBytes + pShaderTable;
						memcpy(pShaderRecord, pHitGroupIdentifierData, shaderIdentifierSize);

						UINT materialIndex = materialOffset +
							m_Models[modelId].m_pMesh[meshId].materialIndex;
						memcpy(pShaderRecord + offsetToDescriptorHandle, &m_GpuSceneMaterialSrvs[materialIndex].ptr,
							sizeof(m_GpuSceneMaterialSrvs[materialIndex].ptr));
						MaterialRootConstant material;
						material.MeshInfoID = meshOffset + i;
						material.Use16bitIndex = m_Models[modelId].indexSize == 2;
						memcpy(pShaderRecord + offsetToMaterialConstants, &material, sizeof(material));
					}
					meshOffset += numGeoms;
				}

				materialOffset += numMaterials;
			}
		};

		for (int i = 0; i < NumRaytracingTypes; i++)
		{
			ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
			ThrowIfFailed(m_dxrStateObjects[i].As(&stateObjectProperties));
			GetShaderTable(stateObjectProperties.Get(), pHitShaderTable.data());
			m_RaytracingInputs[i] = RaytracingDispatchRayInputs(*m_dxrDevice.Get(), stateObjectProperties.Get(),
				pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), L"RayGen", L"Miss");
		}
	}

	bool VPLManager::GenerateVPLs(GraphicsContext & context, float VPLEmissionLevel, const Vector3& lightDirection, float lightIntensity, int frameId, int maxDepth /*= 3*/, bool hasRegenRequest/*=false*/)
	{
		ScopedTimer _p0(L"Generate VPLs", context);

		int sqrtDispatchDim = VPLEmissionLevel * 100;
		if (lightIntensity != lastLightIntensity || lightDirection != lastLightDirection || hasRegenRequest)
		{
			needRegenerateVPLs = true;
			lastLightIntensity = lightIntensity;
			lastLightDirection = lightDirection;
		}

		if (needRegenerateVPLs)
		{
			//ScopedTimer _p0(L"LightTracingShader", context);
			// Prepare constants
			LightTracingConstants hitShaderConstants = {};
			hitShaderConstants.sunDirection = -lightDirection;
			hitShaderConstants.sunLight = Vector3(1.0, 1.0, 1.0)*lightIntensity;
			hitShaderConstants.sceneSphere = sceneBoundingSphere;
			hitShaderConstants.DispatchOffset = 0;
			hitShaderConstants.maxDepth = maxDepth;
			hitShaderConstants.frameId = frameId;
			context.WriteBuffer(m_lightConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));
			context.TransitionResource(m_lightConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			context.TransitionResource(VPLBuffers[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.TransitionResource(VPLBuffers[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.TransitionResource(VPLBuffers[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.TransitionResource(m_hitShaderMeshInfoBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			context.FlushResourceBarriers();

			ID3D12GraphicsCommandList * pCommandList = context.GetCommandList();

			ID3D12DescriptorHeap *pDescriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };

			pCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());
			m_dxrCommandList = reinterpret_cast<ID3D12GraphicsCommandList5*>(pCommandList);
			m_dxrCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

			pCommandList->SetComputeRootDescriptorTable(0, m_SceneSrvs);
			pCommandList->SetComputeRootConstantBufferView(1, m_lightConstantBuffer.GetGpuVirtualAddress());
			pCommandList->SetComputeRootDescriptorTable(3, m_VPLUavs);

			context.ResetCounter(VPLBuffers[POSITION], 0);

			D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = m_RaytracingInputs[LightTracing].GetDispatchRayDesc(sqrtDispatchDim, sqrtDispatchDim);

			m_dxrCommandList->SetComputeRootShaderResourceView(7, m_bvh_topLevelAccelerationStructurePointer.GpuVA);
			m_dxrCommandList->SetPipelineState1(m_dxrStateObjects[LightTracing].Get());
			m_dxrCommandList->DispatchRays(&dispatchRaysDesc);

			needRegenerateVPLs = false;

			ReadbackBuffer readbackNumVplsBuffer;
			readbackNumVplsBuffer.Create(L"ReadBackNumVplsBuffer", 1, sizeof(uint32_t));

			context.CopyBuffer(readbackNumVplsBuffer, VPLBuffers[POSITION].GetCounterBuffer());
			context.Flush(true);
			numVPLs = *(uint32_t*)readbackNumVplsBuffer.Map();
			numPaths = sqrtDispatchDim * sqrtDispatchDim;
			//printf("numVPLs: %d  numPaths: %d\n", numVPLs, numPaths);
			readbackNumVplsBuffer.Unmap();

			return true;
		}
		return false;
	}

	void VPLManager::DispatchRays(RaytracingTypes raytracingType, int dispatchWidth, int dispatchHeight)
	{
		D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = m_RaytracingInputs[raytracingType].GetDispatchRayDesc(
			dispatchWidth, dispatchHeight);
		m_dxrCommandList->SetComputeRootShaderResourceView(7, m_bvh_topLevelAccelerationStructurePointer.GpuVA);
		m_dxrCommandList->SetPipelineState1(m_dxrStateObjects[raytracingType].Get());
		m_dxrCommandList->DispatchRays(&dispatchRaysDesc);
	}


	void VPLManager::SetRayTracingDescriptorHeaps(ID3D12GraphicsCommandList * pCommandList)
	{
		ID3D12DescriptorHeap *pDescriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };

		m_dxrCommandList = reinterpret_cast<ID3D12GraphicsCommandList5*>(pCommandList);
		m_dxrCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	}
