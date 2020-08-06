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

#include "SLCDemo.h"

int wmain(int argc, wchar_t** argv)
{
#if _DEBUG
	CComPtr<ID3D12Debug> debugInterface;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface))))
	{
		debugInterface->EnableDebugLayer();
	}
#endif

	UUID experimentalShadersFeatures[] = { D3D12ExperimentalShaderModels };
	struct Experiment { UUID *Experiments; UINT numFeatures; };
	Experiment experiments[] = {
		{ experimentalShadersFeatures, ARRAYSIZE(experimentalShadersFeatures) },
		{ nullptr, 0 },
	};

	CComPtr<ID3D12Device> pDevice;
	CComPtr<IDXGIAdapter1> pAdapter;
	CComPtr<IDXGIFactory2> pFactory;
	CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory));
	bool validDeviceFound = false;
	for (auto &experiment : experiments)
	{
		if (SUCCEEDED(D3D12EnableExperimentalFeatures(experiment.numFeatures, experiment.Experiments, nullptr, nullptr)))
		{
			for (uint32_t Idx = 0; !validDeviceFound && DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(Idx, &pAdapter); ++Idx)
			{
				DXGI_ADAPTER_DESC1 desc;
				pAdapter->GetDesc1(&desc);
				if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
				{
					validDeviceFound = SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice)));
				}
				pAdapter = nullptr;
			}
			if (validDeviceFound) break;
		}
	}

	s_EnableVSync.Decrement();

	// parse scene file

	std::string sceneFile = "SceneDescription_cornell.xml";
	if (argc > 1)
	{
		std::wstring ws(argv[1]);
		sceneFile = std::string(ws.begin(), ws.end());
	}

	int imgWidth;
	int imgHeight;
	std::vector<std::string> modelPaths;
	Camera camera;
	bool isCameraInitialized;
	SunLightConfig sunLightConfig;
	float exposure;
	std::shared_ptr<SimpleAnimation> animation = nullptr;
	bool isVPLScene = false;

	if (ParseSceneFile(sceneFile, modelPaths, isVPLScene, camera, isCameraInitialized,
		sunLightConfig, imgWidth, imgHeight, exposure, animation))
	{
		g_CustomResolutionX = imgWidth;
		g_CustomResolutionY = imgHeight;
		g_DisplayWidth = imgWidth;
		g_DisplayHeight = imgHeight;
		camera.SetAspectRatio(g_DisplayHeight / (float)g_DisplayWidth);
		GameCore::RunApplication(SLCDemo(modelPaths, isVPLScene, isCameraInitialized, camera, sunLightConfig, exposure, animation), L"SLCDemo");
	}
	else
	{
		printf("Error happens in reading scene files. Fall back to the default Crytek Sponza scene\n");
		g_CustomResolutionX = 1920;
		g_CustomResolutionY = 1080;
		GameCore::RunApplication(SLCDemo(), L"SLCDemo");
	}


	return 0;
}
