#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <vector>
#include <wincodec.h> // WIC用のヘッダー
#include <cmath>     // 回転・数学計算用

// DXCコンパイルに必要なヘッダーとライブラリ
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

// ReportLiveObjects用のヘッダーとライブラリ
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"Windowscodecs.lib") // WIC用のライブラリ

// ImGuiを有効化するフラグとインクルード設定
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

// 頂点データ構造体
struct Vector4
{
	float x, y, z, w;
};

// テクスチャ座標用構造体
struct Vector2
{
	float u, v;
};

// 頂点情報構造体（座標 + テクスチャ座標）
struct Vertex
{
	Vector4 position;
	Vector2 texcoord;
};

// マテリアル用構造体（b0に転送するデータ型）
struct Material
{
	Vector4 color;
};

// 行列計算用の4x4行列構造体
struct Matrix4x4
{
	float m[4][4];
};

// 変形行列用構造体（b1に転送するデータ型）
struct TransformationMatrix
{
	Matrix4x4 wvp; // シェーダー側の float4x4 WVP と一致させる
};

// CPU側で扱うTransform構造体
struct Transform
{
	float scale[3];
	float rotate[3];
	float translate[3];
};

// --- 行列計算用のヘルパー関数群 ---
Matrix4x4 MakeIdentity4x4()
{
	Matrix4x4 result = {};
	for (int i = 0; i < 4; ++i) result.m[i][i] = 1.0f;
	return result;
}

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip)
{
	Matrix4x4 result = {};
	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;
	return result;
}

// アフィン変換行列作成関数（警告 C4100 対策済み）
Matrix4x4 MakeAffineMatrix(const float scale[3], const float rotate[3], const float translate[3])
{
	(void)rotate;

	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = scale[0];
	result.m[1][1] = scale[1];
	result.m[2][2] = scale[2];
	result.m[3][0] = translate[0];
	result.m[3][1] = translate[1];
	result.m[3][2] = translate[2];
	return result;
}

Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2)
{
	Matrix4x4 result = {};
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			result.m[i][j] = m1.m[i][0] * m2.m[0][j] +
				m1.m[i][1] * m2.m[1][j] +
				m1.m[i][2] * m2.m[2][j] +
				m1.m[i][3] * m2.m[3][j];
		}
	}
	return result;
}

// --- ★【関数化】DescriptorHandle取得関数を追加 ---
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

// WindowProc
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
	{
		return true;
	}
#endif
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// ログ出力用ヘルパー（文字列変換）
std::wstring ConvertString(const std::string& str)
{
	if (str.empty()) return std::wstring();
	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

void Log(const std::string& message) { OutputDebugStringA(message.c_str()); }
void Log(const std::wstring& message) { OutputDebugStringW(message.c_str()); }

// CompileShader 関数
IDxcBlob* CompileShader(
	const std::wstring& filePath, const wchar_t* profile,
	IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler)
{
	Log(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile));

	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	if (FAILED(hr))
	{
		Log(std::format(L"❌ シェーダーファイルが見つかりません: {}\n", filePath));
		assert(SUCCEEDED(hr));
	}

	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	LPCWSTR arguments[] = {
		filePath.c_str(), L"-E", L"main", L"-T", profile,
		L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr", L"-HV", L"2021"
	};

	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));
	assert(SUCCEEDED(hr));

	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0)
	{
		Log(reinterpret_cast<const char*>(shaderError->GetStringPointer()));
		assert(false);
	}

	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	shaderSource->Release();
	shaderResult->Release();
	return shaderBlob;
}

// CreateDescriptorHeap 関数
ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
	ID3D12DescriptorHeap* descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

// WICテクスチャ読み込み関数
void LoadTextureFile(const std::wstring& filePath, std::vector<uint8_t>& outData, D3D12_RESOURCE_DESC& outDesc)
{
	IWICImagingFactory* wicFactory = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
	assert(SUCCEEDED(hr));

	IWICBitmapDecoder* decoder = nullptr;
	hr = wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);

	if (FAILED(hr))
	{
		std::wstring errorMsg = L"❌ 画像ファイルが開けません。ファイルが存在するか、パスを確認してください:\n" + filePath;
		Log(errorMsg + L"\n");
		MessageBoxW(nullptr, errorMsg.c_str(), L"ファイル読み込みエラー", MB_OK | MB_ICONERROR);
		exit(-1);
	}

	IWICBitmapFrameDecode* frame = nullptr;
	decoder->GetFrame(0, &frame);

	UINT width = 0, height = 0;
	frame->GetSize(&width, &height);

	outData.resize(width * height * 4);
	frame->CopyPixels(nullptr, width * 4, static_cast<UINT>(outData.size()), outData.data());

	outDesc = {};
	outDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	outDesc.Width = width;
	outDesc.Height = height;
	outDesc.DepthOrArraySize = 1;
	outDesc.MipLevels = 1;
	outDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	outDesc.SampleDesc.Count = 1;
	outDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	frame->Release();
	decoder->Release();
	wicFactory->Release();
}

// 深度テクスチャリソースの生成関数
ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height)
{
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));

	return resource;
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	WNDCLASS wc{};
	wc.lpfnWndProc = WindowProc;
	wc.lpszClassName = L"CG2WindowClass";
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClass(&wc);

	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindow(
		wc.lpszClassName, L"CG2 - Sprite Drawing", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, wrc.right - wrc.left, wrc.bottom - wrc.top,
		nullptr, nullptr, wc.hInstance, nullptr
	);
	ShowWindow(hwnd, SW_SHOW);

	ID3D12Debug1* debugController = nullptr;
#ifdef _DEBUG
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	IDXGIFactory7* dxgiFactory = nullptr;
	CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

	IDXGIAdapter4* useAdapter = nullptr;
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; i++)
	{
		DXGI_ADAPTER_DESC3 adapterDesc{};
		useAdapter->GetDesc3(&adapterDesc);
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
		useAdapter = nullptr;
	}

	ID3D12Device* device = nullptr;
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
	for (size_t i = 0; i < _countof(featureLevels); ++i)
	{
		if (SUCCEEDED(D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device)))) break;
	}

	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	ID3D12CommandAllocator* commandAllocator = nullptr;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	ID3D12GraphicsCommandList* commandList = nullptr;
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));

	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	ID3D12Resource* swapChainResources[2] = { nullptr };
	swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	commandList->Close();

	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
	device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	IDxcIncludeHandler* includeHandler = nullptr;
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

	// ルートシグネチャ設定
	D3D12_ROOT_PARAMETER rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 1;

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	ID3D12RootSignature* rootSignature = nullptr;
	device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

	// インプットレイアウト
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{ inputElementDescs, _countof(inputElementDescs) };
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature;
	psoDesc.InputLayout = inputLayoutDesc;
	psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
	psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
	psoDesc.BlendState = blendDesc;
	psoDesc.RasterizerState = rasterizerDesc;
	psoDesc.DepthStencilState = depthStencilDesc;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	ID3D12PipelineState* pipelineState = nullptr;
	device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));

	D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD };

	// --- 3D三角形用のリソース設定 ---
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeof(Vertex) * 6;
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* vertexResource = nullptr;
	device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResource));

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = sizeof(Vertex) * 6;
	vertexBufferView.StrideInBytes = sizeof(Vertex);

	Vertex* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	vertexData[0].position = { -0.5f, -0.5f, 0.0f, 1.0f }; vertexData[0].texcoord = { 0.0f, 1.0f };
	vertexData[1].position = { 0.0f,  0.5f, 0.0f, 1.0f }; vertexData[1].texcoord = { 0.5f, 0.0f };
	vertexData[2].position = { 0.5f, -0.5f, 0.0f, 1.0f }; vertexData[2].texcoord = { 1.0f, 1.0f };
	vertexData[3].position = { -0.5f, -0.5f,  0.5f, 1.0f }; vertexData[3].texcoord = { 0.0f, 1.0f };
	vertexData[4].position = { 0.0f,  0.0f,  0.0f, 1.0f }; vertexData[4].texcoord = { 0.5f, 0.0f };
	vertexData[5].position = { 0.5f, -0.5f, -0.5f, 1.0f }; vertexData[5].texcoord = { 1.0f, 1.0f };
	vertexResource->Unmap(0, nullptr);

	// --- 2Dスプライト用のリソース作成 ---
	ID3D12Resource* vertexResourceSprite = nullptr;
	device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceSprite));

	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	vertexBufferViewSprite.SizeInBytes = sizeof(Vertex) * 6;
	vertexBufferViewSprite.StrideInBytes = sizeof(Vertex);

	Vertex* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };   vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };     vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f }; vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };     vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[4].position = { 640.0f, 0.0f, 0.0f, 1.0f };   vertexDataSprite[4].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[5].position = { 640.0f, 360.0f, 0.0f, 1.0f }; vertexDataSprite[5].texcoord = { 1.0f, 1.0f };
	vertexResourceSprite->Unmap(0, nullptr);

	// マテリアルバッファ作成
	D3D12_RESOURCE_DESC materialResourceDesc{};
	materialResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	materialResourceDesc.Width = (sizeof(Material) + 255) & ~255;
	materialResourceDesc.Height = 1;
	materialResourceDesc.DepthOrArraySize = 1;
	materialResourceDesc.MipLevels = 1;
	materialResourceDesc.SampleDesc.Count = 1;
	materialResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* materialResource = nullptr;
	device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResource));

	Material* materialData = nullptr;
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };

	// 3D用の変形行列バッファの作成
	D3D12_RESOURCE_DESC wvpResourceDesc{};
	wvpResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	wvpResourceDesc.Width = (sizeof(TransformationMatrix) + 255) & ~255;
	wvpResourceDesc.Height = 1;
	wvpResourceDesc.DepthOrArraySize = 1;
	wvpResourceDesc.MipLevels = 1;
	wvpResourceDesc.SampleDesc.Count = 1;
	wvpResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* wvpResource = nullptr;
	device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&wvpResource));

	TransformationMatrix* wvpData = nullptr;
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	wvpData->wvp = MakeIdentity4x4();

	// スプライト用の変形行列バッファ作成
	ID3D12Resource* transformationMatrixResourceSprite = nullptr;
	device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transformationMatrixResourceSprite));

	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	transformationMatrixDataSprite->wvp = MakeIdentity4x4();

	Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// --- ★【変更】DescriptorSizeを取得 ---
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 1枚目のテクスチャロード
	std::vector<uint8_t> textureData;
	D3D12_RESOURCE_DESC textureDesc{};
	LoadTextureFile(L"Resources/uvChecker.png", textureData, textureDesc);

	D3D12_HEAP_PROPERTIES textureHeapProps{};
	textureHeapProps.Type = D3D12_HEAP_TYPE_CUSTOM;
	textureHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	textureHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	ID3D12Resource* textureResource = nullptr;
	device->CreateCommittedResource(
		&textureHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureResource)
	);
	textureResource->WriteToSubresource(0, nullptr, textureData.data(), static_cast<UINT>(textureDesc.Width * 4), static_cast<UINT>(textureData.size()));

	// --- ★【変更】index=1 の位置に 1枚目のSRV (uvChecker) ハンドル作成 ---
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);


	// --- ★【追加】2枚目のテクスチャロード (monsterBall.png) と生成 ---
	std::vector<uint8_t> textureData2;
	D3D12_RESOURCE_DESC textureDesc2{};
	LoadTextureFile(L"Resources/monsterBall.png", textureData2, textureDesc2);

	ID3D12Resource* textureResource2 = nullptr;
	device->CreateCommittedResource(
		&textureHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc2,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureResource2)
	);
	textureResource2->WriteToSubresource(0, nullptr, textureData2.data(), static_cast<UINT>(textureDesc2.Width * 4), static_cast<UINT>(textureData2.size()));

	// index=2 の位置に 2枚目のSRV (monsterBall)
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = textureDesc2.Format;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(textureResource2, &srvDesc2, textureSrvHandleCPU2);


	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(kClientWidth), static_cast<float>(kClientHeight), 0.0f, 1.0f };
	D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };

#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device, 2, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvDescriptorHeap, srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
#endif

	float angle = 0.0f;
	bool useMonsterBall = true; // ★切り替え用フラグ

	MSG msg{};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Settings");

			// ★切り替え用チェックボックスを追加
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);

			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::ColorEdit4("color", &materialData->color.x);
			}

			if (ImGui::CollapsingHeader("TransformSprite", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::SliderFloat2("scale", transformSprite.scale, 0.1f, 5.0f);
				ImGui::SliderFloat2("translate", transformSprite.translate, -200.0f, 1000.0f);
			}

			ImGui::End();
			ImGui::Render();
#endif

			// 3D回転計算
			angle += 0.02f;
			float sinA = std::sin(angle);
			float cosA = std::cos(angle);

			Matrix4x4 rotateMatrix = MakeIdentity4x4();
			rotateMatrix.m[0][0] = cosA;
			rotateMatrix.m[0][2] = -sinA;
			rotateMatrix.m[2][0] = sinA;
			rotateMatrix.m[2][2] = cosA;

			wvpData->wvp = rotateMatrix;

			// スプライト行列計算
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(kClientWidth), static_cast<float>(kClientHeight), 0.0f, 100.0f);

			Matrix4x4 worldViewMatrixSprite = Multiply(worldMatrixSprite, viewMatrixSprite);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldViewMatrixSprite, projectionMatrixSprite);

			transformationMatrixDataSprite->wvp = worldViewProjectionMatrixSprite;

			commandAllocator->Reset();
			commandList->Reset(commandAllocator, nullptr);

			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = swapChainResources[backBufferIndex];
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);

			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			commandList->SetGraphicsRootSignature(rootSignature);
			commandList->SetPipelineState(pipelineState);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

			// --- ★【変更】1. 3Dオブジェクト（球）の描画：チェックボックスの三項演算子で切り替え ---
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);

			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			commandList->DrawInstanced(6, 1, 0, 0);

			// --- ★【変更】2. 2Dスプライトの描画：常に uvChecker に固定（直前のステートを引き継がないバグ対策） ---
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
			commandList->DrawInstanced(6, 1, 0, 0);

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);

			commandList->Close();
			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);

			swapChain->Present(1, 0);

			fenceValue++;
			commandQueue->Signal(fence, fenceValue);
			if (fence->GetCompletedValue() < fenceValue)
			{
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				WaitForSingleObject(fenceEvent, INFINITE);
			}
		}
	}

#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	if (transformationMatrixResourceSprite)
	{
		transformationMatrixResourceSprite->Unmap(0, nullptr);
		transformationMatrixResourceSprite->Release();
	}
	if (vertexResourceSprite)
	{
		vertexResourceSprite->Release();
	}
	if (wvpResource)
	{
		wvpResource->Unmap(0, nullptr);
		wvpResource->Release();
	}
	if (materialResource)
	{
		materialResource->Unmap(0, nullptr);
		materialResource->Release();
	}

	depthStencilResource->Release();
	dsvDescriptorHeap->Release();
	textureResource->Release();
	if (textureResource2) textureResource2->Release(); // ★2枚目のテクスチャ解放
	vertexResource->Release();
	pipelineState->Release();
	vertexShaderBlob->Release();
	pixelShaderBlob->Release();
	rootSignature->Release();
	signatureBlob->Release();
	if (errorBlob) errorBlob->Release();
	includeHandler->Release();
	dxcCompiler->Release();
	dxcUtils->Release();
	CloseHandle(fenceEvent);
	fence->Release();
	srvDescriptorHeap->Release();
	rtvDescriptorHeap->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();
	device->Release();
	useAdapter->Release();
	dxgiFactory->Release();

#ifdef _DEBUG
	if (debugController != nullptr) debugController->Release();
#endif
	CloseWindow(hwnd);

#ifdef _DEBUG
	IDXGIDebug1* debug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
	{
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}
#endif
	CoUninitialize();
	return 0;
}