#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>

// ファイルやディレクトリに関する操作を行うライブラリ
#include <filesystem>

// ファイルに書いたり読んだりするライブラリ
#include <fstream>

// 時間を扱うライブラリ
#include <chrono>

// Dumpを出力
#include <strsafe.h>

// Debug用のあれやこれやを使えるようにする
#include <DbgHelp.h>

//#include <dxcapi.h>
//#pragma comment(lib, "dxcompiler.lib")
#include <dxgidebug.h>

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

std::ofstream logStream;

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	SYSTEMTIME time;
	GetLocalTime(&time);

	wchar_t filePath[MAX_PATH] = { 0 };

	CreateDirectory(L"./Dumps", nullptr);

	StringCchPrintfW(
		filePath,
		MAX_PATH,
		L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",
		time.wYear,
		time.wMonth,
		time.wDay,
		time.wHour,
		time.wMinute
	);

	HANDLE dumpFileHandle =
		CreateFile(
			filePath,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_WRITE | FILE_SHARE_READ,
			0,
			CREATE_ALWAYS,
			0,
			0
		);

	DWORD processId =
		GetCurrentProcessId();

	DWORD threadId =
		GetCurrentThreadId();

	MINIDUMP_EXCEPTION_INFORMATION
		minidumpInformation{};

	minidumpInformation.ThreadId =
		threadId;

	minidumpInformation.ExceptionPointers =
		exception;

	minidumpInformation.ClientPointers =
		TRUE;

	MiniDumpWriteDump(
		GetCurrentProcess(),
		processId,
		dumpFileHandle,
		MiniDumpNormal,
		&minidumpInformation,
		nullptr,
		nullptr
	);

	return EXCEPTION_EXECUTE_HANDLER;
}

void Log(
	std::ostream& os,
	const std::string& message
)
{
	os << message << std::endl;
	OutputDebugStringA(
		message.c_str()
	);
}

void Log(
	const std::string& message
)
{
	OutputDebugStringA(
		message.c_str()
	);
}

std::wstring ConvertString(
	const std::string& str
)
{
	if (str.empty())
	{
		return std::wstring();
	}

	auto sizeNeeded =
		MultiByteToWideChar(
			CP_UTF8,
			0,
			str.data(),
			static_cast<int>(
				str.size()
				),
			NULL,
			0
		);

	if (sizeNeeded == 0)
	{
		return std::wstring();
	}

	std::wstring result(
		sizeNeeded,
		0
	);

	MultiByteToWideChar(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(
			str.size()
			),
		result.data(),
		sizeNeeded
	);

	return result;
}

std::string ConvertString(
	const std::wstring& str
)
{
	if (str.empty())
	{
		return std::string();
	}

	auto sizeNeeded =
		WideCharToMultiByte(
			CP_UTF8,
			0,
			str.data(),
			static_cast<int>(
				str.size()
				),
			NULL,
			0,
			NULL,
			NULL
		);

	if (sizeNeeded == 0)
	{
		return std::string();
	}

	std::string result(
		sizeNeeded,
		0
	);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(
			str.size()
			),
		result.data(),
		sizeNeeded,
		NULL,
		NULL
	);

	return result;
}

LRESULT CALLBACK WindowProc(
	HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
);

int WINAPI WinMain(
	_In_ HINSTANCE,
	_In_opt_ HINSTANCE,
	_In_ LPSTR,
	_In_ int
)
{
	SetUnhandledExceptionFilter(
		ExportDump
	);

	WNDCLASS wc{};

	wc.lpfnWndProc =
		WindowProc;

	wc.lpszClassName =
		L"CG2WindowClass";

	wc.hInstance =
		GetModuleHandle(
			nullptr
		);

	wc.hCursor =
		LoadCursor(
			nullptr,
			IDC_ARROW
		);

	RegisterClass(&wc);

	const int32_t
		kClientWidth = 1280;

	const int32_t
		kClientHeight = 720;

	RECT wrc =
	{
		0,
		0,
		kClientWidth,
		kClientHeight
	};

	AdjustWindowRect(
		&wrc,
		WS_OVERLAPPEDWINDOW,
		false
	);

	HWND hwnd =
		CreateWindow(
			wc.lpszClassName,
			L"CG2",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			wrc.right - wrc.left,
			wrc.bottom - wrc.top,
			nullptr,
			nullptr,
			wc.hInstance,
			nullptr
		);

	ShowWindow(
		hwnd,
		SW_SHOW
	);

	MSG msg{};

	IDXGIFactory7*
		dxgiFactory =
		nullptr;

	HRESULT hr =
		CreateDXGIFactory(
			IID_PPV_ARGS(
				&dxgiFactory
			)
		);

	assert(
		SUCCEEDED(hr)
	);

	IDXGIAdapter4*
		useAdapter =
		nullptr;

	ID3D12Device*
		device =
		nullptr;
	//機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL
		featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};

	for (
		UINT i = 0;
		dxgiFactory->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(
				&useAdapter
			)
		) != DXGI_ERROR_NOT_FOUND;
		++i
		)
	{
		DXGI_ADAPTER_DESC3
			adapterDesc{};

		hr =
			useAdapter->GetDesc3(
				&adapterDesc
			);

		assert(
			SUCCEEDED(hr)
		);

		if (
			!(
				adapterDesc.Flags &
				DXGI_ADAPTER_FLAG3_SOFTWARE
				)
			)
		{
			break;
		}

		useAdapter =
			nullptr;
	}

	assert(
		useAdapter
	);

	for (
		size_t i = 0;
		i < _countof(
			featureLevels
		);
		++i
		)
	{
		hr =
			D3D12CreateDevice(
				useAdapter,
				featureLevels[i],
				IID_PPV_ARGS(
					&device
				)
			);

		if (
			SUCCEEDED(hr)
			)
		{
			break;
		}
	}

	assert(device);

	ID3D12CommandQueue*
		commandQueue =
		nullptr;

	D3D12_COMMAND_QUEUE_DESC
		commandQueueDesc{};

	hr =
		device->CreateCommandQueue(
			&commandQueueDesc,
			IID_PPV_ARGS(
				&commandQueue
			)
		);

	assert(
		SUCCEEDED(hr)
	);

	ID3D12CommandAllocator*
		commandAllocator =
		nullptr;

	hr =
		device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(
				&commandAllocator
			)
		);

	assert(
		SUCCEEDED(hr)
	);

	ID3D12GraphicsCommandList*
		commandList =
		nullptr;

	hr =
		device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			commandAllocator,
			nullptr,
			IID_PPV_ARGS(
				&commandList
			)
		);

	assert(
		SUCCEEDED(hr)
	);

	// Fence生成
	ID3D12Fence*
		fence =
		nullptr;

	uint64_t
		fenceValue = 0;

	hr =
		device->CreateFence(
			fenceValue,
			D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(
				&fence
			)
		);

	assert(
		SUCCEEDED(hr)
	);

	// Event生成

	HANDLE
		fenceEvent =
		CreateEvent(
			nullptr,
			FALSE,
			FALSE,
			nullptr
		);

	assert(
		fenceEvent
	);

	IDXGISwapChain4*
		swapChain =
		nullptr;

	DXGI_SWAP_CHAIN_DESC1
		swapChainDesc{};

	swapChainDesc.Width =
		kClientWidth;

	swapChainDesc.Height =
		kClientHeight;

	swapChainDesc.Format =
		DXGI_FORMAT_R8G8B8A8_UNORM;

	swapChainDesc.SampleDesc.Count =
		1;

	swapChainDesc.BufferUsage =
		DXGI_USAGE_RENDER_TARGET_OUTPUT;

	swapChainDesc.BufferCount =
		2;

	swapChainDesc.SwapEffect =
		DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr =
		dxgiFactory->CreateSwapChainForHwnd(
			commandQueue,
			hwnd,
			&swapChainDesc,
			nullptr,
			nullptr,
			reinterpret_cast<
			IDXGISwapChain1**
			>(
				&swapChain
				)
		);

	assert(
		SUCCEEDED(hr)
	);

	ID3D12DescriptorHeap*
		rtvDescriptorHeap =
		nullptr;

	D3D12_DESCRIPTOR_HEAP_DESC
		rtvHeapDesc{};

	rtvHeapDesc.Type =
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	rtvHeapDesc.NumDescriptors =
		2;

	hr =
		device->CreateDescriptorHeap(
			&rtvHeapDesc,
			IID_PPV_ARGS(
				&rtvDescriptorHeap
			)
		);

	assert(
		SUCCEEDED(hr)
	);

	ID3D12Resource*
		swapChainResources[2]
		=
	{
		nullptr,
		nullptr
	};

	hr = swapChain->GetBuffer(
		0,
		IID_PPV_ARGS(
			&swapChainResources[0]
		)
	);
	assert(SUCCEEDED(hr));

	hr = swapChain->GetBuffer(
		1,
		IID_PPV_ARGS(
			&swapChainResources[1]
		)
	);
	assert(SUCCEEDED(hr));
	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC
		rtvDesc{};

	rtvDesc.Format =
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	rtvDesc.ViewDimension =
		D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE
		rtvStartHandle =
		rtvDescriptorHeap
		->GetCPUDescriptorHandleForHeapStart();

	D3D12_CPU_DESCRIPTOR_HANDLE
		rtvHandles[2];

	rtvHandles[0] =
		rtvStartHandle;

	device->CreateRenderTargetView(
		swapChainResources[0],
		&rtvDesc,
		rtvHandles[0]
	);

	rtvHandles[1].ptr =
		rtvHandles[0].ptr +
		device
		->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV
		);

	device->CreateRenderTargetView(
		swapChainResources[1],
		&rtvDesc,
		rtvHandles[1]
	);

	while (
		msg.message != WM_QUIT
		)
	{
		if (
			PeekMessage(
				&msg,
				NULL,
				0,
				0,
				PM_REMOVE
			)
			)
		{
			TranslateMessage(
				&msg
			);

			DispatchMessage(
				&msg
			);
		}
		else
		{
			UINT
				backBufferIndex =
				swapChain
				->GetCurrentBackBufferIndex();

			D3D12_RESOURCE_BARRIER
				barrier{};

			barrier.Type =
				D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

			barrier.Flags =
				D3D12_RESOURCE_BARRIER_FLAG_NONE;

			barrier.Transition.pResource =
				swapChainResources[
					backBufferIndex
				];

			barrier.Transition.StateBefore =
				D3D12_RESOURCE_STATE_PRESENT;

			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			commandList->ResourceBarrier(
				1,
				&barrier
			);

			commandList->OMSetRenderTargets(
				1,
				&rtvHandles[
					backBufferIndex
				],
				false,
				nullptr
			);

			float clearColor[] =
			{
			0.1f,
			0.25f,
			0.5f,
			1.0f
			};

			commandList
				->ClearRenderTargetView(
					rtvHandles[
						backBufferIndex
					],
					clearColor,
					0,
					nullptr
				);

			barrier
				.Transition
				.StateBefore =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier
				.Transition
				.StateAfter =
				D3D12_RESOURCE_STATE_PRESENT;

			commandList->ResourceBarrier(
				1,
				&barrier
			);

			hr =
				commandList->Close();

			assert(
				SUCCEEDED(hr)
			);

			ID3D12CommandList*
				commandLists[] =
			{
			commandList
			};

			commandQueue
				->ExecuteCommandLists(
					1,
					commandLists
				);

			swapChain->Present(
				1,
				0
			);

			fenceValue++;

			hr =
				commandQueue->Signal(
					fence,
					fenceValue
				);

			assert(
				SUCCEEDED(hr)
			);

			if (
				fence
				->GetCompletedValue()
				<
				fenceValue
				)
			{
				fence
					->SetEventOnCompletion(
						fenceValue,
						fenceEvent
					);

				WaitForSingleObject(
					fenceEvent,
					INFINITE
				);
			}

			hr =
				commandAllocator
				->Reset();

			assert(
				SUCCEEDED(hr)
			);

			hr =
				commandList
				->Reset(
					commandAllocator,
					nullptr
				);

			assert(
				SUCCEEDED(hr)
			);
		}
	}
	CloseHandle(fenceEvent);

	fence->Release();

	swapChainResources[0]->Release();
	swapChainResources[1]->Release();

	rtvDescriptorHeap->Release();

	swapChain->Release();

	commandList->Release();

	commandAllocator->Release();

	commandQueue->Release();

	device->Release();

	useAdapter->Release();

	dxgiFactory->Release();

	// リークチェック
	IDXGIDebug1* debugController = nullptr;

	if (SUCCEEDED(
		DXGIGetDebugInterface1(
			0,
			IID_PPV_ARGS(&debugController)
		)))
	{
		debugController->ReportLiveObjects(
			DXGI_DEBUG_ALL,
			DXGI_DEBUG_RLO_ALL
		);

		debugController->Release();
	}

	return 0;
}

LRESULT CALLBACK
WindowProc(
	HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
)
{
	switch (msg)
	{
	case WM_DESTROY:

		PostQuitMessage(
			0
		);

		return 0;
	}
	return DefWindowProc(
		hwnd,
		msg,
		wparam,
		lparam
	);
}