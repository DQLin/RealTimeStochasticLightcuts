#pragma once

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


//#define FRUSTUM_CULLING

#include <atlbase.h>
#include "dxgi1_3.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "CameraController.h"
#include "BufferManager.h"
#include "Camera.h"
#include "ModelLoader.h"
#include "GpuBuffer.h"
#include "ReadbackBuffer.h"
#include "CommandContext.h"
#include "SamplerManager.h"
#include "TemporalEffects.h"
#include "MotionBlur.h"
#include "DepthOfField.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "FXAA.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "ShadowCamera.h"
#include "ParticleEffectManager.h"
#include "GameInput.h"
#include "Quad.h"

#include "SLCRenderer.h"
#include "RaytracingHlslCompat.h"
#include "SimpleAnimation.h"

#include <chrono>

using namespace GameCore;
using namespace Math;
using namespace Graphics;


struct SunLightConfig
{
	bool hasAnimation = false;
	float lightIntensity;
	float inclinationAnimFreq;
	float inclinationAnimPhase;
	float orientationAnimFreq;
	float orientationAnimPhase;
};

bool ParseSceneFile(const std::string SceneFile, std::vector<std::string>& modelPaths, bool& isVPLScene, Camera& m_Camera, bool& isCameraInitialized,
	SunLightConfig& sunLightConfig, int& imgWidth, int &imgHeight, float &exposure, std::shared_ptr<SimpleAnimation>& animation);

class SLCDemo : public GameCore::IGameApp
{
public:

	SLCDemo(void) { m_Models.resize(1); PostEffects::Exposure = 8.0; }
	SLCDemo(const std::vector<std::string>& modelFiles, bool isVPLScene, bool isCameraInitialized, const Camera& camera,
		const SunLightConfig& sunLightConfig, float exposure, std::shared_ptr<SimpleAnimation> animation);

	virtual void Startup(void) override;
	virtual void Cleanup(void) override;

	virtual void Update(float deltaT) override;
	virtual void RenderScene(void) override;

private:

	__declspec(align(16)) struct ModelViewerConstants
	{
		Vector3 sunDirection;
		Vector3 sunLight;
		Vector3 diffuseColor;
		Vector3 specularColor;
		Vector3 emissionColor;
		Vector3 viewerPos;
		int hasEmissiveTexture;
		int textureFlags;
		glm::vec2 screenDimension;
		float zMagic;
	};

	enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };
	void RenderObjects(GraphicsContext& gfxContext, int modelId, const Matrix4& ViewProjMat, ModelViewerConstants psConstants, eObjectFilter Filter = kAll, bool renderShadowMap = false);
	void GetSubViewportAndScissor(int i, int j, int rate, D3D12_VIEWPORT& viewport, D3D12_RECT& scissor);
	void IntegrateEmissiveTriangles(GraphicsContext& gfxContext);

#ifdef FRUSTUM_CULLING
	struct ViewFrustum
	{
		glm::vec4 plane[6];
	};

	ViewFrustum frustum;

	void CalculateFrustum()
	{
		glm::mat4 M;
		memcpy(&M, &m_Camera.GetViewProjMatrix(), 64);
		frustum.plane[0] = glm::vec4(M[0][3] + M[0][0], M[1][3] + M[1][0], M[2][3] + M[2][0], M[3][3] + M[3][0]);
		frustum.plane[1] = glm::vec4(M[0][3] - M[0][0], M[1][3] - M[1][0], M[2][3] - M[2][0], M[3][3] - M[3][0]);
		frustum.plane[2] = glm::vec4(M[0][3] + M[0][1], M[1][3] + M[1][1], M[2][3] + M[2][1], M[3][3] + M[3][1]);
		frustum.plane[3] = glm::vec4(M[0][3] - M[0][1], M[1][3] - M[1][1], M[2][3] - M[2][1], M[3][3] - M[3][1]);
		frustum.plane[4] = glm::vec4(		  M[0][2],			 M[1][2],		    M[2][2],		   M[3][2]);
		frustum.plane[5] = glm::vec4(M[0][3] - M[0][2], M[1][3] - M[1][2], M[2][3] - M[2][2], M[3][3] - M[3][2]);
	}

	bool IsBoxInFrustum(const Model1::BoundingBox& box)
	{
		for (int i = 0; i < 6; i++)
		{
			int out = 0;
			out += ((dot(frustum.plane[i], glm::vec4((float)box.min.GetX(), (float)box.min.GetY(), (float)box.min.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			out += ((dot(frustum.plane[i], glm::vec4((float)box.max.GetX(), (float)box.min.GetY(), (float)box.min.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			out += ((dot(frustum.plane[i], glm::vec4((float)box.min.GetX(), (float)box.max.GetY(), (float)box.min.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			out += ((dot(frustum.plane[i], glm::vec4((float)box.max.GetX(), (float)box.max.GetY(), (float)box.min.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			out += ((dot(frustum.plane[i], glm::vec4((float)box.min.GetX(), (float)box.min.GetY(), (float)box.max.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			out += ((dot(frustum.plane[i], glm::vec4((float)box.max.GetX(), (float)box.min.GetY(), (float)box.max.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			out += ((dot(frustum.plane[i], glm::vec4((float)box.min.GetX(), (float)box.max.GetY(), (float)box.max.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			out += ((dot(frustum.plane[i], glm::vec4((float)box.max.GetX(), (float)box.max.GetY(), (float)box.max.GetZ(), 1.0f)) < 0.0) ? 1 : 0);
			if (out == 8) return false;
		}
		return true;
	}
#endif

	std::vector<std::string> m_ModelFiles;
	bool m_IsCameraInitialized = false;

	Camera m_Camera;
	std::auto_ptr<CameraController> m_CameraController;
	Matrix4 m_ViewProjMatrix;
	D3D12_VIEWPORT m_MainViewport;
	D3D12_RECT m_MainScissor;

	RootSignature m_RootSig;
	RootSignature m_ComputeRootSig;
	GraphicsPSO m_EmissiveIntegrationPSO;
	ColorBuffer m_DummyColorBuffer;
	DepthBuffer m_DummyDepthBuffer;
	GraphicsPSO m_DepthPSO;
	GraphicsPSO m_CutoutDepthPSO;
	GraphicsPSO m_ModelPSO;
	GraphicsPSO m_CutoutModelPSO;
	GraphicsPSO m_ShadowPSO;
	GraphicsPSO m_CutoutShadowPSO;
	GraphicsPSO m_ScreenPSO;
	GraphicsPSO m_FillPSO;

	D3D12_CPU_DESCRIPTOR_HANDLE m_DefaultSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_BiasedDefaultSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_GBuffer[6];

	std::vector<Model1> m_Models;
	Quad  m_quad;

	SunLightConfig m_SunLightConfig;
	Vector4 m_SceneSphere;
	Vector3 m_SunDirection;
	ShadowCamera m_SunShadow;

	const int maxUpdateFrames = 1;

	int frameId;
	bool hasGeometryChange = false;

	int64_t initialTick = 0;

	SLCRenderer slcRenderer;
};