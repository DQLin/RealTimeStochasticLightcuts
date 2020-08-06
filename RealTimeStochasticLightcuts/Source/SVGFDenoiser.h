#pragma once
#include "CommandContext.h"
#include "TemporalEffects.h"
#include "SVGFReprojectionCS.h"
#include "SVGFAtrousCS.h"
#include "SVGFMomentsFilterCS.h"
#include "Camera.h"

#define DOUBLE_INPUT

class SVGFDenoiser
{
public:
	ColorBuffer m_IntegratedS[3];
	ColorBuffer m_IntegratedU[3];
	ColorBuffer m_IntegratedM[2];
	ColorBuffer m_HistoryLength;
	ColorBuffer m_PrevImaginaryLinearDepth;

	ComputePSO m_SVGFReprojectionCS;
	ComputePSO m_SVGFAtrousCS;
	ComputePSO m_SVGFMomentsFilterCS;

	static NumVar m_CPhi;
	static ExpVar m_NPhi;
	static NumVar m_ZPhi;
	static NumVar m_PPhi;
	static NumVar m_Alpha;
	static NumVar m_MomentAlpha;
	static NumVar m_DepthTolerance;
	static IntVar m_MaxHistoryLength;
	static BoolVar m_UseZ;

	bool IsInitialized;

	int numInputs;

	SVGFDenoiser() { IsInitialized = false; };

	~SVGFDenoiser()
	{
		RecycleResources();
	}
	
	void Initialize(ComputeContext& Context, RootSignature& m_ComputeRootSig, int numInputs)
	{
		if (!IsInitialized)
		{
			this->numInputs = numInputs;

#define CreatePSO( ObjName, ShaderByteCode ) \
			ObjName.SetRootSignature(m_ComputeRootSig); \
			ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
			ObjName.Finalize();

			CreatePSO(m_SVGFReprojectionCS, g_pSVGFReprojectionCS);
			CreatePSO(m_SVGFAtrousCS, g_pSVGFAtrousCS);
			CreatePSO(m_SVGFMomentsFilterCS, g_pSVGFMomentsFilterCS);
#undef CreatePSO

#define CreateBuffer( BufferName, BufferDebugName, Count ) \
			for (int i = 0; i < Count; i++) { \
				BufferName[i].Create(BufferDebugName + std::to_wstring(i), Graphics::g_SceneColorBuffer.GetWidth(), \
				Graphics::g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R16G16B16A16_FLOAT); \
			}
			CreateBuffer(m_IntegratedS, L"IntergratedS", 3);
			if (numInputs > 1) CreateBuffer(m_IntegratedU, L"IntergratedU", 3);
			CreateBuffer(m_IntegratedM, L"IntergratedM", 2);

			m_PrevImaginaryLinearDepth.Create(L"PrevImaginaryLinearDepth", Graphics::g_SceneColorBuffer.GetWidth(),
				Graphics::g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R32_FLOAT);

			m_HistoryLength.Create(L"HistoryLength", Graphics::g_SceneColorBuffer.GetWidth(),
				Graphics::g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R8_UINT);
			Context.TransitionResource(m_HistoryLength, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
			Context.ClearUAV(m_HistoryLength);
			Context.Flush(true);
			IsInitialized = true;
		}
	}

	void RecycleResources()
	{
		if (IsInitialized)
		{
			m_IntegratedS[0].Destroy();
			m_IntegratedS[1].Destroy();
			m_IntegratedS[2].Destroy();
			m_IntegratedU[0].Destroy();
			m_IntegratedU[1].Destroy();
			m_IntegratedU[2].Destroy();
			m_IntegratedM[0].Destroy();
			m_IntegratedM[1].Destroy();
			m_HistoryLength.Destroy();

			IsInitialized = false;
		}
	}

	void Reproject(ComputeContext& Context, const Math::Camera& camera, ColorBuffer& TexCurS, ColorBuffer& TexCurU, bool disable);

	void Filter(ComputeContext& Context, ColorBuffer& ResultBuffer);

	void FilterMoments(ComputeContext& Context);
};
