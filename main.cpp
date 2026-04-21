#include <Windows.h>
#include <cstdint>
#include <string>
#include<format>
const int32_t kClientWidth = 1280;
const int32_t kClientHeight = 720;

//ログ関数
void Log(const std::string& message)
{
	OutputDebugStringA(message.c_str());
}
std::wstring ConvertString(const std::string& str)
{
	if (str.empty())
	{
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0)
	{
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str)
{
	if (str.empty())
	{
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0)
	{
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}
//ウィンドウプロシージャ
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	//メッセンジャーに応じてゲーム固有の処理を行う
	switch (msg)
	{
	case WM_DESTROY:
		//OSに対して、アプリの終了を教える
		PostQuitMessage(0);
		return 0;
	}
	//標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{

	//ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };
	// クライアント領域を元に実際にwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	WNDCLASS wc{};
	//ウィンドウプロシージャ
	wc.lpfnWndProc = windowProc;
	//ウィンドウクラスの登録する
	wc.lpszClassName = L"CG2WindowClass";
	//インスタントハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	//カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	//ウィンドウクラスの登録する
	RegisterClass(&wc);

	OutputDebugStringA("Hello,DirectX!\n");	// 出力ウィンドウへの文字出力

	//ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,
		L"CG2",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr, nullptr,
		wc.hInstance,
		nullptr
	);

	//ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};
	//ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT)
	{
		//Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			//ゲームの処理
			//文字列を格納する
			std::string str0{ "STRING!!!\n" };
			//整数を文字列にする
			std::string str1{ std::to_string(10) + "\n" };

			Log(str0);
			Log(str1);
		}
	}
	return 0;
}