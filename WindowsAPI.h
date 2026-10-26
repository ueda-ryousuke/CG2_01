#pragma once
#include <windows.h>

class WindowsAPI
{
public: // 静的メンバ関数
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

public: // メンバ関数

	// 初期化
	void Initialize();

	// 更新
	void Update();
};