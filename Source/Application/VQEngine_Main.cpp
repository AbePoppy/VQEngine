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

#include "Libs/VQUtils/Source/utils.h"

#include <fstream>
#include <sstream>
#include <cassert>

#ifdef _DEBUG
constexpr char* BUILD_CONFIG = "Debug";
#else
constexpr char* BUILD_CONFIG = "Release";
#endif
constexpr char* VQENGINE_VERSION = "v0.1.0";


void VQEngine::MainThread_Tick()
{
	if (mpWinMain->IsClosed())
	{
		mpWinDebug->OnClose();
		PostQuitMessage(0);
	}

	if (this->mSettings.bAutomatedTestRun)
	{
		if (this->mSettings.NumAutomatedTestFrames <= mNumRenderLoopsExecuted)
		{
			PostQuitMessage(0);
		}
	}


	// TODO: populate input queue and signal Update thread 
	//       to drain the buffered input from the queue
}

bool VQEngine::Initialize(const FStartupParameters& Params)
{
	InititalizeEngineSettings(Params);
	InitializeApplicationWindows(Params);
	InitializeThreads();

	return true;
}

void VQEngine::Exit()
{
	ExitThreads();
}



void VQEngine::InititalizeEngineSettings(const FStartupParameters& Params)
{
	// Defaults
	mSettings.gfx.bFullscreen = false;
	mSettings.gfx.bVsync = false;
	mSettings.gfx.bUseTripleBuffering = true;
	mSettings.gfx.RenderResolutionX = 1920;
	mSettings.gfx.RenderResolutionY = 1080;


	mSettings.bAutomatedTestRun = false;
	mSettings.NumAutomatedTestFrames = 100; // default num frames to run if -Test is specified in cmd line params
	mSettings.DebugWindow_Width  = 600;
	mSettings.DebugWindow_Height = 600;
	mSettings.MainWindow_Width = 1920;
	mSettings.MainWindow_Height = 1080;

	sprintf_s(mSettings.strMainWindowTitle , "VQEngine %s-%s", VQENGINE_VERSION, BUILD_CONFIG);
	sprintf_s(mSettings.strDebugWindowTitle, "VQDebugging");

	// Override #0 : from file
	FStartupParameters paramFile = VQEngine::ParseEngineSettingsFile();
	if (paramFile.bOverrideGFXSetting_bFullscreen)         mSettings.gfx.bFullscreen         = paramFile.EngineSettings.gfx.bFullscreen;
	if (paramFile.bOverrideGFXSetting_bVSync     )         mSettings.gfx.bVsync              = paramFile.EngineSettings.gfx.bVsync;
	if (paramFile.bOverrideGFXSetting_bUseTripleBuffering) mSettings.gfx.bUseTripleBuffering = paramFile.EngineSettings.gfx.bUseTripleBuffering;
	if (paramFile.bOverrideGFXSetting_Width)               mSettings.gfx.RenderResolutionX   = paramFile.EngineSettings.gfx.RenderResolutionX;
	if (paramFile.bOverrideGFXSetting_Height)              mSettings.gfx.RenderResolutionY   = paramFile.EngineSettings.gfx.RenderResolutionY;

	if (paramFile.bOverrideENGSetting_MainWindowWidth)    mSettings.MainWindow_Width  = paramFile.EngineSettings.MainWindow_Width;
	if (paramFile.bOverrideENGSetting_MainWindowHeight)   mSettings.MainWindow_Height = paramFile.EngineSettings.MainWindow_Height;
	if (paramFile.bOverrideENGSetting_bAutomatedTest)     mSettings.bAutomatedTestRun = paramFile.EngineSettings.bAutomatedTestRun;
	if (paramFile.bOverrideENGSetting_bTestFrames)
	{ 
		mSettings.bAutomatedTestRun = true; 
		mSettings.NumAutomatedTestFrames = paramFile.EngineSettings.NumAutomatedTestFrames; 
	}


	// Override #1 : if there's command line params
	if (Params.bOverrideGFXSetting_bFullscreen)         mSettings.gfx.bFullscreen         = Params.EngineSettings.gfx.bFullscreen;
	if (Params.bOverrideGFXSetting_bVSync     )         mSettings.gfx.bVsync              = Params.EngineSettings.gfx.bVsync;
	if (Params.bOverrideGFXSetting_bUseTripleBuffering) mSettings.gfx.bUseTripleBuffering = Params.EngineSettings.gfx.bUseTripleBuffering;
	if (Params.bOverrideGFXSetting_Width)               mSettings.gfx.RenderResolutionX   = Params.EngineSettings.gfx.RenderResolutionX;
	if (Params.bOverrideGFXSetting_Height)              mSettings.gfx.RenderResolutionY   = Params.EngineSettings.gfx.RenderResolutionY;

	if (Params.bOverrideENGSetting_MainWindowWidth)    mSettings.MainWindow_Width  = Params.EngineSettings.MainWindow_Width;
	if (Params.bOverrideENGSetting_MainWindowHeight)   mSettings.MainWindow_Height = Params.EngineSettings.MainWindow_Height;
	if (Params.bOverrideENGSetting_bAutomatedTest)     mSettings.bAutomatedTestRun = Params.EngineSettings.bAutomatedTestRun;
	if (Params.bOverrideENGSetting_bTestFrames)
	{
		mSettings.bAutomatedTestRun = true;
		mSettings.NumAutomatedTestFrames = Params.EngineSettings.NumAutomatedTestFrames;
	}
}

void VQEngine::InitializeApplicationWindows(const FStartupParameters& Params)
{
	FWindowDesc mainWndDesc = {};
	mainWndDesc.width  = mSettings.MainWindow_Width;
	mainWndDesc.height = mSettings.MainWindow_Height;
	mainWndDesc.hInst = Params.hExeInstance;
	mainWndDesc.pWndOwner = this;
	mainWndDesc.pfnWndProc = WndProc;
	mpWinMain.reset(new Window(mSettings.strMainWindowTitle, mainWndDesc));

	mainWndDesc.width  = mSettings.DebugWindow_Width;
	mainWndDesc.height = mSettings.DebugWindow_Height;
	mpWinDebug.reset(new Window(mSettings.strDebugWindowTitle, mainWndDesc));
}

void VQEngine::InitializeThreads()
{
	mbStopAllThreads.store(false);
	mRenderThread = std::thread(&VQEngine::RenderThread_Main, this);
	mUpdateThread = std::thread(&VQEngine::UpdateThread_Main, this);
	mLoadThread   = std::thread(&VQEngine::LoadThread_Main, this);
}

void VQEngine::ExitThreads()
{
	mbStopAllThreads.store(true);
	mRenderThread.join();
	mUpdateThread.join();

	// no need to lock here: https://en.cppreference.com/w/cpp/thread/condition_variable/notify_all
	// The notifying thread does not need to hold the lock on the same mutex 
	// as the one held by the waiting thread(s); in fact doing so is a pessimization, 
	// since the notified thread would immediately block again, waiting for the 
	// notifying thread to release the lock.
	mCVLoadTasksReadyForProcess.notify_all();

	mLoadThread.join();
}


static std::pair<std::string, std::string> ParseLineINI(const std::string& iniLine)
{
	assert(!iniLine.empty());
	std::pair<std::string, std::string> SettingNameValuePair;

	const bool bSectionTag = iniLine[0] == '[';

	if (bSectionTag)
	{
		auto vecSettingNameValue = StrUtil::split(iniLine.substr(1), ']');
		SettingNameValuePair.first = vecSettingNameValue[0];
	}
	else
	{
		auto vecSettingNameValue = StrUtil::split(iniLine, '=');
		assert(vecSettingNameValue.size() >= 2);
		SettingNameValuePair.first  = vecSettingNameValue[0];
		SettingNameValuePair.second = vecSettingNameValue[1];
	}


	return SettingNameValuePair;
}
static bool ParseBool(const std::string& s) { bool b; std::istringstream(s) >> b; return b; }
static int  ParseInt(const std::string& s) { return std::atoi(s.c_str()); }

FStartupParameters VQEngine::ParseEngineSettingsFile()
{
	constexpr char* ENGINE_SETTINGS_FILE_NAME = "Data/EngineSettings.ini";
	FStartupParameters params = {};

	std::ifstream file(ENGINE_SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		std::string currSection;
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") continue; // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line);
			const std::string& SettingName  = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (SettingName == "Graphics" || SettingName == "Engine")
			{
				currSection = SettingName;
				continue;
			}


			// 
			// Graphics
			//
			if (SettingName == "VSync")
			{
				params.bOverrideGFXSetting_bVSync = true;
				params.EngineSettings.gfx.bVsync = ParseBool(SettingValue);
			}
			if (SettingName == "ResolutionX")
			{
				params.bOverrideGFXSetting_Width = true;
				params.EngineSettings.gfx.RenderResolutionX = ParseInt(SettingValue);
			}
			if (SettingName == "ResolutionY")
			{
				params.bOverrideGFXSetting_Height = true;
				params.EngineSettings.gfx.RenderResolutionY = ParseInt(SettingValue);
			}


			// 
			// Engine
			//
			if (SettingName == "Width")
			{
				params.bOverrideENGSetting_MainWindowWidth = true;
				params.EngineSettings.MainWindow_Width = ParseInt(SettingValue);
			}
			if (SettingName == "Height")
			{
				params.bOverrideENGSetting_MainWindowHeight = true;
				params.EngineSettings.MainWindow_Height = ParseInt(SettingValue);
			}
		}
	}
	else
	{
		Log::Warning("Cannot find settings file %s in current directory: %s", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
		Log::Warning("Will use default settings for Engine & Graphics.", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}

	return params;
}
