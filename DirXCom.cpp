#include "DirXCom.h"
#include <cassert>
#include <vector>
#include "externals/DirectXTex/d3dx12.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

// stringをwstringに変換する（LoadTexture用）
static std::wstring ConvertString(const std::string& str)
{
	if (str.empty()) return L"";
	int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
	if (sizeNeeded == 0) sizeNeeded = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
	std::wstring wstr(sizeNeeded - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], sizeNeeded);
	return wstr;
}

void DirXCom::Intialize(WindowsAPI* windowsAPI)
{
	// NULL検出
	assert(windowsAPI);
	// メンバ変数に記録
	this->windowsAPI = windowsAPI;

	InitializeDevice();
	InitializeCommand();
	CreateSwapChain();
	CreateDepthBuffer();
	CreateDescriptorHeaps();
	InitializeRenderTargetView();
	InitializeDepthStencilView();
	InitializeFence();
	InitializeViewport();
	InitializeScissorRect();
	CreateDXCCompiler();
	InitializeImGui();
}

void DirXCom::InitializeDevice()
{
	CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter));
	D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
}

void DirXCom::InitializeCommand()
{
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
}

void DirXCom::CreateSwapChain()
{
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WindowsAPI::kClientWidth;
	swapChainDesc.Height = WindowsAPI::kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), windowsAPI->GetHwnd(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
}

void DirXCom::CreateDepthBuffer()
{
	// 深度バッファリソース設定
	D3D12_RESOURCE_DESC resourceDesc{ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)WindowsAPI::kClientWidth, (UINT)WindowsAPI::kClientHeight, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL };
	// 利用するヒープの設定
	D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_DEFAULT };
	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, {1.0f, 0} };
	// リソースの生成
	device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&depthStencilResource));
}

void DirXCom::CreateDescriptorHeaps()
{
	// 各種デスクリプタサイズを取得しておく
	descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// 各種デスクリプタヒープ生成
	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	srvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	dsvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
}

void DirXCom::InitializeRenderTargetView()
{
	// スワップチェーンからリソースを引っ張ってくる
	swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));

	// RTV用の設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D12_RTV_DIMENSION_TEXTURE2D };

	// 裏表の2つ分
	for (uint32_t i = 0; i < _countof(rtvHandles); ++i)
	{
		// RTVハンドルを取得
		rtvHandles[i] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, i);
		// レンダーターゲットビューの生成
		device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, rtvHandles[i]);
	}
}

void DirXCom::InitializeDepthStencilView()
{
	// DSV用の設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{ DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D };
	// DSVをデスクリプタヒープの先頭につくる
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0));
}

void DirXCom::InitializeFence()
{
	fenceValue = 0;
	device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

void DirXCom::InitializeViewport()
{
	// ビューポート矩形の設定
	viewport = D3D12_VIEWPORT{ 0.0f, 0.0f, (float)WindowsAPI::kClientWidth, (float)WindowsAPI::kClientHeight, 0.0f, 1.0f };
}

void DirXCom::InitializeScissorRect()
{
	// シザリング矩形の設定
	scissorRect = D3D12_RECT{ 0, 0, WindowsAPI::kClientWidth, WindowsAPI::kClientHeight };
}

void DirXCom::CreateDXCCompiler()
{
	// DXCユーティリティの生成
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	// DXCコンパイラの生成
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));

	// デフォルトインクルードハンドラの生成
	dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
}

void DirXCom::InitializeImGui()
{
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(windowsAPI->GetHwnd());
	ImGui_ImplDX12_Init(device.Get(), 2, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvDescriptorHeap.Get(), GetSRVCPUDescriptorHandle(0), GetSRVGPUDescriptorHandle(0));
#endif
}

ComPtr<ID3D12DescriptorHeap> DirXCom::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{ heapType, numDescriptors, shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirXCom::GetCPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirXCom::GetGPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

void DirXCom::PreDraw()
{
	// バックバッファの番号取得
	UINT bbIndex = swapChain->GetCurrentBackBufferIndex();

	// リソースバリアで書き込み可能に変更
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[bbIndex].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &barrier);

	// 描画先のRTVとDSVを指定する
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0);
	commandList->OMSetRenderTargets(1, &rtvHandles[bbIndex], false, &dsvHandle);

	// 画面全体の色をクリア
	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandles[bbIndex], clearColor, 0, nullptr);

	// 画面全体の深度をクリア
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// SRV用のデスクリプタヒープを指定する
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// ビューポート領域の設定
	commandList->RSSetViewports(1, &viewport);

	// シザー矩形の設定
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirXCom::PostDraw()
{
	// バックバッファの番号取得
	UINT bbIndex = swapChain->GetCurrentBackBufferIndex();

	// リソースバリアで表示状態に変更
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[bbIndex].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrier);

	// グラフィックスコマンドをクローズ
	commandList->Close();

	// GPUコマンドの実行
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, commandLists);

	// GPU画面の交換を通知
	swapChain->Present(1, 0);

	// Fenceの値を更新
	fenceValue++;

	// コマンドキューにシグナルを送る
	commandQueue->Signal(fence.Get(), fenceValue);

	// コマンド完了待ち
	if (fence->GetCompletedValue() < fenceValue)
	{
		fence->SetEventOnCompletion(fenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// コマンドアロケーターのリセット
	commandAllocator->Reset();

	// コマンドリストのリセット
	commandList->Reset(commandAllocator.Get(), nullptr);
}

ComPtr<IDxcBlob> DirXCom::CompileShader(const std::wstring& filePath, const wchar_t* profile)
{
	// シェーダーファイルを読み込む
	ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));

	DxcBuffer shaderSourceBuffer{ shaderSource->GetBufferPointer(), shaderSource->GetBufferSize(), DXC_CP_UTF8 };
	LPCWSTR arguments[] = { filePath.c_str(), L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr", L"-HV", L"2021" };

	// コンパイルする
	ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler.Get(), IID_PPV_ARGS(&shaderResult));
	assert(SUCCEEDED(hr));

	// コンパイル結果を取得
	ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	return shaderBlob;
}

ComPtr<ID3D12Resource> DirXCom::CreateBufferResource(size_t sizeInBytes)
{
	// アップロードヒープ
	D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD };
	// バッファリソースの設定
	D3D12_RESOURCE_DESC resourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeInBytes, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	// リソースの生成
	ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

ComPtr<ID3D12Resource> DirXCom::CreateTextureResource(const DirectX::TexMetadata& metadata)
{
	// metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// Resourceの生成
	ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

void DirXCom::UploadTextureData(const ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages)
{
	// Meta情報を取得してサブリソースを準備
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

	// 中間リソースを生成して転送コマンドを積む
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
	ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(intermediateSize);
	UpdateSubresources(commandList.Get(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

	// COPY_DEST から GENERIC_READ へ変更
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);

	// 中間リソースが消える前に転送を完了させる（戻り値をvoidにしているため）
	commandList->Close();
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, commandLists);
	fenceValue++;
	commandQueue->Signal(fence.Get(), fenceValue);
	if (fence->GetCompletedValue() < fenceValue)
	{
		fence->SetEventOnCompletion(fenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}
	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);
}

DirectX::ScratchImage DirXCom::LoadTexture(const std::string& filePath)
{
	// テクスチャファイルを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	return mipImages;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirXCom::GetSRVCPUDescriptorHandle(uint32_t index)
{
	return GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirXCom::GetSRVGPUDescriptorHandle(uint32_t index)
{
	return GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
}