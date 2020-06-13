//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

#pragma once

#include "Device.h"
#include "SwapChain.h"
#include "CommandQueue.h"

#include "../Application/Platform.h" // FGPUInfo
#include "../Application/Settings.h"
#include "../Application/Types.h"

#include <vector>
#include <unordered_map>
#include <array>

class Window;

// Data to be updated per frame
struct FFrameData
{
	std::array<float, 4> SwapChainClearColor;
};
struct FLoadingScreenData
{
	std::array<float, 4> SwapChainClearColor;
	// TODO: loading screen background img resource
	// TODO: animation resources
};

struct FRendererInitializeParameters
{
	std::vector<FWindowRepresentation> Windows;
	FGraphicsSettings                  Settings;
};



class VQRenderer
{
public:
	static std::vector< FGPUInfo > EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters = false);

public:
	void Initialize(const FRendererInitializeParameters& RendererInitParams);
	void Exit();

	void RenderWindowContext(HWND hwnd, const FFrameData& FrameData);

	inline short GetSwapChainBackBufferCountOfWindow(std::unique_ptr<Window>& pWnd) const { return GetSwapChainBackBufferCountOfWindow(pWnd.get()); };
	short GetSwapChainBackBufferCountOfWindow(Window* pWnd) const;
	short GetSwapChainBackBufferCountOfWindow(HWND hwnd) const;

private:
	// Private Functions go here


private:
	// RenderWindowContext struct encapsulates the swapchain and window association
	// - Each SwapChain is associated with a Window (HWND)
	// - Each SwapChain is associated with a Graphics Queue for Present()
	// - Each SwapChain is created with a Device
	struct FRenderWindowContext
	{
		Device*      pDevice = nullptr;
		SwapChain    SwapChain;
		CommandQueue PresentQueue;


		// 1x allocator per command-recording-thread, multiplied by num swapchain backbuffers
		// Source: https://gpuopen.com/performance/
		std::vector<ID3D12CommandAllocator*> mCommandAllocatorsGFX;
		std::vector<ID3D12CommandAllocator*> mCommandAllocatorsCompute;
		std::vector<ID3D12CommandAllocator*> mCommandAllocatorsCopy;


		ID3D12GraphicsCommandList* pCmdList_GFX = nullptr;

		bool bVsync = false;
	};

private:
	Device mDevice; // GPU

	std::unordered_map<HWND, FRenderWindowContext> mRenderContextLookup;

	CommandQueue mGFXQueue;
	CommandQueue mComputeQueue;
	CommandQueue mCopyQueue;

};
