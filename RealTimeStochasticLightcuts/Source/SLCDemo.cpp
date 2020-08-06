// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files (the "Software"), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all 
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.

//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  Alex Nankervis
//             James Stanard
//

#include "SLCDemo.h"

#include "DepthViewerVS.h"
#include "DepthViewerPS.h"
#include "ModelViewerVS.h"
#include "ModelViewerPS.h"
#include "ScreenShaderVS.h"
#include "ScreenShaderPS.h"
#include "EmissiveIntegrationVS.h"
#include "EmissiveIntegrationPS.h"
#include "TestUtils.h"

ExpVar m_SunLightIntensity("Sunlight/Sun Light Intensity", 3.0f, 0.0f, 16.0f, 0.1f);
NumVar m_SunOrientation("Sunlight/Sun Orientation", 1.16, 0.0f, 10.0f, 0.01f);
NumVar m_SunInclination("Sunlight/Sun Inclination", 0.86, 0.0f, 1.0f, 0.001f);

NumVar ShadowDimX("Sunlight/Sun Shadow Dim X", 5000, 1, 10000, 1);
NumVar ShadowDimY("Sunlight/Sun Shadow Dim Y", 3000, 1, 10000, 1);
NumVar ShadowDimZ("Sunlight/Sun Shadow Dim Z", 3000, 1, 10000, 1);

NumVar ShadowCenterX("Sunlight/Sun Shadow Center X", 0, -1000, 1000, 1);
NumVar ShadowCenterY("Sunlight/Sun Shadow Center Y", -500, -1000, 1000, 1);
NumVar ShadowCenterZ("Sunlight/Sun Shadow Center Z", 0, -1000, 1000, 1);
BoolVar enableCameraAnimation("Animate Camera", true);

extern BoolVar m_EnableNodeViz;

const char* debugViewNames[6] = { "N/A", "Diffuse Sampling",
"Specular Sampling", "Albedo", "Normal", "Specular Params" };
EnumVar DebugView("Debug View", 0, 6, debugViewNames);

SLCDemo::SLCDemo(const std::vector<std::string>& modelFiles, bool isVPLScene, bool isCameraInitialized, const Camera& camera,
	const SunLightConfig& sunLightConfig, float exposure, std::shared_ptr<SimpleAnimation> animation)
{
	m_Models.resize(1);
	if (animation)
	{
		m_Models[0].m_Animations.push_back(animation);
		m_Models[0].m_ActiveAnimation = 0;
	}

	slcRenderer.gUseMeshLight = !isVPLScene;
	m_IsCameraInitialized = isCameraInitialized;
	if (isCameraInitialized) m_Camera = camera;
	m_SunLightConfig = sunLightConfig;
	m_SunLightIntensity = sunLightConfig.lightIntensity;
	m_SunOrientation = sunLightConfig.orientationAnimPhase;
	m_SunInclination = sunLightConfig.inclinationAnimPhase;
	m_ModelFiles = modelFiles;
	PostEffects::Exposure = exposure;
}

void SLCDemo::Startup(void)
{
	float costheta = cosf(m_SunOrientation);
	float sintheta = sinf(m_SunOrientation);
	float cosphi = cosf(m_SunInclination * 3.141592654f * 0.5f);
	float sinphi = sinf(m_SunInclination * 3.141592654f * 0.5f);
	m_SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));

	m_MainViewport.TopLeftX = 0;
	m_MainViewport.TopLeftY = 0;
	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

	//create ray tracing descriptor heap
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
	HRESULT hr = g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;

	m_RootSig.Reset(10, 2);
	m_RootSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(1, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);

	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 32, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 5, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[5].InitAsConstants(1, 2, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 4, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[7].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[8].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[9].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 16, 3, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig.Finalize(L"ModelViewer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_ComputeRootSig.Reset(3, 1);
	m_ComputeRootSig.InitStaticSampler(0, SamplerPointClampDesc);
	m_ComputeRootSig[0].InitAsConstantBuffer(0);
	m_ComputeRootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 12);
	m_ComputeRootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 12);

	m_ComputeRootSig.Finalize(L"Image Processing");

	DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT NormalFormat = g_SceneNormalBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
	DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();

	DXGI_FORMAT GBufferColorFormats[] = { g_ScenePositionBuffer.GetFormat(), g_SceneNormalBuffer.GetFormat(), g_SceneAlbedoBuffer.GetFormat(),
										g_SceneSpecularBuffer.GetFormat(), g_SceneEmissionBuffer.GetFormat(), g_VelocityBuffer.GetFormat() };

	D3D12_INPUT_ELEMENT_DESC vertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "INSTANCEID", 0, DXGI_FORMAT_R32_UINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
	};

	D3D12_INPUT_ELEMENT_DESC screenVertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	D3D12_INPUT_ELEMENT_DESC EmissiveVertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	// Depth-only (2x rate)
	m_DepthPSO.SetRootSignature(m_RootSig);
	m_DepthPSO.SetRasterizerState(RasterizerTwoSided);
	m_DepthPSO.SetBlendState(BlendNoColorWrite);
	m_DepthPSO.SetDepthStencilState(DepthStateReadWrite);
	m_DepthPSO.SetInputLayout(_countof(vertElem), vertElem);
	m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_DepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
	m_DepthPSO.SetVertexShader(g_pDepthViewerVS, sizeof(g_pDepthViewerVS));
	m_DepthPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_DepthPSO.Finalize();

	// Depth-only shading but with alpha testing
	m_CutoutDepthPSO = m_DepthPSO;
	m_CutoutDepthPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_CutoutDepthPSO.SetRasterizerState(RasterizerTwoSided);
	m_CutoutDepthPSO.Finalize();

	// Depth-only but with a depth bias and/or render only backfaces
	m_ShadowPSO = m_DepthPSO;
	m_ShadowPSO.SetRasterizerState(RasterizerShadowTwoSided);
	m_ShadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
	m_ShadowPSO.Finalize();

	// Shadows with alpha testing
	m_CutoutShadowPSO = m_ShadowPSO;
	m_CutoutShadowPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_CutoutShadowPSO.SetRasterizerState(RasterizerShadowTwoSided);
	m_CutoutShadowPSO.Finalize();

	// Full color pass
	m_ModelPSO = m_DepthPSO;
	m_ModelPSO.SetBlendState(BlendDisable);
	m_ModelPSO.SetDepthStencilState(DepthStateTestEqual);
	m_ModelPSO.SetRenderTargetFormats(6, GBufferColorFormats, DepthFormat);
	m_ModelPSO.SetVertexShader(g_pModelViewerVS, sizeof(g_pModelViewerVS));
	m_ModelPSO.SetPixelShader(g_pModelViewerPS, sizeof(g_pModelViewerPS));
	m_ModelPSO.Finalize();

	m_CutoutModelPSO = m_ModelPSO;
	m_CutoutModelPSO.SetRasterizerState(RasterizerTwoSided);
	m_CutoutModelPSO.Finalize();

	m_ScreenPSO.SetRootSignature(m_RootSig);
	m_ScreenPSO.SetRasterizerState(RasterizerDefault);
	m_ScreenPSO.SetBlendState(BlendDisable);
	m_ScreenPSO.SetDepthStencilState(DepthStateDisabled);
	m_ScreenPSO.SetInputLayout(_countof(screenVertElem), screenVertElem);
	m_ScreenPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_ScreenPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
	m_ScreenPSO.SetVertexShader(g_pScreenShaderVS, sizeof(g_pScreenShaderVS));
	m_ScreenPSO.SetPixelShader(g_pScreenShaderPS, sizeof(g_pScreenShaderPS));
	m_ScreenPSO.Finalize();

	m_GBuffer[0] = g_ScenePositionBuffer.GetSRV();
	m_GBuffer[1] = g_SceneNormalBuffer.GetSRV();
	m_GBuffer[2] = g_SceneAlbedoBuffer.GetSRV();
	m_GBuffer[3] = g_SceneSpecularBuffer.GetSRV();
	m_GBuffer[4] = g_SceneEmissionBuffer.GetSRV();
	m_GBuffer[5] = g_VelocityBuffer.GetSRV();
	g_SceneEmissionBuffer.SetClearColor(Color(0.f, 0.f, 0.f, 0.f));

	TextureManager::Initialize(L"");
	if (m_ModelFiles.empty())
	{
		// use default demo scene of miniEngine
		slcRenderer.gUseMeshLight = false;
		ASSERT(m_Models[0].Load({ "DefaultData/sponza.h3d" }), "Failed to load model");
		ASSERT(m_Models[0].m_Header.meshCount > 0, "Model contains no meshes");
	}
	else
	{
		ASSERT(m_Models[0].Load(m_ModelFiles), "Failed to load model");
		ASSERT(m_Models[0].m_Header.meshCount > 0, "Model contains no meshes");
	}
	std::cout << "Loading Complete." << std::endl;

	if (slcRenderer.gUseMeshLight)
	{
		// initialize resources for emissive integration
		m_DummyColorBuffer.Create(L"dummy color buffer", 2048, 2048, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
		m_DummyDepthBuffer.Create(L"dummy depth buffer", 2048, 2048, DXGI_FORMAT_D16_UNORM);

		D3D12_RASTERIZER_DESC RasterizerStateConservativeTwoSided = RasterizerTwoSided;
		RasterizerStateConservativeTwoSided.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
		m_EmissiveIntegrationPSO.SetRootSignature(m_RootSig);
		m_EmissiveIntegrationPSO.SetRasterizerState(RasterizerStateConservativeTwoSided);
		m_EmissiveIntegrationPSO.SetBlendState(BlendDisable);
		m_EmissiveIntegrationPSO.SetDepthStencilState(DepthStateDisabled);
		m_EmissiveIntegrationPSO.SetInputLayout(_countof(EmissiveVertElem), EmissiveVertElem);
		m_EmissiveIntegrationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_EmissiveIntegrationPSO.SetRenderTargetFormats(1, &m_DummyColorBuffer.GetFormat(), m_DummyDepthBuffer.GetFormat());
		m_EmissiveIntegrationPSO.SetVertexShader(g_pEmissiveIntegrationVS, sizeof(g_pEmissiveIntegrationVS));
		m_EmissiveIntegrationPSO.SetPixelShader(g_pEmissiveIntegrationPS, sizeof(g_pEmissiveIntegrationPS));
		m_EmissiveIntegrationPSO.Finalize();
	}

	m_SceneSphere = m_Models[0].m_SceneBoundingSphere;
	Vector3 modelBoundingBoxExtent = m_Models[0].m_Header.boundingBox.max - m_Models[0].m_Header.boundingBox.min;
	float modelRadius = Length(modelBoundingBoxExtent) * .5f;

	if (m_ModelFiles.empty())
	{
		// lobby
		Vector3 eye(-899.81, 607.74, -36.63);
		m_Camera.SetEyeAtUp(eye, eye + Vector3(0.966, -0.259, 0.02), Vector3(kYUnitVector));
		m_Camera.SetZRange(1.0f, 10000.0f);
	}
	else if (!m_IsCameraInitialized)
	{
		Model1::BoundingBox bb = m_Models[0].GetBoundingBox();
		Vector3 eye(0.5*(bb.max + bb.min));
		m_Camera.SetEyeAtUp(eye, eye + Vector3(0, 0, -1), Vector3(kYUnitVector));
		m_Camera.SetZRange(0.0005 * modelRadius, 5 * modelRadius);
	}

	if (m_Models[0].CameraNodeID != -1)
	{
		glm::vec3 pos = glm::vec3(m_Models[0].initialCameraMatrix[3]);
		glm::vec3 up = glm::vec3(m_Models[0].initialCameraMatrix[1]);
		glm::vec3 lookat = glm::vec3(m_Models[0].initialCameraMatrix[2]);
		glm::mat4 viewMat =  glm::lookAt(pos, lookat, up);
		Matrix4 viewMatInternalFormat;
		memcpy(&viewMatInternalFormat, &viewMat, 64);
		m_Camera.SetViewMatrix(viewMatInternalFormat);
		m_Camera.Update();
	}

	m_CameraController.reset(new CameraController(m_Camera, Vector3(kYUnitVector)));

	if (!m_ModelFiles.empty())
	{
		m_CameraController->SetMoveSpeed(modelRadius);
		m_CameraController->SetStrafeSpeed(modelRadius);
		ShadowDimX = modelBoundingBoxExtent.GetX() * 2.f;
		ShadowDimY = modelBoundingBoxExtent.GetY() * 2.f;
		ShadowDimZ = modelBoundingBoxExtent.GetZ() * 2.f;
		ShadowCenterY = -0.5 * modelRadius;
	}

	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();
	m_quad.Init();

	MotionBlur::Enable = false;
	TemporalEffects::EnableTAA = false;
	FXAA::Enable = true;
	PostEffects::EnableHDR = true;
	PostEffects::EnableAdaptation = false;
	SSAO::Enable = false;
	DepthOfField::Enable = false;

	frameId = -1;

	if (slcRenderer.gUseMeshLight)
	{
		GraphicsContext& gfxContext = GraphicsContext::Begin(L"Integrate Emissive Triangles");
		IntegrateEmissiveTriangles(gfxContext);
		// prepare mesh light triangle intensity buffer
		for (int i = 0; i < m_Models.size(); i++)
		{
			m_Models[i].PopulateMeshLightTriangleIntensityBuffer(gfxContext);
		}
		gfxContext.Finish(true);
	}
	slcRenderer.Initialize(m_Models.data(), m_Models.size(), m_MainViewport.Width, m_MainViewport.Height);

	return;
}

void SLCDemo::Cleanup(void)
{
	for (int i = 0; i < m_Models.size(); i++) m_Models[i].Clear();
}

namespace Graphics
{
	extern EnumVar DebugZoom;
}

void SLCDemo::Update(float deltaT)
{
	//ScopedTimer _prof(L"Update State");

	double currentAnimationTime = 0;

	if (frameId == -1)
	{
		initialTick = SystemTime::GetCurrentTick();
	}
	else currentAnimationTime = SystemTime::TimeBetweenTicks(initialTick, SystemTime::GetCurrentTick());

	if (GameInput::IsFirstPressed(GameInput::kLShoulder))
		DebugZoom.Decrement();
	else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
		DebugZoom.Increment();

	hasGeometryChange = false;
	bool hasChange = false;
	Vector3 pos = m_Camera.GetPosition();
	Vector3 fwd = m_Camera.GetForwardVec();

#ifdef GROUND_TRUTH
	m_CameraController->EnableMomentum(false);
	bool hasCameraMovement = false;
	m_CameraController->Update(0.2*deltaT, hasCameraMovement);
#endif
	

#ifdef FRUSTUM_CULLING
	CalculateFrustum();
#endif

	if (m_SunLightConfig.hasAnimation)
	{
		m_SunInclination = m_SunLightConfig.inclinationAnimPhase + m_SunLightConfig.inclinationAnimFreq * sin(currentAnimationTime);
		m_SunOrientation = m_SunLightConfig.orientationAnimPhase + m_SunLightConfig.orientationAnimFreq * sin(currentAnimationTime);
	}

	// update the global matrices
	if (m_Models[0].m_Animations.size() > 0)
	{
		auto& pAnimation = m_Models[0].m_Animations[m_Models[0].m_ActiveAnimation];
		pAnimation->animate(currentAnimationTime, m_Models[0].m_CPULocalMatrices);
	}

	CommandContext& InitContext = CommandContext::Begin();
	InitContext.CopyBuffer(m_Models[0].m_PreviousGlobalMatrixBuffer, m_Models[0].m_GlobalMatrixBuffer);
	InitContext.Finish(true);

	for (size_t i = 0; i < m_Models[0].m_CPUGlobalMatrices.size(); i++)
	{
		if (m_Models[0].m_MatrixParentId[i] == -1) m_Models[0].m_CPUGlobalMatrices[i] = m_Models[0].m_CPULocalMatrices[i];
		else m_Models[0].m_CPUGlobalMatrices[i] = m_Models[0].m_CPUGlobalMatrices[m_Models[0].m_MatrixParentId[i]] * m_Models[0].m_CPULocalMatrices[i];
		m_Models[0].m_CPUGlobalInvTransposeMatrices[i] = glm::transpose(glm::inverse(m_Models[0].m_CPUGlobalMatrices[i]));
	}
	// update
	m_Models[0].m_GlobalMatrixBuffer.Update(0, m_Models[0].m_CPUGlobalMatrices.size(), m_Models[0].m_CPUGlobalMatrices.data());
	m_Models[0].m_GlobalInvTransposeMatrixBuffer.Update(0, m_Models[0].m_CPUGlobalInvTransposeMatrices.size(), m_Models[0].m_CPUGlobalInvTransposeMatrices.data());
	hasGeometryChange = m_Models[0].m_Animations.size() > 0;


	if (enableCameraAnimation && m_Camera.m_UseCameraAnimation)
	{
		if (m_Models[0].CameraNodeID != -1)
		{
			glm::mat4 cameraMatrix = m_Models[0].m_CPUGlobalMatrices[m_Models[0].CameraNodeID] * m_Models[0].initialCameraMatrix;
			glm::vec3 pos = glm::vec3(cameraMatrix[3]);
			glm::vec3 up = glm::vec3(cameraMatrix[1]);
			glm::vec3 lookat = glm::vec3(cameraMatrix[2]);
			glm::mat4 newviewmatrix = glm::lookAt(pos, pos + lookat, up);
			Matrix4 vm;
			memcpy(&vm, &newviewmatrix, 64);
			m_Camera.SetViewMatrix(vm);
			m_Camera.Update();
		}
		else
		{
			m_Camera.Animate(currentAnimationTime);
			m_Camera.Update();
		}
	}
	else
	{
		m_CameraController->Update(0.2*deltaT);
	}

	if (GameInput::IsFirstPressed(GameInput::kKey_f))
	{
		slcRenderer.m_Filtering = !slcRenderer.m_Filtering;
	}

	if (GameInput::IsFirstPressed(GameInput::kKey_r))
	{
		slcRenderer.m_bRayTracedReflection = !slcRenderer.m_bRayTracedReflection;
	}

	if (GameInput::IsFirstPressed(GameInput::kKey_h))
	{
		PostEffects::EnableHDR = !PostEffects::EnableHDR;
	}

	if (GameInput::IsFirstPressed(GameInput::kKey_t))
	{
		TemporalEffects::EnableTAA = !TemporalEffects::EnableTAA;
	}

	if (GameInput::IsFirstPressed(GameInput::kKey_b))
	{
		PostEffects::BloomEnable = !PostEffects::BloomEnable;
	}

	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();

	float costheta = cosf(m_SunOrientation);
	float sintheta = sinf(m_SunOrientation);
	float cosphi = cosf(m_SunInclination * 3.14159f * 0.5f);
	float sinphi = sinf(m_SunInclination * 3.14159f * 0.5f);
	m_SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));

	TemporalEffects::GetJitterOffset(m_MainViewport.TopLeftX, m_MainViewport.TopLeftY);

	float oldWidth = m_MainViewport.Width;
	float oldHeight = m_MainViewport.Height;

	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

	if (m_MainViewport.Width != oldWidth || m_MainViewport.Height != oldHeight) hasChange = true;

#ifdef GROUND_TRUTH
	if (hasCameraMovement) {
		frameId = 0;
	}
	else frameId++;
#else
	frameId++;
#endif
}

void SLCDemo::RenderObjects(GraphicsContext& gfxContext, int modelId, const Matrix4& ViewProjMat, ModelViewerConstants psConstants, eObjectFilter Filter, bool renderShadowMap)
{
	struct VSConstants
	{
		glm::mat4 modelMatrix;
		Matrix4 ViewProjection;
		Matrix4 prevViewProjectionMatrix;
		Vector3 viewerPos;
	} vsConstants;
	vsConstants.modelMatrix = m_Models[modelId].m_modelMatrix;
	vsConstants.ViewProjection = ViewProjMat;
	vsConstants.prevViewProjectionMatrix = m_Camera.GetPrevViewProjMatrix();
	vsConstants.viewerPos = m_Camera.GetPosition();

	if (!slcRenderer.gUseMeshLight)
	{
		gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);
	}

	uint32_t materialIdx = 0xFFFFFFFFul;

	uint32_t VertexStride = m_Models[modelId].m_VertexStride;

	for (uint32_t meshIndex = 0; meshIndex < m_Models[modelId].m_Header.meshCount; meshIndex++)
	{
		const Model1::Mesh& mesh = m_Models[modelId].m_pMesh[meshIndex];

		uint32_t indexCount = mesh.indexCount;
		uint32_t startIndex = mesh.indexDataByteOffset / m_Models[modelId].indexSize;
		uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;
		uint32_t instanceCount = mesh.instanceCount;
		uint32_t baseInstance = mesh.instanceListOffset;

		if (slcRenderer.gUseMeshLight)
		{
			vsConstants.modelMatrix = m_Models[modelId].m_modelMatrix * m_Models[modelId].meshModelMatrices[meshIndex];
			gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);
		}

		if (mesh.materialIndex != materialIdx)
		{
#ifdef FRUSTUM_CULLING
			if (!renderShadowMap && !IsBoxInFrustum(mesh.boundingBox)) continue;
#endif
			if (m_Models[modelId].m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kCutout) ||
				!m_Models[modelId].m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kOpaque))
				continue;
			if (m_Models[modelId].m_pMaterialIsTransparent[mesh.materialIndex]) continue;

			materialIdx = mesh.materialIndex;
			gfxContext.SetDynamicDescriptors(2, 0, 3, m_Models[modelId].GetSRVs(materialIdx));

			auto EmitSRV = m_Models[modelId].GetEmitSRV(materialIdx);
			if (EmitSRV.ptr != 0)
			{
				psConstants.hasEmissiveTexture = true;  gfxContext.SetDynamicDescriptor(2, 3, EmitSRV);
			}
			else
			{
				psConstants.hasEmissiveTexture = false;
			}

			psConstants.textureFlags = m_Models[modelId].GetTextureFlags(materialIdx);
		}

		gfxContext.SetConstants(5, baseVertex, materialIdx);
		psConstants.diffuseColor = m_Models[modelId].m_pMaterial[mesh.materialIndex].diffuse;
		psConstants.specularColor = m_Models[modelId].m_pMaterial[mesh.materialIndex].specular;
		psConstants.emissionColor = m_Models[modelId].m_pMaterial[mesh.materialIndex].emissive;
		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		gfxContext.DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, baseInstance);
	}
}

void SLCDemo::GetSubViewportAndScissor(int i, int j, int rate, D3D12_VIEWPORT & viewport, D3D12_RECT & scissor)
{
	viewport.Width = m_MainViewport.Width / rate;
	viewport.Height = m_MainViewport.Height / rate;
	viewport.TopLeftX = 0.5 + viewport.Width * j;
	viewport.TopLeftY = 0.5 + viewport.Height * i;

	scissor = m_MainScissor;
}


void SLCDemo::IntegrateEmissiveTriangles(GraphicsContext& gfxContext)
{
	gfxContext.SetRootSignature(m_RootSig);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gfxContext.TransitionResource(m_DummyDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	gfxContext.SetDepthStencilTarget(m_DummyDepthBuffer.GetDSV());
	gfxContext.TransitionResource(m_DummyColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.SetRenderTarget(m_DummyColorBuffer.GetRTV());

	__declspec(align(16)) struct PSConstants
	{
		Vector3 emissionColor;
	} psConstants;

	for (int modelId = 0; modelId < m_Models.size(); modelId++)
	{
		gfxContext.TransitionResource(m_Models[modelId].m_MeshLightIndexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		gfxContext.TransitionResource(m_Models[modelId].m_MeshLightVertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		gfxContext.SetIndexBuffer(m_Models[modelId].m_MeshLightIndexBuffer.IndexBufferView());
		gfxContext.SetVertexBuffer(0, m_Models[modelId].m_MeshLightVertexBuffer.VertexBufferView());

		gfxContext.TransitionResource(m_Models[modelId].m_MeshLightTriangleIntensityBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gfxContext.TransitionResource(m_Models[modelId].m_MeshLightTriangleNumberOfTexelsBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		{
			gfxContext.SetPipelineState(m_EmissiveIntegrationPSO);
			int numMeshLights = m_Models[modelId].m_CPUMeshLights.size();
			for (uint32_t meshIndex = 0; meshIndex < numMeshLights; meshIndex++)
			{
				const CPUMeshLight& mesh = m_Models[modelId].m_CPUMeshLights[meshIndex];
				if (mesh.emitMatId >= 0)
				{
					int materialIdx = mesh.emitMatId;
					D3D12_VIEWPORT vp = m_Models[modelId].GetEmitIntegrationViewport(materialIdx);
					D3D12_RECT scissor = m_MainScissor;
					scissor.right = vp.Width;
					scissor.bottom = vp.Height;
					gfxContext.SetViewportAndScissor(vp, scissor);
					uint32_t indexCount = mesh.indexCount;
					uint32_t startIndex = mesh.indexOffset;
					uint32_t baseVertex = mesh.vertexOffset;
					auto EmitSRV = m_Models[modelId].GetEmitResource(materialIdx).GetSRV();
					gfxContext.SetDynamicDescriptor(6, 0, m_Models[modelId].m_MeshLightTriangleIntensityBuffer.GetUAV());
					gfxContext.SetDynamicDescriptor(6, 1, m_Models[modelId].m_MeshLightTriangleNumberOfTexelsBuffer.GetUAV());
					gfxContext.SetDynamicDescriptor(2, 3, EmitSRV);
					gfxContext.FlushResourceBarriers();
					gfxContext.ClearColor(m_DummyColorBuffer);
					psConstants.emissionColor = m_Models[modelId].m_pMaterial[materialIdx].emissive;
					gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
					int triangleOffset = mesh.indexOffset / 3;
					gfxContext.SetConstants(5, triangleOffset, meshIndex);
					gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
				}
			}
		}
	}
	gfxContext.Flush(true);

}

template<class T>
T base_name(T const & path, T const & delims = "/\\")
{
	return path.substr(path.find_last_of(delims) + 1);
}
template<class T>
T remove_extension(T const & filename)
{
	typename T::size_type const p(filename.find_last_of('.'));
	return p > 0 && p != T::npos ? filename.substr(0, p) : filename;
}


void SLCDemo::RenderScene(void)
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

	if (slcRenderer.gUseMeshLight)
		slcRenderer.BuildMeshLightTree(gfxContext, ViewConfig(m_Camera, m_MainViewport, m_MainScissor), frameId, hasGeometryChange, hasGeometryChange);
	else
		slcRenderer.BuildVPLLightTree(gfxContext, m_SunDirection, m_SunLightIntensity, frameId, hasGeometryChange);

	std::chrono::high_resolution_clock::time_point t1;
	std::chrono::high_resolution_clock::time_point t2;
	std::chrono::duration<double> time_span;

	uint32_t FrameIndexMod2 = TemporalEffects::GetFrameIndexMod2();

	ModelViewerConstants psConstants;
	psConstants.sunDirection = m_SunDirection;
	psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
	psConstants.viewerPos = m_Camera.GetPosition();
	psConstants.screenDimension = glm::vec2(m_MainViewport.Width, m_MainViewport.Height);
	psConstants.zMagic = (m_Camera.GetFarClip() - m_Camera.GetNearClip()) / m_Camera.GetNearClip();

	// Set the default state for command lists
	{
		ScopedTimer _prof(L"G-buffer generation", gfxContext);

		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext.SetDynamicDescriptor(9, 0, m_Models[0].m_GlobalMatrixBuffer.GetSRV());
		gfxContext.SetDynamicDescriptor(9, 1, m_Models[0].m_GlobalInvTransposeMatrixBuffer.GetSRV());
		gfxContext.SetDynamicDescriptor(9, 2, m_Models[0].m_PreviousGlobalMatrixBuffer.GetSRV());

		// can use vertex depth attribs instead
		{
			//ScopedTimer _prof(L"Z PrePass", gfxContext);

			for (int modelId = 0; modelId < m_Models.size(); modelId++)
			{
				gfxContext.SetIndexBuffer(m_Models[modelId].m_IndexBuffer.IndexBufferView());

				const D3D12_VERTEX_BUFFER_VIEW VBViews[] = { m_Models[modelId].m_VertexBuffer.VertexBufferView(),
											m_Models[modelId].m_InstanceBuffer.VertexBufferView() };
				gfxContext.SetVertexBuffers(0, 2, VBViews);

				{
					//ScopedTimer _prof(L"Opaque", gfxContext);
					gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
					if (modelId == 0) gfxContext.ClearDepth(g_SceneDepthBuffer);

					gfxContext.SetPipelineState(m_DepthPSO);

					gfxContext.SetDepthStencilTarget(g_SceneDepthBuffer.GetDSV());
					gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
					RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kOpaque);
				}
				{
					//ScopedTimer _prof(L"Cutout", gfxContext);
					gfxContext.SetPipelineState(m_CutoutDepthPSO);
					RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kCutout);
				}
			}
		}

		// Using MiniEngine's SSAO class to compute linearized depth
		SSAO::LinearizeDepth(gfxContext, m_Camera);

		{
			//ScopedTimer _prof(L"Main Render", gfxContext);

			for (int modelId = 0; modelId < m_Models.size(); modelId++)
			{
				gfxContext.SetIndexBuffer(m_Models[modelId].m_IndexBuffer.IndexBufferView());
				const D3D12_VERTEX_BUFFER_VIEW VBViews[] = { m_Models[modelId].m_VertexBuffer.VertexBufferView(),
						m_Models[modelId].m_InstanceBuffer.VertexBufferView() };
				gfxContext.SetVertexBuffers(0, 2, VBViews);

				if (!slcRenderer.gUseMeshLight)
				{
					//ScopedTimer _prof(L"Render Shadow Map", gfxContext);

					m_SunShadow.UpdateMatrix(-m_SunDirection, Vector3(ShadowCenterX, ShadowCenterY, ShadowCenterZ), Vector3(ShadowDimX, ShadowDimY, ShadowDimZ),
						(uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

					g_ShadowBuffer.BeginRendering(gfxContext, modelId == 0);
					gfxContext.SetPipelineState(m_ShadowPSO);
					RenderObjects(gfxContext, modelId, m_SunShadow.GetViewProjMatrix(), psConstants, kOpaque, true);
					gfxContext.SetPipelineState(m_CutoutShadowPSO);
					RenderObjects(gfxContext, modelId, m_SunShadow.GetViewProjMatrix(), psConstants, kCutout, true);
					g_ShadowBuffer.EndRendering(gfxContext);
				}


				{
					//ScopedTimer _prof(L"Render G-buffer", gfxContext);

					gfxContext.SetPipelineState(m_ModelPSO);

					gfxContext.TransitionResource(g_ScenePositionBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
					gfxContext.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
					gfxContext.TransitionResource(g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
					gfxContext.TransitionResource(g_SceneSpecularBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
					gfxContext.TransitionResource(g_SceneEmissionBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
					gfxContext.TransitionResource(g_VelocityBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
					gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);
					if (modelId == 0) gfxContext.ClearColor(g_ScenePositionBuffer);
					if (modelId == 0) gfxContext.ClearColor(g_SceneNormalBuffer);
					if (modelId == 0) gfxContext.ClearColor(g_SceneAlbedoBuffer);
					if (modelId == 0) gfxContext.ClearColor(g_SceneSpecularBuffer);
					if (modelId == 0) gfxContext.ClearColor(g_SceneEmissionBuffer);
					if (modelId == 0) gfxContext.ClearColor(g_VelocityBuffer);
					const D3D12_CPU_DESCRIPTOR_HANDLE gBufferHandles[] = { g_ScenePositionBuffer.GetRTV(), g_SceneNormalBuffer.GetRTV(),
						g_SceneAlbedoBuffer.GetRTV(), g_SceneSpecularBuffer.GetRTV(), g_SceneEmissionBuffer.GetRTV(), g_VelocityBuffer.GetRTV() };
					gfxContext.SetRenderTargets(6, gBufferHandles, g_SceneDepthBuffer.GetDSV_DepthReadOnly());
					gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

					RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kOpaque);

					gfxContext.SetPipelineState(m_CutoutModelPSO);
					RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kCutout);
				}
			}
		}

	}

	slcRenderer.Render(gfxContext, ViewConfig(m_Camera, m_MainViewport, m_MainScissor), frameId, hasGeometryChange);
	
	{
		ScopedTimer _prof(L"Compositing", gfxContext);
		__declspec(align(16)) struct
		{
			Matrix4 WorldToShadow;
			Vector3 ViewerPos;
			Vector3 SunDirection;
			Vector3 SunColor;
			Vector4 ShadowTexelSize;
			int scrWidth;
			int scrHeight;
			int shadowRate;
			int debugMode;
			int enableFilter;
			int gUseMeshLight;
			int hasRayTracedReflection;
			int visualizeNodes;
		} psConstants;

		psConstants.WorldToShadow = m_SunShadow.GetShadowMatrix();
		psConstants.ViewerPos = m_Camera.GetPosition();
		psConstants.SunDirection = m_SunDirection;
		psConstants.SunColor = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
		psConstants.ShadowTexelSize = Vector4(1.0f / g_ShadowBuffer.GetWidth());
		psConstants.scrWidth = m_MainViewport.Width;
		psConstants.scrHeight = m_MainViewport.Height;
		psConstants.debugMode = DebugView;
		psConstants.enableFilter = slcRenderer.m_Filtering;
		psConstants.shadowRate = 0;
		psConstants.gUseMeshLight = slcRenderer.gUseMeshLight;
		psConstants.hasRayTracedReflection = slcRenderer.m_bRayTracedReflection;
		psConstants.visualizeNodes = m_EnableNodeViz;
		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		gfxContext.SetIndexBuffer(m_quad.m_IndexBuffer.IndexBufferView());
		gfxContext.SetVertexBuffer(0, m_quad.m_VertexBuffer.VertexBufferView());
		gfxContext.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		gfxContext.TransitionResource(slcRenderer.m_samplingBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(slcRenderer.m_filteredCombined, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(slcRenderer.m_reflectionSamplingBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
		// debug views
		int DebugViewOffset = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE customView;
		customView = slcRenderer.m_vizBuffer.GetSRV();

		if (DebugView == 0 && m_EnableNodeViz)
		{
			gfxContext.TransitionResource(slcRenderer.m_vizBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		else if (DebugView == 1)
		{
			gfxContext.TransitionResource(slcRenderer.m_samplingBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			customView = slcRenderer.m_samplingBuffer.GetSRV();
		}
		else if (DebugView == 2)
		{
			gfxContext.TransitionResource(slcRenderer.m_reflectionSamplingBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			customView = slcRenderer.m_reflectionSamplingBuffer.GetSRV();
		}
		else if (DebugView == 3)
		{
			gfxContext.TransitionResource(g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			customView = g_SceneAlbedoBuffer.GetSRV();
		}
		else if (DebugView == 4)
		{
			gfxContext.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			customView = g_SceneNormalBuffer.GetSRV();
		}
		else if (DebugView == 5)
		{
			gfxContext.TransitionResource(g_SceneSpecularBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			customView = g_SceneSpecularBuffer.GetSRV();
		}

		D3D12_CPU_DESCRIPTOR_HANDLE compositeSrvs[5] = { slcRenderer.m_samplingBuffer.GetSRV(),
					slcRenderer.m_filteredCombined.GetSRV(), slcRenderer.m_reflectionSamplingBuffer.GetSRV(), g_ShadowBuffer.GetSRV(), customView };

		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		gfxContext.SetDynamicDescriptors(3, 0, _countof(m_GBuffer), m_GBuffer);
		gfxContext.SetDynamicDescriptors(4, 0, _countof(compositeSrvs), compositeSrvs);
		gfxContext.SetPipelineState(m_ScreenPSO);
		gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(g_SceneColorBuffer);
		gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV());
		gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
		gfxContext.DrawIndexed(m_quad.indicesPerInstance, 0, 0);
	}

	if (TemporalEffects::EnableTAA) TemporalEffects::ResolveImage(gfxContext);

	if (DepthOfField::Enable)
		DepthOfField::Render(gfxContext, m_Camera.GetNearClip(), m_Camera.GetFarClip());
	else
		MotionBlur::RenderObjectBlur(gfxContext, g_VelocityBuffer);

#ifdef GROUND_TRUTH
	printf("frame %d\n", frameId);
#endif
	hasGeometryChange = false;

	if (!TemporalEffects::EnableTAA) FXAA::Enable = slcRenderer.m_Filtering;

	gfxContext.Finish();
}
