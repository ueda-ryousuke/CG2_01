#include <Windows.h>
#include<cstdint>

const int32_t kClientWidth = 1280;
const int32_t kClientHeight = 720;



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
		}
	}
	return 0;
}