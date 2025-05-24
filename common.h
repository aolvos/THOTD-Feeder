#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <Windows.h>
#include <commctrl.h>
#include <ctype.h>
#include <stdio.h>

#include "resource.h"

#define className "SHURAFEN!"
#define winName   "THOTD Feeder [vJoy]"
#define winW      320
#define winH      240

extern HWND hWnd;
LRESULT CALLBACK WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void fatalError(HWND hWnd, char* msg);
void warnUser(HWND hWnd, char* msg);
