#pragma once
#include "Camera.h"
#include "pch.h"
struct ViewConfig
{
	Math::Camera m_Camera;
	D3D12_VIEWPORT m_MainViewport;
	D3D12_RECT m_MainScissor;
	ViewConfig(const Math::Camera& camera, const D3D12_VIEWPORT& mainViewport, const D3D12_RECT& mainScissor) :
		m_Camera(camera), m_MainViewport(mainViewport), m_MainScissor(mainScissor) {};
};