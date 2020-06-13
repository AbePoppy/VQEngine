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



void VQEngine::UpdateThread_Main()
{
	Log::Info("UpdateThread_Main()");
	mNumUpdateLoopsExecuted.store(0);

	bool bQuit = false;
	while (!mbStopAllThreads && !bQuit)
	{
		UpdateThread_PreUpdate();

		if (!mbRenderThreadInitialized)
		{
			continue;
		}

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"UpdateThread_Tick() : */"u%d (r=%llu)", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

		UpdateThread_UpdateAppState();

		UpdateThread_PostUpdate();

		++mNumUpdateLoopsExecuted;

		UpdateThread_SignalRenderThread();

		UpdateThread_WaitForRenderThread();
	}

	Log::Info("UpdateThread_Main() : Exit");
}



void VQEngine::UpdateThread_WaitForRenderThread()
{
#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info("u:wait : u=%llu, r=%llu", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	mpSemUpdate->Wait();
}

void VQEngine::UpdateThread_SignalRenderThread()
{
	mpSemRender->Signal();
}

void VQEngine::UpdateThread_PreUpdate()
{
	// update timer

	// update input

}

void VQEngine::UpdateThread_UpdateAppState()
{
	assert(mbRenderThreadInitialized);

	if (mAppState == EAppState::INITIALIZING)
	{
		// start loading
		Log::Info("Main Thread starts loading...");
	
		// Do not show windows until we have the loading screen data ready.
		LoadLoadingScreenData();
		mpWinMain->Show();
		mpWinDebug->Show();

		// start load level
		Load_SceneData_Dispatch();
		mAppState = EAppState::LOADING;

		mbLoadingLevel.store(true);
	}

	if (mbLoadingLevel)
	{
		// animate loading screen


		// check if loading is done
		const int NumActiveTasks = mUpdateWorkerThreads.GetNumActiveTasks();
		const bool bLoadDone = NumActiveTasks == 0;
		if (bLoadDone)
		{
			Log::Info("Main Thread loaded");
			mAppState = EAppState::SIMULATING;
			mbLoadingLevel.store(false);
		}

	}


	else
	{
		// update scene data
	}

}

void VQEngine::UpdateThread_PostUpdate()
{
	// compute visibility 
}


// ===============================================================================================================================


void MainWindowScene::Update()
{
}

void DebugWindowScene::Update()
{
}
