#pragma once
#include<Windows.h>

class WinApp {
private:
	WNDCLASSEX w{};
	HWND hwnd = nullptr;
public:
	static const int window_width = 1200;//â°ïù
	static const int window_height = 600;//ècïù

	static LRESULT WindowProc(HWND hwnd, UINT msg, WPARAM wparm, LPARAM lparam);
	//getter
	HWND GetHwnd() { return hwnd; }
	HINSTANCE GetHInstance() { return w.hInstance; }
public:
	void Initialize();
	void Update();
	bool ProcessMessage();
	void Finalize();
};

