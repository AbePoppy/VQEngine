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

#include "Shader.h"
#include "Renderer.h"

#include "../../Libs/VQUtils/Source/utils.h"
#include <fstream>

#include <wrl.h>
#include <D3Dcompiler.h>
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

//-------------------------------------------------------------------------------------------------------------
// CONSTANTS & STATICS
//-------------------------------------------------------------------------------------------------------------
static const std::unordered_map<EShaderStage, const char*> DEFAULT_SHADER_ENTRY_POINT_LOOKUP =
{
	{ EShaderStage::VS, "VSMain" },
	{ EShaderStage::GS, "GSMain" },
	{ EShaderStage::DS, "DSMain" },
	{ EShaderStage::HS, "HSMain" },
	{ EShaderStage::PS, "PSMain" },
	{ EShaderStage::CS, "CSMain" },
};

ID3DInclude* const SHADER_INCLUDE_HANDLER = D3D_COMPILE_STANDARD_FILE_INCLUDE; // use default include handler for using #include in shader files

#if defined( _DEBUG ) || defined ( FORCE_DEBUG )
const UINT SHADER_COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG_NAME_FOR_BINARY;
#else
const UINT SHADER_COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS;
#endif

static std::unordered_map <EShaderStage, std::string> SHADER_STAGE_STRING_LOOKUP =
{
	{EShaderStage::VS, "VS"},
	{EShaderStage::GS, "GS"},
	{EShaderStage::DS, "DS"},
	{EShaderStage::HS, "HS"},
	{EShaderStage::PS, "PS"},
	{EShaderStage::CS, "CS"}
};


//-------------------------------------------------------------------------------------------------------------
// SHADER UTILS
//-------------------------------------------------------------------------------------------------------------
namespace ShaderUtils
{
std::string GetCompileError(ID3DBlob*& errorMessage, const std::string& shdPath)
{
	if (errorMessage)
	{
		char* compileErrors = (char*)errorMessage->GetBufferPointer();
		size_t bufferSize = errorMessage->GetBufferSize();

		std::stringstream ss;
		for (unsigned int i = 0; i < bufferSize; ++i)
		{
			ss << compileErrors[i];
		}
		errorMessage->Release();
		return ss.str();
	}
	else
	{
		return ("Error compiling shader: " + shdPath);
	}
}

std::string GetIncludeFileName(const std::string& line)
{
	const std::string str_search = "#include \"";
	const size_t foundPos = line.find(str_search);
	if (foundPos != std::string::npos)
	{
		std::string quotedFileName = line.substr(foundPos + strlen("#include "), line.size() - foundPos);// +str_search.size() - 1);
		return quotedFileName.substr(1, quotedFileName.size() - 2);
	}
	return std::string();
}

bool AreIncludesDirty(const std::string& srcPath, const std::string& cachePath)
{
	const std::string ShaderSourceDir = DirectoryUtil::GetFolderPath(srcPath);
	const std::string ShaderCacheDir = DirectoryUtil::GetFolderPath(cachePath);

	std::stack<std::string> includeStack;
	includeStack.push(srcPath);
	while (!includeStack.empty())
	{
		const std::string includeFilePath = includeStack.top();
		includeStack.pop();
		std::ifstream src = std::ifstream(includeFilePath.c_str());
		if (!src.good())
		{
			Log::Error("[ShaderCompile] %s : Cannot open include file '%s'", srcPath.c_str(),  includeFilePath.c_str());
			continue;
		}

		std::string line;
		while (getline(src, line))
		{
			if (line.size() >= 2 && line[0] == line[1] && line[1] == '/') // skip comment lines
				continue;

			const std::string includeFileName = GetIncludeFileName(line);
			if (includeFileName.empty()) continue;

			const std::string includeSourcePath = ShaderSourceDir + includeFileName;
			const std::string includeCachePath = ShaderCacheDir + includeFileName;

			if (DirectoryUtil::IsFileNewer(includeSourcePath, cachePath))
				return true;
			includeStack.push(includeSourcePath);
		}
		src.close();
	}
	return false;
}

bool IsCacheDirty(const std::string& sourcePath, const std::string& cachePath)
{
	if (!DirectoryUtil::FileExists(cachePath)) 
		return true;

	return DirectoryUtil::IsFileNewer(sourcePath, cachePath) || AreIncludesDirty(sourcePath, cachePath);
}

std::vector<D3D12_INPUT_ELEMENT_DESC> ReflectInputLayoutFromVS(ID3D12ShaderReflection* pReflection)
{
	D3D12_SHADER_DESC shaderDesc = {};
	pReflection->GetDesc(&shaderDesc);
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout(shaderDesc.InputParameters);

	D3D_PRIMITIVE primitiveDesc = shaderDesc.InputPrimitive;

	for (unsigned i = 0; i < shaderDesc.InputParameters; ++i)
	{
		D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
		pReflection->GetInputParameterDesc(i, &paramDesc);

		// fill out input element desc
		D3D12_INPUT_ELEMENT_DESC elementDesc;
		elementDesc.SemanticName = paramDesc.SemanticName;
		elementDesc.SemanticIndex = paramDesc.SemanticIndex;
		elementDesc.InputSlot = 0;
		elementDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		elementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elementDesc.InstanceDataStepRate = 0;

		// determine DXGI format
		if (paramDesc.Mask == 1)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
		}
		else if (paramDesc.Mask <= 3)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		}
		else if (paramDesc.Mask <= 7)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		}
		else if (paramDesc.Mask <= 15)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		}

		inputLayout[i] = elementDesc; //save element desc
	}

	return inputLayout;
}


Shader::FBlob CompileFromSource(const FShaderStageCompileDesc& ShaderStageCompileDesc, std::string& OutErrorString)
{
	const WCHAR* strPath = ShaderStageCompileDesc.FilePath.data();
	std::vector<std::string> SMTokens = StrUtil::split(ShaderStageCompileDesc.ShaderModel, '_');
	assert(SMTokens.size() == 3);

	// https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model
	const bool bIsShaderModel5 = SMTokens[1][0] == '5';

	const EShaderStage ShaderStageEnum = GetShaderStageEnumFromShaderModel(ShaderStageCompileDesc.ShaderModel);
	Log::Info("Compiling Shader Source: %s [%s @ %s()]"
		, StrUtil::UnicodeToASCII<256>(strPath).c_str()
		, SHADER_STAGE_STRING_LOOKUP.at(ShaderStageEnum).c_str()
		, ShaderStageCompileDesc.EntryPoint.c_str()
	);

	//-------------------------------------------------------------------------
	// Great readup on DirectX12 shader compiling conventions by Adam Sawicki,
	// explaining FXC vs DXC and how to use them in an application
	// https://asawicki.info/news_1719_two_shader_compilers_of_direct3d_12
	//
	// YouTube: HLSL Compiler | Michael Dougherty | DirectX Developer Day
	// https://www.youtube.com/watch?v=tyyKeTsdtmo
	// 
	// Using DXC In Practice
	// https://posts.tanki.ninja/2019/07/11/Using-DXC-In-Practice/
	// 
	// DXC Wiki 'Using dxc.exe & dxcompiler.dll'
	// https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll
	//-------------------------------------------------------------------------
	Shader::FBlob blob;

	// SM5 - Use FXC compiler - generates DXBC shader code
	if (bIsShaderModel5)
	{
		ID3DBlob* pBlob_ErrMsg = nullptr;

		int i = 0;
		std::vector<D3D_SHADER_MACRO> d3dMacros(ShaderStageCompileDesc.Macros.size() + 1);
		std::for_each(RANGE(ShaderStageCompileDesc.Macros), [&](const FShaderMacro& macro)
		{
			d3dMacros[i++] = D3D_SHADER_MACRO({ macro.Name.c_str(), macro.Value.c_str() });
		});
		d3dMacros[i] = { NULL, NULL };

		if (FAILED(D3DCompileFromFile(
			strPath,
			d3dMacros.data(),
			SHADER_INCLUDE_HANDLER,
			ShaderStageCompileDesc.EntryPoint.c_str(),
			ShaderStageCompileDesc.ShaderModel.c_str(),
			SHADER_COMPILE_FLAGS,
			0,
			&blob.pD3DBlob,
			&pBlob_ErrMsg)))
		{
			OutErrorString = GetCompileError(pBlob_ErrMsg, StrUtil::UnicodeToASCII<512>(strPath));
		}

	}

	// SM6 - Use DXC compiler - generates DXIL shader code
	else
	{
		// collection of wstrings to feed into dxc compiler
		const std::wstring strEntryPoint  = StrUtil::ASCIIToUnicode(ShaderStageCompileDesc.EntryPoint);
		const std::wstring strShaderModel = StrUtil::ASCIIToUnicode(StrUtil::GetLowercased(ShaderStageCompileDesc.ShaderModel));
		const std::wstring strParentFolder = StrUtil::ASCIIToUnicode(DirectoryUtil::GetFolderPath(StrUtil::UnicodeToASCII<260>(strPath)));
		std::vector<std::wstring> unicodeDefineArgs;
		for (const FShaderMacro& macro : ShaderStageCompileDesc.Macros)
		{
			unicodeDefineArgs.push_back(StrUtil::ASCIIToUnicode(macro.Name) + L"=" + StrUtil::ASCIIToUnicode(macro.Value));
		}
		std::vector<LPCWSTR> ppArgs = {}; // pointers to various wstrings in the function scope

		// initialize compiler/lib/util instance
		CComPtr<IDxcLibrary>   DXC_library;
		CComPtr<IDxcCompiler3> DXC_compiler3; // IDxcCompiler3 is the latest as of writing this code (April 2021)
		CComPtr<IDxcUtils>     DXC_utils;
		HRESULT hr = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&DXC_library));
		if (FAILED(hr))
		{
			Log::Error("Couldn't initialize DirectXCompiler Library");
			assert(false);
		}
		hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&DXC_compiler3));
		if (FAILED(hr))
		{
			Log::Error("Couldn't initialize DirectXCompiler Compiler3");
			assert(false);
		}
		hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&DXC_utils));
		if (FAILED(hr))
		{
			Log::Error("Couldn't initialize DirectXCompiler Utils");
			assert(false);
		}

		CComPtr<IDxcVersionInfo> DXC_versionInfo; // ?

		// load shader source blob from file
		uint32_t codePage = CP_UTF8;
		CComPtr<IDxcBlobEncoding> sourceBlob;
		if (FAILED(DXC_library->CreateBlobFromFile(strPath, &codePage, &sourceBlob)))
		{
			assert(false); // Handle file loading error...
		}
		DxcBuffer Source;
		Source.Ptr = sourceBlob->GetBufferPointer();
		Source.Size = sourceBlob->GetBufferSize();
		Source.Encoding = DXC_CP_ACP;  // Assume BOM says UTF8 or UTF16 or this is ANSI text.
		
		// build args: support compile flags from D3DCompile
		UINT CompileFlags = 0; // TODO
		if (CompileFlags & D3DCOMPILE_DEBUG)                      ppArgs.push_back(L"-Zi");
		if (CompileFlags & D3DCOMPILE_OPTIMIZATION_LEVEL0)        ppArgs.push_back(L"-O0");
		if (CompileFlags & D3DCOMPILE_SKIP_OPTIMIZATION)          ppArgs.push_back(L"-0d");
		if (CompileFlags & D3DCOMPILE_DEBUG_NAME_FOR_SOURCE)      ppArgs.push_back(L"-Zss");
		else if (CompileFlags & D3DCOMPILE_DEBUG_NAME_FOR_BINARY) ppArgs.push_back(L"-Zsb");

		// build args: defines
		for (const std::wstring& unicodeDefineArg : unicodeDefineArgs)
		{
			ppArgs.push_back(L"-D");
			ppArgs.push_back(unicodeDefineArg.c_str());
		}

		// build args: entry point
		ppArgs.push_back(L"-E");
		ppArgs.push_back(strEntryPoint.c_str());

		// build args: shader model
		ppArgs.push_back(L"-T");
		ppArgs.push_back(strShaderModel.c_str());

		// build args: include path
		ppArgs.push_back(L"-I");
		ppArgs.push_back(strParentFolder.c_str());

		// include handler
		CComPtr<IDxcIncludeHandler> pIncludeHandler;
		DXC_utils->CreateDefaultIncludeHandler(&pIncludeHandler);

		// compile
		CComPtr<IDxcResult> pResults;
		hr = DXC_compiler3->Compile(
			&Source,
			ppArgs.data(), (UINT32)ppArgs.size(),
			pIncludeHandler,
			IID_PPV_ARGS(&pResults)
		);
		pResults->GetStatus(&hr);

		// Print errors/warnings if present
		CComPtr<IDxcBlobUtf8> pErrors = nullptr;
		pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		// Note that d3dcompiler would return null if no errors or warnings are present.  
		// IDxcCompiler3::Compile will always return an error buffer, but its length will be zero if there are no warnings or errors.
		if (pErrors != nullptr && pErrors->GetStringLength() != 0)
		{
			std::string msg = pErrors->GetStringPointer();
			if(FAILED(hr)) { Log::Error(msg);   }
			else           { Log::Warning(msg); }
		}

		// Quit if the compilation failed.
		if (FAILED(hr))
		{
			wprintf(L"Compilation Failed\n");
			assert(false); // TODO:
		}
		
		CComPtr<IDxcBlobUtf16> pShaderName = nullptr;
		pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&blob.pBlobDxc), &pShaderName);
	}

	return blob;
}


Shader::FBlob CompileFromCachedBinary(const std::string& ShaderBinaryFilePath)
{
	std::ifstream cache(ShaderBinaryFilePath, std::ios::in | std::ios::binary | std::ios::ate);
	const size_t shaderBinarySize = cache.tellg();
	void* pBuffer = calloc(1, shaderBinarySize);
	cache.seekg(0);
	cache.read(reinterpret_cast<char*>(pBuffer), shaderBinarySize);
	cache.close();

	Log::Info("Loading Shader Binary: %s ", DirectoryUtil::GetFileNameFromPath(ShaderBinaryFilePath).c_str());

	Shader::FBlob ShaderBlob;
	if (FAILED(D3DCreateBlob(shaderBinarySize, &ShaderBlob.pD3DBlob)))
	{
		assert(false);
	}
	
	assert(pBuffer);
	assert(shaderBinarySize > 0);
	memcpy(ShaderBlob.pD3DBlob->GetBufferPointer(), pBuffer, shaderBinarySize);
	free(pBuffer);

	return ShaderBlob;
}

void CacheShaderBinary(const std::string& ShaderBinaryFilePath, size_t ShaderBinarySize, const void* pShaderBinary)
{
	const char* pBuffer = reinterpret_cast<const char*>(pShaderBinary);
	std::ofstream cache(ShaderBinaryFilePath, std::ios::out | std::ios::binary);
	cache.write(pBuffer, ShaderBinarySize);
	cache.close();
}


size_t GeneratePreprocessorDefinitionsHash(const std::vector<FShaderMacro>& macros)
{
	if (macros.empty()) return 0;
	std::string concatenatedMacros;
	for (const FShaderMacro& macro : macros)
		concatenatedMacros += macro.Name + macro.Value;
	return std::hash<std::string>()(concatenatedMacros);
}

EShaderStage GetShaderStageEnumFromShaderModel(const std::string& ShaderModel)
{
	// ShaderModel e.g. = "cs_5_1"
	//                     ^^-- use this token as enum lookup key
	auto vTokens = StrUtil::split(ShaderModel, '_');
	assert(vTokens.size() > 1); // (sort of) validate ShaderModel string
	const std::string ShaderModelIdentifier = StrUtil::GetLowercased(vTokens[0]);

	static std::unordered_map<std::string, EShaderStage> SHADER_STAGE_ENUM_LOOKUP =
	{
		{ "vs", EShaderStage::VS},
		{ "gs", EShaderStage::GS},
		{ "ds", EShaderStage::DS},
		{ "hs", EShaderStage::HS},
		{ "ps", EShaderStage::PS},
		{ "cs", EShaderStage::CS}
	};

	auto it = SHADER_STAGE_ENUM_LOOKUP.find(ShaderModelIdentifier);
	if (it == SHADER_STAGE_ENUM_LOOKUP.end())
	{
		Log::Warning("Unknown shader model identifier: %s", ShaderModelIdentifier.c_str());
		return EShaderStage::UNINITIALIZED;
	}

	return it->second;
}

} // namespace ShaderUtils

const void* Shader::FBlob::GetByteCode() const
{
	if (this->pD3DBlob)
		return this->pD3DBlob->GetBufferPointer();
	if (this->pBlobDxc)
		return this->pBlobDxc->GetBufferPointer();
	
	assert(!IsNull());
	return nullptr; // should never hit this
}

size_t Shader::FBlob::GetByteCodeSize() const
{
	if (this->pD3DBlob)
		return this->pD3DBlob->GetBufferSize();
	if (this->pBlobDxc)
		return this->pBlobDxc->GetBufferSize();
	return 0;
}
