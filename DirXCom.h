#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <dxcapi.h>
#include <array>
#include <cstdint>
#include <string>
#include <chrono>
#include "WindowsAPI.h"
#include "externals/DirectXTex/DirectXTex.h"

class DirXCom
{
public: // メンバ関数
	/// <summary>
	/// 初期化
	/// </summary>
	void Intialize(WindowsAPI* windowsAPI);

	/// <summary>
	/// SRVの指定番号のCPUデスクリプタハンドルを取得する
	/// </summary>
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);

	/// <summary>
	/// SRVの指定番号のGPUデスクリプタハンドルを取得する
	/// </summary>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

	// 描画前処理
	void PreDraw();
	// 描画後処理
	void PostDraw();

	/// <summary>
	/// シェーダーのコンパイル
	/// </summary>
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile);

	/// <summary>
	/// バッファリソースの生成
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

	/// <summary>
	/// テクスチャリソースの生成
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

	/// <summary>
	/// テクスチャデータの転送
	/// </summary>
	void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

	/// <summary>
	/// テクスチャファイルの読み込み
	/// </summary>
	/// <param name="filePath">テクスチャファイルのパス</param>
	/// <returns>画像イメージデータ</returns>
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);

	// getter
	ID3D12Device* GetDevice() const { return device.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList.Get(); }

private: // 初期化用のメンバ関数
	// デバイスの初期化
	void InitializeDevice();
	// コマンド関連の初期化
	void InitializeCommand();
	// スワップチェーンの生成
	void CreateSwapChain();
	// 深度バッファの生成
	void CreateDepthBuffer();
	// 各種デスクリプタヒープの生成
	void CreateDescriptorHeaps();
	// レンダーターゲットビューの初期化
	void InitializeRenderTargetView();
	// 深度ステンシルビューの初期化
	void InitializeDepthStencilView();
	// フェンスの初期化
	void InitializeFence();
	// ビューポート矩形の初期化
	void InitializeViewport();
	// シザリング矩形の初期化
	void InitializeScissorRect();
	// DXCコンパイラの生成
	void CreateDXCCompiler();
	// ImGuiの初期化
	void InitializeImGui();

private: // 内部処理用のメンバ関数
	// FPS固定初期化
	void InitializeFixFPS();
	// FPS固定更新
	void UpdateFixFPS();

	/// <summary>
	/// デスクリプタヒープを生成する
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

	/// <summary>
	/// 指定番号のCPUデスクリプタハンドルを取得する
	/// </summary>
	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index);

	/// <summary>
	/// 指定番号のGPUデスクリプタハンドルを取得する
	/// </summary>
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index);

private: // メンバ変数
	// WindowsAPI
	WindowsAPI* windowsAPI = nullptr;

	// DirectX12デバイス
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	// DXGIファクトリ
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;

	// コマンドアロケータ
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	// コマンドリスト
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	// コマンドキュー
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;

	// スワップチェーン
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
	// スワップチェーンリソース
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> swapChainResources;

	// 深度バッファリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource;

	// デスクリプタサイズ
	uint32_t descriptorSizeRTV = 0;
	uint32_t descriptorSizeSRV = 0;
	uint32_t descriptorSizeDSV = 0;

	// デスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;

	// RTVハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = {};

	// フェンス
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	uint64_t fenceValue = 0;
	HANDLE fenceEvent = nullptr;


	// ビューポート矩形
	D3D12_VIEWPORT viewport{};
	// シザリング矩形
	D3D12_RECT scissorRect{};

	// 記録時間(FPS固定用)
	std::chrono::steady_clock::time_point reference_;

	// DXCユーティリティ
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	// DXCコンパイラ
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	// デフォルトインクルードハンドラ
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
};