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

#define USE_PIX 1
#if USE_PIX 
	// Enable PIX markers for RGP, must be included before pix3.h
	#define VQE_ENABLE_RGP_PIX 0
#endif
#include "WinPixEventRuntime/Include/WinPixEventRuntime/pix3.h"

#define SCOPED_GPU_MARKER(pCmd, pStr)       ScopedGPUMarker GPUMarker(pCmd,pStr)
#define SCOPED_CPU_MARKER(pStr)             ScopedMarker    CPUMarker(pStr)
#define SCOPED_CPU_MARKER_C(pStr, PIXColor) ScopedMarker    CPUMarker(pStr, PIXColor)

struct ID3D12GraphicsCommandList;
struct ID3D12CommandQueue;


class ScopedMarker
{
public:
	ScopedMarker(const char* pLabel, unsigned PIXColor = PIX_COLOR_DEFAULT);
	~ScopedMarker();
};

class ScopedGPUMarker// : public ScopedMarker
{
public:
	ScopedGPUMarker(ID3D12GraphicsCommandList* pCmdList, const char* pLabel, unsigned PIXColor = PIX_COLOR_DEFAULT);
	ScopedGPUMarker(ID3D12CommandQueue*       pCmdQueue, const char* pLabel, unsigned PIXColor = PIX_COLOR_DEFAULT);
	~ScopedGPUMarker();

	ScopedGPUMarker(const ScopedGPUMarker&)       = delete;
	ScopedGPUMarker(ScopedGPUMarker&&)            = delete;
	ScopedGPUMarker& operator=(ScopedGPUMarker&&) = delete;
private:
	union 
	{
		ID3D12GraphicsCommandList* mpCmdList;
		ID3D12CommandQueue* mpCmdQueue;
	};
};

