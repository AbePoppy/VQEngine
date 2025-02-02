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

#include "VQEngine.h"
#include "Geometry.h"
#include "GPUMarker.h"

#include <d3d12.h>
#include <dxgi.h>

#include "RenderPass/AmbientOcclusion.h"
#include "RenderPass/DepthPrePass.h"

#include "VQEngine_RenderCommon.h"

#include "imgui.h"
//
// DRAW COMMANDS
//
void VQEngine::DrawMesh(ID3D12GraphicsCommandList* pCmd, const Mesh& mesh)
{
	SCOPED_CPU_MARKER("DrawMesh");
	using namespace DirectX;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	// Draw Object -----------------------------------------------
	const auto VBIBIDs = mesh.GetIABufferIDs();
	const uint32 NumIndices = mesh.GetNumIndices();
	const uint32 NumInstances = 1;
	const BufferID& VB_ID = VBIBIDs.first;
	const BufferID& IB_ID = VBIBIDs.second;
	const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
	const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, &vb);
	pCmd->IASetIndexBuffer(&ib);

	pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
}

void VQEngine::DrawShadowViewMeshList(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView::FShadowView& shadowView)
{
	using namespace DirectX;
	struct FCBufferLightVS
	{
		XMMATRIX matWorldViewProj;
		XMMATRIX matWorld;
	};

	for (const FShadowMeshRenderCommand& renderCmd : shadowView.meshRenderCommands)
	{
		SCOPED_CPU_MARKER("Process_ShadowMeshRenderCommand");
		// set constant buffer data
		FCBufferLightVS* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);
		pCBuffer->matWorldViewProj = renderCmd.matWorldViewProj;
		pCBuffer->matWorld = renderCmd.matWorldTransformation;
		pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

		const Mesh& mesh = mpScene->mMeshes.at(renderCmd.meshID);
		DrawMesh(pCmd, mesh);
	}
}


//
// RENDER PASSES
//
void VQEngine::RenderDirectionalShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& SceneShadowView)
{
	SCOPED_GPU_MARKER(pCmd, "RenderDirectionalShadowMaps");

	if (!SceneShadowView.ShadowView_Directional.meshRenderCommands.empty())
	{
		const std::string marker = "Directional";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(7));

		const float RenderResolutionX = 2048.0f; // TODO
		const float RenderResolutionY = 2048.0f; // TODO
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
		D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		// Bind Depth / clear
		const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Directional);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		//if constexpr (!B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW)
		{
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);
		}

		DrawShadowViewMeshList(pCmd, pCBufferHeap, SceneShadowView.ShadowView_Directional);
	}
}
void VQEngine::RenderSpotShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& SceneShadowView)
{
	SCOPED_GPU_MARKER(pCmd, "RenderSpotShadowMaps");
	const bool bRenderAtLeastOneSpotShadowMap = SceneShadowView.NumSpotShadowViews > 0;

	// Set Viewport & Scissors
	const float RenderResolutionX = 1024.0f; // TODO
	const float RenderResolutionY = 1024.0f; // TODO
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);
	
	if (!bRenderAtLeastOneSpotShadowMap)
		return;


	//
	// SPOT LIGHTS
	//
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(7));
	
	for (uint i = 0; i < SceneShadowView.NumSpotShadowViews; ++i)
	{
		const FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[i];

		if (ShadowView.meshRenderCommands.empty())
			continue;

		const std::string marker = "Spot[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		// Bind Depth / clear
		const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Spot);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle(i);
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
		pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);

		DrawShadowViewMeshList(pCmd, pCBufferHeap, ShadowView);
	}
}
void VQEngine::RenderPointShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& SceneShadowView, size_t iBegin, size_t NumPointLights)
{
	SCOPED_GPU_MARKER(pCmd, "RenderPointShadowMaps");
	if (SceneShadowView.NumPointShadowViews <= 0)
		return;

	using namespace DirectX;
	struct FCBufferLightPS
	{
		XMFLOAT3 vLightPos;
		float fFarPlane;
	};
	
#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	// Set Viewport & Scissors
	const float RenderResolutionX = 1024.0f; // TODO
	const float RenderResolutionY = 1024.0f; // TODO
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);
#endif

	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_LINEAR_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(8));
	for (size_t i = iBegin; i < iBegin + NumPointLights; ++i)
	{
		const std::string marker = "Point[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		FCBufferLightPS* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);

		pCBuffer->vLightPos = SceneShadowView.PointLightLinearDepthParams[i].vWorldPos;
		pCBuffer->fFarPlane = SceneShadowView.PointLightLinearDepthParams[i].fFarPlane;
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		for (size_t face = 0; face < 6; ++face)
		{
			const size_t iShadowView = i * 6 + face;
			const FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Point[iShadowView];

			if (ShadowView.meshRenderCommands.empty())
				continue;

			const std::string marker_face = "[Cubemap Face=" + std::to_string(face) + "]";
			SCOPED_GPU_MARKER(pCmd, marker_face.c_str());

			// Bind Depth / clear
			const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Point);
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle((uint32_t)iShadowView);
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);

			// draw render list
			DrawShadowViewMeshList(pCmd, pCBufferHeap, ShadowView);
		}
	}
}

void VQEngine::RenderDepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView)
{
	using namespace VQ_SHADER_DATA;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	const auto& rsc = mResources_MainWnd;
	auto pRscNormals     = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= mRenderer.GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = mRenderer.GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = mRenderer.GetTextureResource(rsc.Tex_SceneDepth);

	if (!bMSAA)
	{
		std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
	}

	const DSV& dsvColor   = mRenderer.GetDSV(bMSAA ? mResources_MainWnd.DSV_SceneDepthMSAA : mResources_MainWnd.DSV_SceneDepth);
	const RTV& rtvNormals = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneNormalsMSAA : mResources_MainWnd.RTV_SceneNormals);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvNormals.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();


	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };


	SCOPED_GPU_MARKER(pCmd, "RenderDepthPrePass");

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::DEPTH_PREPASS_PSO_MSAA_4 : EBuiltinPSOs::DEPTH_PREPASS_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(14)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented

	// draw meshes
	for (const FMeshRenderCommand& meshRenderCmd : SceneView.meshRenderCommands)
	{
		if (mpScene->mMeshes.find(meshRenderCmd.meshID) == mpScene->mMeshes.end())
		{
			Log::Warning("MeshID=%d couldn't be found", meshRenderCmd.meshID);
			continue; // skip drawing this mesh
		}

		const Material& mat = mpScene->GetMaterial(meshRenderCmd.matID);
		const Mesh& mesh = mpScene->mMeshes.at(meshRenderCmd.meshID);
		const auto VBIBIDs = mesh.GetIABufferIDs();
		const uint32 NumIndices = mesh.GetNumIndices();
		const uint32 NumInstances = 1;
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);


		// set constant buffer data
		PerObjectData* pPerObj = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pPerObj)), (void**)(&pPerObj), &cbAddr);

		pPerObj->matWorldViewProj = meshRenderCmd.matWorldTransformation * SceneView.viewProj;
		pPerObj->matWorld = meshRenderCmd.matWorldTransformation;
		pPerObj->matNormal = meshRenderCmd.matNormalTransformation;
		pPerObj->materialData = std::move(mat.GetCBufferData());

		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		// set textures
		if (mat.SRVMaterialMaps != INVALID_ID)
		{
			pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
			//pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
		}

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}

	// resolve if MSAA

	if (bMSAA)
	{
		DXGI_FORMAT fmtDepth = DXGI_FORMAT_D32_FLOAT;

		// transition for resolve
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
				  CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA , D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals     , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)
				
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA   , D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);

		// resolve
		{
			ResolveDepth(pCmd, pCBufferHeap, rsc.Tex_SceneDepthMSAA, rsc.SRV_SceneDepthMSAA, rsc.UAV_SceneDepth);
		}
		{
			SCOPED_GPU_MARKER(pCmd, "ResolveSceneNormalsMSAA");
			pCmd->ResolveSubresource(pRscNormals, 0, pRscNormalsMSAA, 0, mRenderer.GetTextureFormat(mResources_MainWnd.Tex_SceneNormalsMSAA));
		}
	}

	// transition for shader read
	{
		std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
		if (bMSAA)
		{
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE           , D3D12_RESOURCE_STATE_RENDER_TARGET));
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA  , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_UNORDERED_ACCESS         , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		}
		else
		{
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE));
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
		}

		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, 
			bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		));

		pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());


		Barriers.clear();

		// FFX-CACAO expects TexDepthResolve (UAV) to contain depth buffer data, but we've written to TexDepth (DSV) when !bMSAA.
		// Here we just copy TexDepth into TexDepthResolve to keep MSAA-toggle functionality on the fly with Key M.
		if (!bMSAA)
		{
			pCmd->CopyResource(pRscDepthResolve, pRscDepth);
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
			pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
		}
	}
}

void VQEngine::RenderAmbientOcclusion(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView)
{
	const auto& rsc = mResources_MainWnd;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	ID3D12Resource* pRscAmbientOcclusion = mRenderer.GetTextureResource(rsc.Tex_AmbientOcclusion);
	const UAV& uav = mRenderer.GetUAV(rsc.UAV_FFXCACAO_Out);

	const char* pStrPass[FAmbientOcclusionPass::EMethod::NUM_AMBIENT_OCCLUSION_METHODS] =
	{
		"Ambient Occlusion (FidelityFX CACAO)"
	};
	SCOPED_GPU_MARKER(pCmd, pStrPass[mRenderPass_AO.Method]);
	
	static bool sbScreenSpaceAO_Previous = false;
	const bool bSSAOToggledOff = sbScreenSpaceAO_Previous && !SceneView.sceneParameters.bScreenSpaceAO;

	if (SceneView.sceneParameters.bScreenSpaceAO)
	{
		FAmbientOcclusionPass::FDrawParameters drawParams = {};
		DirectX::XMStoreFloat4x4(&drawParams.matNormalToView, SceneView.view);
		DirectX::XMStoreFloat4x4(&drawParams.matProj, SceneView.proj);
		drawParams.pCmd = pCmd;

		// TODO: move into RecordCommands?
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		mRenderPass_AO.RecordCommands(&drawParams);

		// set back the descriptor heap for the VQEngine
		ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	}
	
	// clear UAV only once
	if(bSSAOToggledOff)
	{
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		const FLOAT clearValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		pCmd->ClearUnorderedAccessViewFloat(uav.GetGPUDescHandle(), uav.GetCPUDescHandle(), pRscAmbientOcclusion, clearValue, 0, NULL);
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
	}

	sbScreenSpaceAO_Previous = SceneView.sceneParameters.bScreenSpaceAO;
}

void VQEngine::TransitionForSceneRendering(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	const auto& rsc = mResources_MainWnd;
	auto pRscColor           = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	auto pRscColorMSAA       = mRenderer.GetTextureResource(rsc.Tex_SceneColorMSAA);
	auto pRscNormals         = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA     = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscShadowMaps_Spot = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	auto pRscShadowMaps_Point= mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Point);
	auto pRscShadowMaps_Directional = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForSceneRendering");

	CD3DX12_RESOURCE_BARRIER ColorTransition = bMSAA
		? CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		: CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	std::vector<CD3DX12_RESOURCE_BARRIER> vBarriers;
	vBarriers.push_back(ColorTransition);
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	pCmd->ResourceBarrier((UINT)vBarriers.size(), vBarriers.data());
}

void VQEngine::RenderSceneColor(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView)
{
	using namespace VQ_SHADER_DATA;

	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool bHasEnvironmentMapHDRTexture = mResources_MainWnd.EnvironmentMap.SRV_HDREnvironment != INVALID_ID;
	const bool bDrawEnvironmentMap = bHasEnvironmentMapHDRTexture && true;

	using namespace DirectX;

	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	const RTV& rtvColor = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneColorMSAA : mResources_MainWnd.RTV_SceneColor);
	const DSV& dsvColor = mRenderer.GetDSV(bMSAA ? mResources_MainWnd.DSV_SceneDepthMSAA : mResources_MainWnd.DSV_SceneDepth);
	auto pRscDepth = mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneDepth);

	SCOPED_GPU_MARKER(pCmd, "RenderSceneColor");

	// Clear Depth & Render targets
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvColor.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);


	// Set Viewport & Scissors
	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::FORWARD_LIGHTING_PSO_MSAA_4 : EBuiltinPSOs::FORWARD_LIGHTING_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(5)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented

	// set PerFrame constants
	{
		const CBV_SRV_UAV& NullCubemapSRV = mRenderer.GetSRV(mResources_MainWnd.SRV_NullCubemap);
		const CBV_SRV_UAV& NullTex2DSRV   = mRenderer.GetSRV(mResources_MainWnd.SRV_NullTexture2D);

		constexpr UINT PerFrameRSBindSlot = 3;
		PerFrameData* pPerFrame = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(PerFrameData), (void**)(&pPerFrame), &cbAddr);

		assert(pPerFrame);
		pPerFrame->Lights = SceneView.GPULightingData;
		pPerFrame->fAmbientLightingFactor = SceneView.sceneParameters.fAmbientLightingFactor;
		pPerFrame->f2PointLightShadowMapDimensions = { 1024.0f, 1024.f }; // TODO
		pPerFrame->f2SpotLightShadowMapDimensions  = { 1024.0f, 1024.f }; // TODO
		pPerFrame->f2DirectionalLightShadowMapDimensions  = { 2048.0f, 2048.0f }; // TODO
		
		if (bUseHDRRenderPath)
		{
			// adjust ambient factor as the tonemapper changes the output curve for HDR displays 
			// and makes the ambient lighting too strong.
			pPerFrame->fAmbientLightingFactor *= 0.005f;
		}

		//pCmd->SetGraphicsRootDescriptorTable(PerFrameRSBindSlot, );
		pCmd->SetGraphicsRootConstantBufferView(PerFrameRSBindSlot, cbAddr);
		pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Spot).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(5, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Point).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(6, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Directional).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(7, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceDiffBlurred).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(8, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceSpec).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(9, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_BRDFIntegrationLUT).GetGPUDescHandle()
			: NullTex2DSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(10, mRenderer.GetSRV(mResources_MainWnd.SRV_FFXCACAO_Out).GetGPUDescHandle());
	}

	// set PerView constants
	{
		constexpr UINT PerViewRSBindSlot = 2;
		PerViewData* pPerView = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pPerView)), (void**)(&pPerView), &cbAddr);

		assert(pPerView);
		XMStoreFloat3(&pPerView->CameraPosition, SceneView.cameraPosition);
		pPerView->ScreenDimensions.x = RenderResolutionX;
		pPerView->ScreenDimensions.y = RenderResolutionY;

		// TODO: PreView data

		//pCmd->SetGraphicsRootDescriptorTable(PerViewRSBindSlot, D3D12_GPU_DESCRIPTOR_HANDLE{ cbAddr });
		pCmd->SetGraphicsRootConstantBufferView(PerViewRSBindSlot, cbAddr);
	}


	// Draw Objects -----------------------------------------------
	//if(!SceneView.meshRenderCommands.empty())
	{
		constexpr UINT PerObjRSBindSlot = 1;
		SCOPED_GPU_MARKER(pCmd, "Geometry");

		for (const FMeshRenderCommand& meshRenderCmd : SceneView.meshRenderCommands)
		{
			const Material& mat = mpScene->GetMaterial(meshRenderCmd.matID);

			// set constant buffer data
			PerObjectData* pPerObj = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pPerObj)), (void**)(&pPerObj), &cbAddr);


			pPerObj->matWorldViewProj = meshRenderCmd.matWorldTransformation * SceneView.viewProj;
			pPerObj->matWorld         = meshRenderCmd.matWorldTransformation;
			pPerObj->matNormal        = meshRenderCmd.matNormalTransformation;
			pPerObj->materialData = std::move(mat.GetCBufferData());

			pCmd->SetGraphicsRootConstantBufferView(PerObjRSBindSlot, cbAddr);

			// set textures
			if (mat.SRVMaterialMaps != INVALID_ID)
			{
				pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
				//pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));

			}

			// draw mesh
			if (mpScene->mMeshes.find(meshRenderCmd.meshID) == mpScene->mMeshes.end())
			{
				Log::Warning("MeshID=%d couldn't be found", meshRenderCmd.meshID);
				continue; // skip drawing this mesh
			}

			const Mesh& mesh = mpScene->mMeshes.at(meshRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Light bounds ------------------------------------------
	if(!SceneView.lightBoundsRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "LightBounds");
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA 
			? EBuiltinPSOs::WIREFRAME_PSO_MSAA_4 
			: EBuiltinPSOs::WIREFRAME_PSO)
		);

		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(6)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented
		for (const FLightRenderCommand& lightBoundRenderCmd : SceneView.lightBoundsRenderCommands)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color            = lightBoundRenderCmd.color;
			pCBuffer->matModelViewProj = lightBoundRenderCmd.matWorldTransformation * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

			// set IA
			const Mesh& mesh = mpScene->mMeshes.at(lightBoundRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			// draw
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Light Meshes ------------------------------------------
	if(!SceneView.lightRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "Lights");
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA
			? EBuiltinPSOs::UNLIT_PSO_MSAA_4
			: EBuiltinPSOs::UNLIT_PSO)
		);
		if(SceneView.lightBoundsRenderCommands.empty())
			pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(6)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented

		for (const FLightRenderCommand& lightRenderCmd : SceneView.lightRenderCommands)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color            = lightRenderCmd.color;
			pCBuffer->matModelViewProj = lightRenderCmd.matWorldTransformation * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

			// set IA
			const Mesh& mesh = mpScene->mMeshes.at(lightRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			// draw
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Bounding Boxes ----------------------------------------
	if (!SceneView.boundingBoxRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "BoundingBoxes");

		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA
			? EBuiltinPSOs::WIREFRAME_PSO_MSAA_4
			: EBuiltinPSOs::WIREFRAME_PSO)
		);
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(6)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented


		// set IA
		const Mesh& mesh = mpScene->mMeshes.at(SceneView.boundingBoxRenderCommands.begin()->meshID);

		const auto VBIBIDs = mesh.GetIABufferIDs();
		const uint32 NumIndices = mesh.GetNumIndices();
		const uint32 NumInstances = 1;
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		for(const FBoundingBoxRenderCommand& BBRenderCmd: SceneView.boundingBoxRenderCommands)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = BBRenderCmd.color;
			pCBuffer->matModelViewProj = BBRenderCmd.matWorldTransformation * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Environment Map ---------------------------------------
	if (bDrawEnvironmentMap)
	{
		SCOPED_GPU_MARKER(pCmd, "EnvironmentMap");

		ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

		Camera skyCam = mpScene->GetActiveCamera().Clone();
		FCameraParameters p = {};
		p.bInitializeCameraController = false;
		p.ProjectionParams = skyCam.GetProjectionParameters();
		p.ProjectionParams.bPerspectiveProjection = true;
		p.ProjectionParams.FieldOfView = p.ProjectionParams.FieldOfView * RAD2DEG; // TODO: remove the need for this conversion
		p.x = p.y = p.z = 0;
		p.Yaw   = SceneView.MainViewCameraYaw   * RAD2DEG;
		p.Pitch = SceneView.MainViewCameraPitch * RAD2DEG;
		skyCam.InitializeCamera(p);

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		FFrameConstantBuffer * pConstBuffer = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(FFrameConstantBuffer ), (void**)(&pConstBuffer), &cbAddr);
		pConstBuffer->matModelViewProj = skyCam.GetViewMatrix() * skyCam.GetProjectionMatrix();

		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::SKYDOME_PSO_MSAA_4 : EBuiltinPSOs::SKYDOME_PSO));

		// hardcoded root signature for now until shader reflection and rootsignature management is implemented
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(2));

		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_HDREnvironment).GetGPUDescHandle());
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		DrawMesh(pCmd, mBuiltinMeshes[EBuiltInMeshes::CUBE]);
	}
}

void VQEngine::ResolveMSAA(ID3D12GraphicsCommandList* pCmd)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	if (!bMSAA)
		return;

	SCOPED_GPU_MARKER(pCmd, "ResolveMSAA");
	auto pRscColor     = mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneColor);
	auto pRscColorMSAA = mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneColorMSAA);

	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
				  CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA , D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscColor     , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}

	pCmd->ResolveSubresource(pRscColor, 0, pRscColorMSAA, 0, mRenderer.GetTextureFormat(mResources_MainWnd.Tex_SceneColor));
}

void VQEngine::ResolveDepth(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, TextureID DepthTexture, SRV_ID SRVDepthTexture, UAV_ID UAVDepthResolveTexture)
{
	const SRV& srvMSAADepthSource = mRenderer.GetSRV(SRVDepthTexture);
	const UAV& uavDepthResolveTarget = mRenderer.GetUAV(UAVDepthResolveTexture);
	
	int W, H;
	int NumSamples = 4; // TODO: generic?
	mRenderer.GetTextureDimensions(DepthTexture, W, H);

	constexpr int DispatchGroupDimensionX = 8;
	constexpr int DispatchGroupDimensionY = 8;
	const     int DispatchX = (W + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
	const     int DispatchY = (H + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
	constexpr int DispatchZ = 1;

	struct FParams { uint params[4]; } CBuffer;
	CBuffer.params[0] = W;
	CBuffer.params[1] = H;
	CBuffer.params[2] = NumSamples; // MSAA Sample Count
	CBuffer.params[3] = 0xDEADC0DE;
	FParams* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	pCBufferHeap->AllocConstantBuffer(sizeof(FParams), (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = CBuffer;

	SCOPED_GPU_MARKER(pCmd, "ResolveDepth");
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_RESOLVE));
	pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(3));
	pCmd->SetComputeRootDescriptorTable(0, srvMSAADepthSource.GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(1, uavDepthResolveTarget.GetGPUDescHandle());
	pCmd->SetComputeRootConstantBufferView(2, cbAddr);
	pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
}

void VQEngine::TransitionForPostProcessing(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	const auto& rsc = mResources_MainWnd;

	const bool bCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.FFXCASParams.CASSharpen > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();

	auto pRscPostProcessInput = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	auto pRscTonemapperOut    = mRenderer.GetTextureResource(rsc.Tex_PostProcess_TonemapperOut);
	auto pRscFFXCASOut        = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FFXCASOut);
	auto pRscFSROut           = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut); // TODO: handle RCAS=off
	auto pRscPostProcessOut   = (bFSREnabled
		? pRscFSROut 
		: (bCASEnabled
			? pRscFFXCASOut 
			: pRscTonemapperOut)
	);

	auto pRscShadowMaps_Spot = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	auto pRscShadowMaps_Point = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Point);
	auto pRscShadowMaps_Directional = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForPostProcessing");
	std::vector<CD3DX12_RESOURCE_BARRIER> barriers =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscPostProcessInput , (bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscPostProcessOut   , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot       , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point      , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
	};
	if (bFSREnabled || bCASEnabled)
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void VQEngine::RenderPostProcess(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams)
{
	ID3D12DescriptorHeap*       ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());

	// pass io
	const SRV& srv_ColorIn       = mRenderer.GetSRV(mResources_MainWnd.SRV_SceneColor);
	const UAV& uav_TonemapperOut = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_TonemapperOut);
	const SRV& srv_TonemapperOut = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut);
	const UAV& uav_FFXCASOut     = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_FFXCASOut);
	const UAV& uav_FSR_EASUOut   = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_FSR_EASUOut);
	const SRV& srv_FSR_EASUOut   = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FSR_EASUOut);
	const UAV& uav_FSR_RCASOut   = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_FSR_RCASOut);
	const SRV& srv_FSR_RCASOut   = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FSR_RCASOut);


	constexpr bool PP_ENABLE_BLUR_PASS = false;

	// compute dispatch dimensions
	const int& InputImageWidth  = PPParams.SceneRTWidth;
	const int& InputImageHeight = PPParams.SceneRTHeight;
	assert(PPParams.SceneRTWidth != 0);
	assert(PPParams.SceneRTHeight != 0);
	constexpr int DispatchGroupDimensionX = 8;
	constexpr int DispatchGroupDimensionY = 8;
	const     int DispatchRenderX = (InputImageWidth  + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
	const     int DispatchRenderY = (InputImageHeight + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
	constexpr int DispatchZ = 1;

	// cmds
	SCOPED_GPU_MARKER(pCmd, "RenderPostProcess");
	{
		const SRV& srv_blurOutput         = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_BlurOutput);
		ID3D12Resource* pRscTonemapperOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);

		if constexpr (PP_ENABLE_BLUR_PASS && PPParams.bEnableGaussianBlur)
		{
			SCOPED_GPU_MARKER(pCmd, "BlurCS");
			const UAV& uav_BlurIntermediate = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_BlurIntermediate);
			const UAV& uav_BlurOutput       = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_BlurOutput);
			const SRV& srv_blurIntermediate = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_BlurIntermediate);
			auto pRscBlurIntermediate = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_BlurIntermediate);
			auto pRscBlurOutput       = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_BlurOutput);


			FPostProcessParameters::FBlurParams* pBlurParams = nullptr;

			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FBlurParams), (void**)&pBlurParams, &cbAddr);
			pBlurParams->iImageSizeX = PPParams.SceneRTWidth;
			pBlurParams->iImageSizeY = PPParams.SceneRTHeight;

			{
				SCOPED_GPU_MARKER(pCmd, "BlurX");
				pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO));
				pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(11));

				const int FFXDispatchGroupDimension = 16;
				const     int FFXDispatchX = (InputImageWidth  + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;
				const     int FFXDispatchY = (InputImageHeight + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;

				pCmd->SetComputeRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

				const CD3DX12_RESOURCE_BARRIER pBarriers[] =
				{
					  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
					, CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurOutput      , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

				};
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
			}


			{
				SCOPED_GPU_MARKER(pCmd, "BlurY");
				pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO));
				pCmd->SetComputeRootDescriptorTable(0, srv_blurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurOutput.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

				const CD3DX12_RESOURCE_BARRIER pBarriers[] =
				{
					  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurOutput      , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
					, CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
				};
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
			}
		}

		{
			SCOPED_GPU_MARKER(pCmd, "TonemapperCS");

			FPostProcessParameters::FTonemapper* pConstBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FTonemapper), (void**)&pConstBuffer, &cbAddr);
			*pConstBuffer = PPParams.TonemapperParams;

			pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::TONEMAPPER_PSO));
			pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(3)); // compute RS
			pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			pCmd->SetComputeRootDescriptorTable(0, PP_ENABLE_BLUR_PASS ? srv_blurOutput.GetGPUDescHandle() : srv_ColorIn.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);
		}

		if(PPParams.IsFFXCASEnabled() && PPParams.FFXCASParams.CASSharpen > 0.0f)
		{
			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			};
			pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);

			SCOPED_GPU_MARKER(pCmd, "FFX-CAS CS");

			unsigned* pConstBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			const size_t cbSize = sizeof(unsigned) * 8;
			pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
			memcpy(pConstBuffer, PPParams.FFXCASParams.CASConstantBlock, cbSize);
			

			ID3D12PipelineState* pPSO = mRenderer.GetPSO(EBuiltinPSOs::FFX_CAS_CS_PSO);
			assert(pPSO);
			pCmd->SetPipelineState(pPSO);
			pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(3)); // same root signature as tonemapper
			pCmd->SetComputeRootDescriptorTable(0, srv_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_FFXCASOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			
			// each FFX-CAS CS thread processes 4 pixels.
			// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
 			constexpr int CAS_WORKGROUP_WORK_DIMENSION = 16;
			const int CASDispatchX = (InputImageWidth  + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			const int CASDispatchY = (InputImageHeight + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			pCmd->Dispatch(CASDispatchX, CASDispatchY, DispatchZ);
		}

		if (PPParams.IsFSREnabled()) // FSR & CAS are mutually exclusive
		{
			if (bHDR)
			{
				// TODO: color conversion pass, barriers etc.
			}

			std::vector<CD3DX12_RESOURCE_BARRIER> pBarriers =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut
				, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
				)
			};
			pCmd->ResourceBarrier((UINT)pBarriers.size(), pBarriers.data());

			{
				SCOPED_GPU_MARKER(pCmd, "FSR-EASU CS");

				unsigned* pConstBuffer = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
				const size_t cbSize = sizeof(unsigned) * 16;
				pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
				memcpy(pConstBuffer, PPParams.FFSR_EASUParams.EASUConstantBlock, cbSize);

				ID3D12PipelineState* pPSO = mRenderer.GetPSO(EBuiltinPSOs::FFX_FSR1_EASU_CS_PSO);
				assert(pPSO);
				pCmd->SetPipelineState(pPSO);
				pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(15));
				pCmd->SetComputeRootDescriptorTable(0, srv_TonemapperOut.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_FSR_EASUOut.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);

				// each FSR-EASU CS thread processes 4 pixels.
				// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
				constexpr int WORKGROUP_WORK_DIMENSION = 16;
				const int DispatchX = (PPParams.DisplayResolutionWidth  + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				const int DispatchY = (PPParams.DisplayResolutionHeight + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
			}
			const bool bFFX_RCAS_Enabled = true; // TODO: drive with UI ?
			if (bFFX_RCAS_Enabled)
			{
				ID3D12Resource* pRscEASUOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_FSR_EASUOut);
				ID3D12Resource* pRscRCASOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_FSR_RCASOut);

				SCOPED_GPU_MARKER(pCmd, "FSR-RCAS CS");
				{
					std::vector<CD3DX12_RESOURCE_BARRIER> barriers;
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscEASUOut
						, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
					));
					pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
				}

				ID3D12PipelineState* pPSO = mRenderer.GetPSO(EBuiltinPSOs::FFX_FSR1_RCAS_CS_PSO);

				FPostProcessParameters::FFSR_RCAS* pConstBuffer = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
				pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FFSR_RCAS), (void**)&pConstBuffer, &cbAddr);
				*pConstBuffer = PPParams.FFSR_RCASParams;

				pCmd->SetPipelineState(pPSO);
				pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(15));
				pCmd->SetComputeRootDescriptorTable(0, srv_FSR_EASUOut.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_FSR_RCASOut.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);

				// each FSR-RCAS CS thread processes 4 pixels.
				// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
				constexpr int WORKGROUP_WORK_DIMENSION = 16;
				const int DispatchX = (PPParams.DisplayResolutionWidth + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				const int DispatchY = (PPParams.DisplayResolutionHeight + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

				{
					const CD3DX12_RESOURCE_BARRIER pBarriers[] =
					{
						CD3DX12_RESOURCE_BARRIER::Transition(pRscEASUOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
						CD3DX12_RESOURCE_BARRIER::Transition(pRscRCASOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
					};
					pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
				}

			}
		}
	}
	
}

void VQEngine::RenderUI(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	const float           RenderResolutionX = static_cast<float>(PPParams.DisplayResolutionWidth);
	const float           RenderResolutionY = static_cast<float>(PPParams.DisplayResolutionHeight);
	D3D12_VIEWPORT                  viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	const auto                      VBIBIDs = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABufferIDs();
	const BufferID&                   IB_ID = VBIBIDs.second;
	const IBV&                        ib    = mRenderer.GetIndexBufferView(IB_ID);
	ID3D12DescriptorHeap*         ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	D3D12_RECT                 scissorsRect { 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	SwapChain&                    swapchain = ctx.SwapChain;

	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.FFXCASParams.CASSharpen > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();
	const SRV& srv_ColorIn = bFFXCASEnabled 
		? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FFXCASOut)
		: (bFSREnabled 
			? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FSR_RCASOut) 
			: mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut));

	ID3D12Resource* pRscFSR1Out       = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_FSR_RCASOut);
	ID3D12Resource* pRscTonemapperOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);
	ID3D12Resource* pRscFFXCASOut     = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_FFXCASOut);
	ID3D12Resource* pRscUI            = mRenderer.GetTextureResource(mResources_MainWnd.Tex_UI_SDR);
	ID3D12Resource* pRscInput = bFFXCASEnabled ? pRscFFXCASOut : (bFSREnabled ? pRscFSR1Out : pRscTonemapperOut);

	ID3D12Resource*          pSwapChainRT = swapchain.GetCurrentBackBufferRenderTarget();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = bHDR 
		? mRenderer.GetRTV(mResources_MainWnd.RTV_UI_SDR).GetCPUDescHandle()
		: swapchain.GetCurrentBackBufferRTVHandle();

	// barriers
	{	
		// Transition Input & Output resources
		// ignore the tonemapper barrier if CAS is enabeld as it'll already be issued.
		CD3DX12_RESOURCE_BARRIER SwapChainTransition = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		CD3DX12_RESOURCE_BARRIER UITransition = CD3DX12_RESOURCE_BARRIER::Transition(pRscUI, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(bHDR ? UITransition : SwapChainTransition);
		if (bFFXCASEnabled)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscFFXCASOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
		}
		else
		{
			if (bFSREnabled)
			{
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			}
			else
			{
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			}
		}
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	if(!bHDR)
	{
		SCOPED_GPU_MARKER(pCmd, "SwapchainPassthrough");
		pCmd->SetPipelineState(mRenderer.GetPSO(bHDR ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(1)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, NULL);
		pCmd->IASetIndexBuffer(&ib);

		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

		pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);
	}
	else
	{
		pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
		const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
	}
#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	{
		SCOPED_GPU_MARKER(pCmd, "UI");

		struct cb
		{
			float matTransformation[4][4];
		};

		ImGuiIO& io = ImGui::GetIO();

		ImGui::Render();

		ImDrawData* draw_data = ImGui::GetDrawData();

		// Create and grow vertex/index buffers if needed
		char* pVertices = NULL;
		D3D12_VERTEX_BUFFER_VIEW VerticesView;

		pCBufferHeap->AllocVertexBuffer(draw_data->TotalVtxCount, sizeof(ImDrawVert), (void**)&pVertices, &VerticesView);

		char* pIndices = NULL;
		D3D12_INDEX_BUFFER_VIEW IndicesView;
		pCBufferHeap->AllocIndexBuffer(draw_data->TotalIdxCount, sizeof(ImDrawIdx), (void**)&pIndices, &IndicesView);

		ImDrawVert* vtx_dst = (ImDrawVert*)pVertices;
		ImDrawIdx* idx_dst = (ImDrawIdx*)pIndices;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtx_dst += cmd_list->VtxBuffer.Size;
			idx_dst += cmd_list->IdxBuffer.Size;
		}

		// Setup orthographic projection matrix into our constant buffer
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		{
			cb* constant_buffer;
			pCBufferHeap->AllocConstantBuffer(sizeof(cb), (void**)&constant_buffer, &cbAddr);

			float L = 0.0f;
			float R = io.DisplaySize.x;
			float B = io.DisplaySize.y;
			float T = 0.0f;
			float proj[4][4] =
			{
				{ 2.0f / (R - L)   , 0.0f             , 0.0f  ,  0.0f },
				{ 0.0f             , 2.0f / (T - B)   , 0.0f  ,  0.0f },
				{ 0.0f             , 0.0f             , 0.5f  ,  0.0f },
				{ (R + L) / (L - R), (T + B) / (B - T), 0.5f  ,  1.0f },
			};
			memcpy(constant_buffer->matTransformation, proj, sizeof(proj));
		}

		// Setup viewport
		D3D12_VIEWPORT vp;
		memset(&vp, 0, sizeof(D3D12_VIEWPORT));
		vp.Width = io.DisplaySize.x;
		vp.Height = io.DisplaySize.y;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		pCmd->RSSetViewports(1, &vp);

		// set pipeline & render state
		pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::UI_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(2));
		pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

		pCmd->IASetIndexBuffer(&IndicesView);
		pCmd->IASetVertexBuffers(0, 1, &VerticesView);
		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		// Render command lists
		int vtx_offset = 0;
		int idx_offset = 0;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* drawList = draw_data->CmdLists[n];
			for (int cmd_i = 0; cmd_i < drawList->CmdBuffer.Size; cmd_i++)
			{
				const ImDrawCmd* pcmd = &drawList->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					pcmd->UserCallback(drawList, pcmd);
				}
				else
				{
					const D3D12_RECT r =
					{
						(LONG)pcmd->ClipRect.x,
						(LONG)pcmd->ClipRect.y,
						(LONG)pcmd->ClipRect.z,
						(LONG)pcmd->ClipRect.w
					};
					pCmd->RSSetScissorRects(1, &r);
					pCmd->SetGraphicsRootDescriptorTable(0, ((CBV_SRV_UAV*)(pcmd->TextureId))->GetGPUDescHandle());

					pCmd->DrawIndexedInstanced(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
				}
				idx_offset += pcmd->ElemCount;
			}
			vtx_offset += drawList->VtxBuffer.Size;
		}
	}
#endif

	if(!bHDR)
	{
		SCOPED_GPU_MARKER(pCmd, "SwapchainTransitionToPresent");
		// Transition SwapChain for Present
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
			, D3D12_RESOURCE_STATE_RENDER_TARGET
			, D3D12_RESOURCE_STATE_PRESENT)
		);
	}
}

void VQEngine::CompositUIToHDRSwapchain(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	SCOPED_GPU_MARKER(pCmd, "CompositUIToHDRSwapchain");

	// handles
	const auto VBIBIDs = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABufferIDs();
	const BufferID& IB_ID = VBIBIDs.second;
	const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);
	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.FFXCASParams.CASSharpen > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();

	SwapChain& swapchain = ctx.SwapChain;
	ID3D12Resource* pSwapChainRT = swapchain.GetCurrentBackBufferRenderTarget();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = swapchain.GetCurrentBackBufferRTVHandle();
	ID3D12Resource* pRscUI = mRenderer.GetTextureResource(mResources_MainWnd.Tex_UI_SDR);
	const SRV& srv_UI_SDR = mRenderer.GetSRV(mResources_MainWnd.SRV_UI_SDR);
	const SRV& srv_SceneColor = bFFXCASEnabled
		? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FFXCASOut)
		: (bFSREnabled
			? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FSR_RCASOut)
			: mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut));

	const int W = mpWinMain->GetWidth();
	const int H = mpWinMain->GetHeight();

	// transition barriers
	std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
	CD3DX12_RESOURCE_BARRIER SwapChainTransition = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CD3DX12_RESOURCE_BARRIER UITransition = CD3DX12_RESOURCE_BARRIER::Transition(pRscUI, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	barriers.push_back(UITransition);
	barriers.push_back(SwapChainTransition);
	pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());

	// states
	D3D12_VIEWPORT vp = {};
	vp.Width  = static_cast<FLOAT>(W);
	vp.Height = static_cast<FLOAT>(H);
	vp.MaxDepth = 1.0f;

	D3D12_RECT rect = {0, 0, W, H};

	// cbuffer
	float* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	const size_t cbSize = sizeof(float) * 1;
	pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = PPParams.TonemapperParams.UIHDRBrightness;


	// set states
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::UI_HDR_scRGB_PSO)); // TODO: HDR10/PQ PSO?
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(16));
	pCmd->SetGraphicsRootDescriptorTable(0, srv_SceneColor.GetGPUDescHandle());
	pCmd->SetGraphicsRootDescriptorTable(1, srv_UI_SDR.GetGPUDescHandle());
	//pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);
	pCmd->SetGraphicsRoot32BitConstant(2, *((UINT*)pConstBuffer), 0);
	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, NULL);
	pCmd->IASetIndexBuffer(&ib);
	pCmd->RSSetScissorRects(1, &rect);
	pCmd->RSSetViewports(1, &vp);
	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	// draw fullscreen triangle
	pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);

	{
		//SCOPED_GPU_MARKER(pCmd, "SwapchainTransitionToPresent");
		// Transition SwapChain for Present
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
			, D3D12_RESOURCE_STATE_RENDER_TARGET
			, D3D12_RESOURCE_STATE_PRESENT)
		);
	}
}

HRESULT VQEngine::PresentFrame(FWindowRenderContext& ctx)
{
	HRESULT hr = {};
	CommandQueue& PresentQueue = ctx.PresentQueue;
	SwapChain& swapchain = ctx.SwapChain;
	ID3D12Resource* pSwapChainRT = swapchain.GetCurrentBackBufferRenderTarget();

	std::vector<ID3D12GraphicsCommandList*>& vCmdLists = ctx.GetGFXCommandListPtrs();
	const UINT NumCommandLists = ctx.GetNumCurrentlyRecordingThreads(CommandQueue::EType::GFX);
	for (UINT i = 0; i < NumCommandLists; ++i)
	{
		vCmdLists[i]->Close();
	}
	{
		SCOPED_CPU_MARKER("ExecuteCommandLists()");
		PresentQueue.pQueue->ExecuteCommandLists(NumCommandLists, (ID3D12CommandList**)vCmdLists.data());
	}
	{
		SCOPED_CPU_MARKER("SwapchainPresent");
		hr = swapchain.Present();
	}
	return hr;
}

