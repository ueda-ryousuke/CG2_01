#pragma once
#include <Windows.h>
#include <wrl.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

class Input
{
public:
	// namespace省略
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	
	void Initialize(HINSTANCE hInstance, HWND hwnd);

	
	void Update();

	
	bool PushKey(BYTE keyNumber) const;

	
	bool TriggerKey(BYTE keyNumber) const;

	
	const BYTE* GetKeyTable() const { return key; }

private:
	ComPtr<IDirectInput8> directInput = nullptr;       // DirectInput本体
	ComPtr<IDirectInputDevice8> keyboard = nullptr;    // キーボードデバイス
	BYTE key[256] = {};                                // 現在のフレームのキー状態
	BYTE keyPre[256] = {};                             // 1フレーム前のキー状態
};