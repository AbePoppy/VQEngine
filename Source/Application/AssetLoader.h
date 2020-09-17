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

#include "Model.h"

#include <set>
#include <queue>
#include <mutex>
#include <future>

class ThreadPool;
class Scene;
class GameObject;

class AssetLoader
{
public:
	using  LoadTaskID = int;

	// 
	// STATIC INTERFACE
	// 
	static LoadTaskID GenerateModelLoadTaskID();

	//
	// TEXTURE LOADING
	//
	enum ETextureType
	{
		DIFFUSE = 0,
		NORMALS,
		SPECULAR,
		ALPHA_MASK,
		EMISSIVE,
		HEIGHT,
		METALNESS,
		ROUGHNESS,

		NUM_TEXTURE_TYPES
	};
	struct FTextureLoadParams
	{
		ETextureType TexType;
		MaterialID   MatID;
		std::string  TexturePath;
	};
	struct FTextureLoadResult
	{
		ETextureType type; // material textures: diffuse/normal/alpha_mask/...
		std::shared_future<TextureID> texLoadResult;
	};
	using TextureLoadResult_t  = FTextureLoadResult;
	using TextureLoadResults_t = std::unordered_multimap<MaterialID, TextureLoadResult_t>;

	//
	// MODEL LOADING
	//
	struct FModelLoadParams
	{
		using pfnImportModel_t = ModelID(*)(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName);

		GameObject* pObject = nullptr;
		std::string      ModelPath;
		std::string      ModelName;
		pfnImportModel_t pfnImportModel = nullptr;
	};
	using ModelLoadResult_t    = std::shared_future<ModelID>;
	using ModelLoadResults_t   = std::unordered_map<GameObject*, ModelLoadResult_t>;

	struct FMaterialTextureAssignment
	{
		MaterialID matID = INVALID_ID;
		std::vector<AssetLoader::FTextureLoadResult> DiffuseIDs;
		std::vector<AssetLoader::FTextureLoadResult> SpecularIDs;
		std::vector<AssetLoader::FTextureLoadResult> NormalsIDs;
		std::vector<AssetLoader::FTextureLoadResult> HeightMapIDs;
		std::vector<AssetLoader::FTextureLoadResult> AlphaMapIDs;
		std::vector<AssetLoader::FTextureLoadResult>& GetTextureMapCollection(ETextureType type);
	};
	struct FMaterialTextureAssignments
	{
		FMaterialTextureAssignments(const ThreadPool& workers) : mWorkersThreads(workers) {}
		void DoAssignments(Scene* pScene, VQRenderer* pRenderer);
		void WaitForTextureLoads();

		const ThreadPool&                       mWorkersThreads; // to check if pool IsExiting()
		std::vector<FMaterialTextureAssignment> mAssignments;
		TextureLoadResults_t                    mTextureLoadResults;
	};

	//
	// CLASS INTERFACE
	// 
	AssetLoader(ThreadPool& WorkerThreads_Model, ThreadPool& WorkerThreads_Texture, VQRenderer& renderer);

	inline const ThreadPool& GetThreadPool_TextureLoad() const { return mWorkers_TextureLoad; }


	void QueueModelLoad(GameObject* pObject, const std::string& ModelPath, const std::string& ModelName);
	void QueueTextureLoad(LoadTaskID taskID, const FTextureLoadParams& TexLoadParam);

	ModelLoadResults_t   StartLoadingModels(Scene* pScene);
	TextureLoadResults_t StartLoadingTextures(LoadTaskID taskID);

private:
	static ModelID ImportModel(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName = "NONE");

	//
	// DATA
	//
private:
	ThreadPool& mWorkers_ModelLoad;
	ThreadPool& mWorkers_TextureLoad;
	VQRenderer& mRenderer;

	template<class T> struct FLoadTaskContext
	{
		std::mutex            Mtx;
		std::queue<T>         LoadQueue;
		std::set<std::string> UniquePaths;
	};
	std::unordered_map<LoadTaskID, FLoadTaskContext<FTextureLoadParams>> mLookup_TextureLoadContext;

	// TODO: use ConcurrentQueue<T> with ProcessElements(pfnProcess);
	std::queue<FModelLoadParams> mModelLoadQueue;
	std::set<std::string>        mUniqueModelPaths;
	std::mutex                   mMtxQueue_ModelLoad;

	std::unordered_map<std::string, ModelID> mLoadedModels;
};