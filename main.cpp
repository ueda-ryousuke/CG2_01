#include <Windows.h>

//Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	OutputDebugStringA("Hello,DirectX!\n");	// 出力ウィンドウへの文字出力
	return 0;

}