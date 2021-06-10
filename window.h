#pragma once
#include <windows.h>

LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
HWND create_window(int width, int height);
